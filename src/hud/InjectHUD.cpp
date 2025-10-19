#include "InjectHUD.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "../elemental_reactions/ElementalGauges.h"
#include "Offsets.h"
#include "Utils.h"

namespace InjectHUD {
    TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
    SKSE::PluginHandle g_pluginHandle = static_cast<SKSE::PluginHandle>(-1);
    std::unordered_map<RE::FormID, HUDEntry> widgets{};
    std::unordered_map<RE::FormID, std::vector<ActiveReactionHUD>> combos;
    std::deque<PendingReaction> g_comboQueue;
    std::mutex g_comboMx;
}

namespace {
    using namespace InjectHUD;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    static void DrainComboQueueOnUI() {
        std::deque<PendingReaction> take;
        {
            std::scoped_lock lk(g_comboMx);
            take.swap(g_comboQueue);
        }
        if (take.empty()) return;

        const double nowRt = NowRtS();
        const float nowH = NowHours();

        auto& RR = ReactionRegistry::get();

        for (auto& pr : take) {
            RE::Actor* a = pr.handle ? pr.handle.get().get() : nullptr;
            if (a) {
                InjectHUD::AddFor(a);
            }

            ActiveReactionHUD hud{};
            hud.reaction = pr.reaction;
            hud.realTime = pr.realTime;
            hud.durationS = pr.secs;

            if (pr.realTime) {
                hud.endRtS = nowRt + pr.secs;
            } else {
                hud.endH = nowH + (pr.secs / 3600.0f);
            }

            if (const auto* rd = RR.get(pr.reaction)) {
                hud.iconPath = rd->hud.iconPath;
                hud.tint = rd->hud.iconTint;
            } else {
                hud.iconPath = "img://textures/erf/icons/icon_fire.dds";
                hud.tint = 0xFFFFFF;
            }

            auto& vec = combos[pr.id];
            std::erase_if(vec, [nowRt, nowH](const ActiveReactionHUD& c) {
                return c.realTime ? (nowRt >= c.endRtS) : (nowH >= c.endH);
            });
            vec.push_back(std::move(hud));
        }
    }

    static std::vector<ActiveReactionHUD> GetActiveReactions(RE::FormID id) {
        std::vector<ActiveReactionHUD> out;
        const auto it = combos.find(id);
        if (it == combos.end()) {
            return out;
        }

        const double nowRt = NowRtS();
        const float nowH = RE::Calendar::GetSingleton()->GetHoursPassed();

        for (const auto& c : it->second) {
            const bool alive = c.realTime ? (nowRt < c.endRtS) : (nowH < c.endH);
            if (alive) out.push_back(c);
        }

        return out;
    }

    inline bool IsPlayerActor(RE::Actor* a) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        return a && pc && (a == pc);
    }

    void FollowPlayerFixed(InjectHUD::ERFWidget& w) {
        if (!w._view) {
            return;
        }
        RE::GRectF rect = w._view->GetVisibleFrameRect();
        const double baseX = rect.left + InjectHUD::ERFWidget::kPlayerMarginLeftPx;
        const double baseY = rect.bottom - InjectHUD::ERFWidget::kPlayerMarginBottomPx;

        const double targetX = baseX;
        const double targetY = baseY;

        if (w._needsSnap || std::isnan(w._lastX) || std::isnan(w._lastY)) {
            w._lastX = targetX;
            w._lastY = targetY;
            w._needsSnap = false;
        }

        const double px = std::floor(targetX + 0.5);
        const double py = std::floor(targetY + 0.5);

        RE::GFxValue::DisplayInfo di;
        di.SetPosition(static_cast<float>(px), static_cast<float>(py));
        di.SetScale(100.0f * InjectHUD::ERFWidget::kPlayerScale, 100.0f * InjectHUD::ERFWidget::kPlayerScale);
        w._object.SetDisplayInfo(di);

        RE::GFxValue vis;
        vis.SetBoolean(true);
        w._object.SetMember("_visible", vis);

        w._lastX = px;
        w._lastY = py;
    }
}

void InjectHUD::ERFWidget::Initialize() {
    if (!_view) {
        return;
    }
    RE::GFxValue vis, alpha;
    vis.SetBoolean(false);
    alpha.SetNumber(100.0);
    _object.SetMember("_visible", vis);
    _object.SetMember("_alpha", alpha);

    _needsSnap = true;
    ResetSmoothing();
    _hadContent = false;
    _lastGaugeRtS = std::numeric_limits<double>::quiet_NaN();
}

void InjectHUD::ERFWidget::FollowActorHead(RE::Actor* actor) {
    if (!actor || !_view) return;
    if (IsPlayerActor(actor)) {
        FollowPlayerFixed(*this);
        return;
    }

    if (!g_viewPort || !g_worldToCamMatrix) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    RE::NiPoint3 world{};
    if (!Utils::GetTargetPos(actor->CreateRefHandle(), world, true)) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    static constexpr float kWorldOffsetZ = 70.0f;
    world.z += kWorldOffsetZ;

    float nx = 0.f, ny = 0.f, depth = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, world, nx, ny, depth, 1e-5f);
    if (depth < 0.f || nx < 0.f || nx > 1.f || ny < 0.f || ny > 1.f) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    const RE::GRectF rect = _view->GetVisibleFrameRect();
    const float stageW = rect.right - rect.left;
    const float stageH = rect.bottom - rect.top;
    if (stageW <= 1.f || stageH <= 1.f) return;

    ny = 1.0f - ny;
    double px = rect.left + stageW * nx;
    double py = rect.top + stageH * ny;

    float scalePct = 100.f;
    if (g_fNear && g_fFar) {
        const float fNear = *g_fNear, fFar = *g_fFar;
        const float lin = fNear * fFar / (fFar + depth * (fNear - fFar));
        const float clamped = std::clamp(lin, 500.f, 2000.f);
        scalePct = (((clamped - 500.f) * (50.f - 100.f)) / (2000.f - 500.f)) + 100.f;
    }

    if (_needsSnap || std::isnan(_lastX) || std::isnan(_lastY)) {
        _lastX = px;
        _lastY = py;
        _needsSnap = false;
    }

    px = std::floor(px + 0.5);
    py = std::floor(py + 0.5);
    RE::GFxValue::DisplayInfo di;
    di.SetPosition(static_cast<float>(px), static_cast<float>(py));
    di.SetScale(scalePct, scalePct);
    _object.SetDisplayInfo(di);

    RE::GFxValue vis;
    vis.SetBoolean(true);
    _object.SetMember("_visible", vis);

    _lastX = px;
    _lastY = py;
}

void InjectHUD::ERFWidget::SetAll(const std::vector<std::string>& comboIconPaths,
                                  const std::vector<double>& comboRemain01,
                                  const std::vector<std::uint32_t>& comboTintsRGB, const std::string& accumIconPath,
                                  const std::vector<double>& accumValues,
                                  const std::vector<std::uint32_t>& accumColorsRGB, std::uint32_t accumTintRGB) {
    if (!_view) {
        return;
    }

    RE::GFxValue args[7], ret;

    RE::GFxValue arrComboPaths, arrComboRemain, arrComboTints;
    RE::GFxValue arrAccumVals, arrAccumCols;

    _view->CreateArray(&arrComboPaths);
    _view->CreateArray(&arrComboRemain);
    _view->CreateArray(&arrComboTints);
    _view->CreateArray(&arrAccumVals);
    _view->CreateArray(&arrAccumCols);

    for (auto& s : comboIconPaths) {
        RE::GFxValue v;
        v.SetString(s.c_str());
        arrComboPaths.PushBack(v);
    }
    for (auto r : comboRemain01) {
        RE::GFxValue v;
        v.SetNumber(r);
        arrComboRemain.PushBack(v);
    }
    for (auto c : comboTintsRGB) {
        RE::GFxValue v;
        v.SetNumber(double(c));
        arrComboTints.PushBack(v);
    }

    for (auto v2 : accumValues) {
        RE::GFxValue v;
        v.SetNumber(v2);
        arrAccumVals.PushBack(v);
    }
    for (auto c2 : accumColorsRGB) {
        RE::GFxValue v;
        v.SetNumber(double(c2));
        arrAccumCols.PushBack(v);
    }

    RE::GFxValue accumIcon;
    accumIcon.SetString(accumIconPath.c_str());
    RE::GFxValue accumTint;
    accumTint.SetNumber(double(accumTintRGB));

    args[0] = arrComboPaths;
    args[1] = arrComboRemain;
    args[2] = arrComboTints;
    args[3] = accumIcon;
    args[4] = arrAccumVals;
    args[5] = arrAccumCols;
    args[6] = accumTint;

    const bool ok = _object.Invoke("setAll", &ret, args, 7);

    RE::GFxValue vis;
    vis.SetBoolean(ok);
    _object.SetMember("_visible", vis);
}

void InjectHUD::AddFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) {
        return;
    }

    const auto id = actor->GetFormID();
    auto& entry = widgets[id];
    entry.handle = actor->CreateRefHandle();

    if (entry.widget) {
        return;
    }

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true) && !IsPlayerActor(actor)) {
        return;
    }

    auto w = std::make_shared<ERFWidget>();
    const auto wid = id;
    g_trueHUD->AddWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, ERF_SYMBOL_NAME, w);
    w->ProcessDelegates();
    entry.widget = std::move(w);
}

void InjectHUD::UpdateFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) {
        return;
    }

    const auto id = actor->GetFormID();
    auto it = widgets.find(id);
    if (it == widgets.end() || !it->second.widget) {
        return;
    }
    auto& w = *it->second.widget;

    const auto actives = GetActiveReactions(id);

    auto bundleOpt = ElementalGauges::PickHudIconDecayed(id);
    const bool haveTotals =
        bundleOpt && !bundleOpt->values.empty() &&
        std::any_of(bundleOpt->values.begin(), bundleOpt->values.end(), [](std::uint32_t v) { return v > 0; });

    const int needed = std::clamp<int>(static_cast<int>(actives.size()) + (haveTotals ? 1 : 0), 0, 3);
    if (needed == 0) {
        if (w._view) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            w._object.SetMember("_visible", vis);
        }
        return;
    }

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true) && !IsPlayerActor(actor)) {
        return;
    }

    std::vector<std::string> comboIconPaths;
    std::vector<double> comboRemain01;
    std::vector<std::uint32_t> comboTintsRGB;

    comboIconPaths.reserve(std::min<std::size_t>(3, actives.size()));
    comboRemain01.reserve(comboIconPaths.capacity());
    comboTintsRGB.reserve(comboIconPaths.capacity());

    const double nowRt = NowRtS();
    const float nowH = RE::Calendar::GetSingleton()->GetHoursPassed();
    for (std::size_t i = 0; i < actives.size() && i < 3; ++i) {
        const auto& r = actives[i];
        const double remain = r.realTime ? (r.endRtS - nowRt) : (double(r.endH - nowH) * 3600.0);
        const double denom = std::max(0.001, (double)r.durationS);
        const double frac = std::clamp(remain / denom, 0.0, 1.0);

        comboIconPaths.push_back(r.iconPath);
        comboRemain01.push_back(frac);
        comboTintsRGB.push_back(r.tint);
    }

    std::string accumIconPath;
    std::vector<double> accumValues;
    std::vector<std::uint32_t> accumColorsRGB;
    std::uint32_t accumTintRGB = 0;

    if (haveTotals) {
        const auto& b = *bundleOpt;
        accumIconPath = b.iconPath;
        accumTintRGB = b.iconTint;
        accumValues.reserve(b.values.size());
        for (auto v : b.values) accumValues.push_back(double(std::clamp<std::uint32_t>(v, 0, 100)));
        accumColorsRGB.assign(b.colors.begin(), b.colors.end());
    }

    w.FollowActorHead(actor);
    w.SetAll(comboIconPaths, comboRemain01, comboTintsRGB, accumIconPath, accumValues, accumColorsRGB, accumTintRGB);
}

void InjectHUD::BeginReaction(RE::Actor* a, ERF_ReactionHandle handle, float seconds, bool realTime) {
    if (!a || seconds <= 0.f) return;

    const auto id = a->GetFormID();

    PendingReaction pr{};
    pr.id = id;
    pr.handle = a->CreateRefHandle();
    pr.reaction = handle;
    pr.secs = seconds;
    pr.realTime = realTime;

    {
        std::scoped_lock lk(g_comboMx);
        g_comboQueue.push_back(std::move(pr));
    }
}

bool InjectHUD::RemoveFor(RE::FormID id) {
    if (!g_trueHUD || !id) return false;
    auto it = widgets.find(id);
    if (it == widgets.end()) return false;

    if (it->second.widget) {
        const auto wid = id;
        g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, TRUEHUD_API::WidgetRemovalMode::Immediate);
        it->second.widget.reset();
    }
    widgets.erase(it);
    combos.erase(id);
    return true;
}

void InjectHUD::RemoveAllWidgets() {
    if (!g_trueHUD) {
        widgets.clear();
        combos.clear();
        return;
    }

    for (const auto& [id, entry] : widgets) {
        if (!entry.widget) continue;
        const auto wid = id;
        g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, TRUEHUD_API::WidgetRemovalMode::Immediate);
    }
    widgets.clear();
    combos.clear();
}

void InjectHUD::OnTrueHUDClose() {
    RemoveAllWidgets();
    HUD::ResetTracking();
}

void InjectHUD::OnUIFrameBegin() { DrainComboQueueOnUI(); }

double InjectHUD::NowRtS() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}