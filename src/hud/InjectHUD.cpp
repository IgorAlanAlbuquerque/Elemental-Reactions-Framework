#include "InjectHUD.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "../Config.h"
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

    static void DrainComboQueueOnUI(double nowRt, float nowH) {
        std::deque<PendingReaction> take;
        {
            std::scoped_lock lk(g_comboMx);
            take.swap(g_comboQueue);
        }
        if (take.empty()) return;

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

            if (pr.realTime)
                hud.endRtS = nowRt + pr.secs;
            else
                hud.endH = nowH + (pr.secs / 3600.0f);

            if (const auto* rd = RR.get(pr.reaction)) {
                hud.tint = rd->Tint;
            } else {
                hud.tint = 0xFFFFFF;
            }

            auto& vec = combos[pr.id];
            std::erase_if(vec, [nowRt, nowH](const ActiveReactionHUD& c) {
                return c.realTime ? (nowRt >= c.endRtS) : (nowH >= c.endH);
            });
            vec.push_back(std::move(hud));
        }
    }

    std::vector<ActiveReactionHUD> GetActiveReactions(RE::FormID id, double nowRt, float nowH) {
        std::vector<ActiveReactionHUD> out;
        const auto it = combos.find(id);
        if (it == combos.end()) return out;

        for (const auto& c : it->second) {
            const bool alive = c.realTime ? (nowRt < c.endRtS) : (nowH < c.endH);
            if (alive) out.push_back(c);
        }
        return out;
    }

    inline bool IsPlayerActor(RE::Actor* a) { return a && a->IsPlayerRef(); }

    void FollowPlayerFixed(InjectHUD::ERFWidget& w) {
        if (!w._view) {
            return;
        }

        spdlog::info("[ERF HUD] FollowPlayerFixed");
        RE::GRectF rect = w._view->GetVisibleFrameRect();
        const double targetX = rect.left + InjectHUD::ERFWidget::kPlayerMarginLeftPx;
        const double targetY = rect.bottom - InjectHUD::ERFWidget::kPlayerMarginBottomPx;

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

    _arraysInit = false;
    EnsureArrays();
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

    constexpr double SMOOTH_FACTOR = 0.15;
    constexpr double MIN_DELTA = 0.25;

    if (_needsSnap || std::isnan(_lastX) || std::isnan(_lastY)) {
        _lastX = px;
        _lastY = py;
        _needsSnap = false;
    }

    const double dx = px - _lastX;
    const double dy = py - _lastY;

    if (std::abs(dx) > MIN_DELTA || std::abs(dy) > MIN_DELTA) {
        px = _lastX + dx * SMOOTH_FACTOR;
        py = _lastY + dy * SMOOTH_FACTOR;
    } else {
        px = _lastX;
        py = _lastY;
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

void InjectHUD::ERFWidget::EnsureArrays() {
    if (!(_view && !_arraysInit)) return;

    _view->CreateArray(&_arrComboRemain);
    _view->CreateArray(&_arrComboTints);
    _view->CreateArray(&_arrAccumVals);
    _view->CreateArray(&_arrAccumCols);

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;

    _arraysInit = true;
}

void InjectHUD::ERFWidget::FillArrayDoubles(RE::GFxValue& arr, const std::vector<double>& src) {
    const std::uint32_t cur = arr.GetArraySize();
    if (cur != src.size()) {
        _view->CreateArray(&arr);
        for (double d : src) {
            RE::GFxValue v;
            v.SetNumber(d);
            arr.PushBack(v);
        }
        return;
    }
    for (std::uint32_t i = 0; i < cur; ++i) {
        RE::GFxValue v;
        v.SetNumber(src[i]);
        arr.SetElement(i, v);
    }
}

void InjectHUD::ERFWidget::FillArrayU32AsNumber(RE::GFxValue& arr, const std::vector<std::uint32_t>& src) {
    const std::uint32_t cur = arr.GetArraySize();
    if (cur != src.size()) {
        _view->CreateArray(&arr);
        for (std::uint32_t u : src) {
            RE::GFxValue v;
            v.SetNumber(static_cast<double>(u));
            arr.PushBack(v);
        }
        return;
    }
    for (std::uint32_t i = 0; i < cur; ++i) {
        RE::GFxValue v;
        v.SetNumber(static_cast<double>(src[i]));
        arr.SetElement(i, v);
    }
}

void InjectHUD::ERFWidget::ClearAndHide() {
    if (!_view) return;

    EnsureArrays();

    _view->CreateArray(&_arrComboRemain);
    _view->CreateArray(&_arrComboTints);
    _view->CreateArray(&_arrAccumVals);
    _view->CreateArray(&_arrAccumCols);
    _isSingle.SetBoolean(ERF::GetConfig().isSingle.load(std::memory_order_relaxed));

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;
    _args[4] = _isSingle;

    RE::GFxValue ret;
    _object.Invoke("setAll", &ret, _args, 5);

    RE::GFxValue vis;
    vis.SetBoolean(false);
    _object.SetMember("_visible", vis);
}

void InjectHUD::ERFWidget::SetAll(const std::vector<double>& comboRemain01,
                                  const std::vector<std::uint32_t>& comboTintsRGB,
                                  const std::vector<double>& accumValues,
                                  const std::vector<std::uint32_t>& accumColorsRGB) {
    if (!_view) return;

    EnsureArrays();

    FillArrayDoubles(_arrComboRemain, comboRemain01);
    FillArrayU32AsNumber(_arrComboTints, comboTintsRGB);
    FillArrayDoubles(_arrAccumVals, accumValues);
    FillArrayU32AsNumber(_arrAccumCols, accumColorsRGB);
    _isSingle.SetBoolean(ERF::GetConfig().isSingle.load(std::memory_order_relaxed));

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;
    _args[4] = _isSingle;

    RE::GFxValue ret;
    const bool ok = _object.Invoke("setAll", &ret, _args, 5);

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

    auto w = std::make_shared<ERFWidget>();
    const auto wid = actor->GetFormID();
    g_trueHUD->AddWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, ERF_SYMBOL_NAME, w);
    w->ProcessDelegates();
    entry.widget = std::move(w);
}

void InjectHUD::UpdateFor(RE::Actor* actor, double nowRt, float nowH) {
    if (!g_trueHUD || !actor) return;

    const auto id = actor->GetFormID();
    auto it = widgets.find(id);
    if (it == widgets.end() || !it->second.widget) return;

    auto& w = *it->second.widget;

    const auto actives = GetActiveReactions(id, nowRt, nowH);

    auto bundleOpt = ElementalGauges::PickHudDecayed(id, nowRt, nowH);
    const bool haveTotals =
        bundleOpt && !bundleOpt->values.empty() &&
        std::any_of(bundleOpt->values.begin(), bundleOpt->values.end(), [](std::uint32_t v) { return v > 0; });

    if (const int needed = static_cast<int>(actives.size()) + (haveTotals ? 1 : 0); needed == 0) {
        if (w._view) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            w._object.SetMember("_visible", vis);
        }
        return;
    }

    const auto h = actor->GetHandle();

    std::vector<double> comboRemain01;
    std::vector<std::uint32_t> comboTintsRGB;

    comboRemain01.reserve(comboRemain01.capacity());
    comboTintsRGB.reserve(comboRemain01.capacity());

    for (std::size_t i = 0; i < actives.size() && i < 3; ++i) {
        const auto& r = actives[i];
        const double remain = r.realTime ? (r.endRtS - nowRt) : (double(r.endH - nowH) * 3600.0);
        const double denom = std::max(0.001, (double)r.durationS);
        const double frac = std::clamp(remain / denom, 0.0, 1.0);

        comboRemain01.push_back(frac);
        comboTintsRGB.push_back(r.tint);
    }

    std::vector<double> accumValues;
    std::vector<std::uint32_t> accumColorsRGB;

    if (haveTotals) {
        const auto& b = *bundleOpt;
        accumValues.reserve(b.values.size());
        for (auto v : b.values) accumValues.push_back(double(std::clamp<std::uint32_t>(v, 0, 100)));
        accumColorsRGB.assign(b.colors.begin(), b.colors.end());
    }

    w.FollowActorHead(actor);
    w.SetAll(comboRemain01, comboTintsRGB, accumValues, accumColorsRGB);
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

bool InjectHUD::HideFor(RE::FormID id) {
    auto it = widgets.find(id);
    if (it == widgets.end() || !it->second.widget) return false;

    auto& w = *it->second.widget;
    if (!w._view) return false;

    w.ClearAndHide();
    return true;
}

bool InjectHUD::RemoveFor(RE::FormID id) {
    if (!g_trueHUD || !id) return false;
    auto it = widgets.find(id);
    if (it == widgets.end()) return false;

    if (it->second.widget) {
        g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, id, TRUEHUD_API::WidgetRemovalMode::Immediate);
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
        g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, id, TRUEHUD_API::WidgetRemovalMode::Immediate);
    }
    widgets.clear();
    combos.clear();
}

void InjectHUD::OnTrueHUDClose() {
    RemoveAllWidgets();
    Utils::HeadCacheClearAll();
    HUD::ResetTracking();
}

void InjectHUD::OnUIFrameBegin(double nowRtS, float nowH) { DrainComboQueueOnUI(nowRtS, nowH); }

bool InjectHUD::IsOnScreen(RE::Actor* actor, float worldOffsetZ) noexcept {
    if (!actor || !g_viewPort || !g_worldToCamMatrix) return false;

    RE::NiPoint3 world{};
    if (!Utils::GetTargetPos(actor->CreateRefHandle(), world, /*bGetTorsoPos=*/true)) return false;

    world.z += worldOffsetZ;

    float nx = 0.f;
    float ny = 0.f;
    float depth = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, world, nx, ny, depth, 1e-5f);
    if (depth < 0.f) return false;

    return (nx >= 0.f && nx <= 1.f && ny >= 0.f && ny <= 1.f);
}