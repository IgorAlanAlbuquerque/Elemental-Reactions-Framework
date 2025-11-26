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
#include "HUDTick.h"
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

    static thread_local HUDFrameSnapshot g_snap{};

    constexpr std::size_t kHUD_TLS_CAP = 12;
    struct HUDTLS {
        std::vector<double> comboRemain01;
        std::vector<std::uint32_t> comboTintsRGB;
        std::vector<double> accumValues;
        std::vector<std::uint32_t> accumColorsRGB;
        std::vector<const char*> IconNames;
        std::vector<ActiveReactionHUD> actives;
        HUDTLS() {
            comboRemain01.reserve(kHUD_TLS_CAP);
            comboTintsRGB.reserve(kHUD_TLS_CAP);
            accumValues.reserve(kHUD_TLS_CAP);
            accumColorsRGB.reserve(kHUD_TLS_CAP);
            IconNames.reserve(kHUD_TLS_CAP * 2);
            actives.reserve(8);
        }
    };
    thread_local HUDTLS g_hudTLS;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    void DrainComboQueueOnUI(double nowRt, float nowH) {
        std::deque<PendingReaction> take;
        {
            std::scoped_lock lk(g_comboMx);
            take.swap(g_comboQueue);
        }
        if (take.empty()) return;

        auto const& RR = ReactionRegistry::get();

        for (auto const& pr : take) {
            if (RE::Actor* a = pr.handle ? pr.handle.get().get() : nullptr; a) {
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
                hud.icon = rd->iconName.empty() ? nullptr : rd->iconName.c_str();
            } else {
                hud.tint = 0xFFFFFF;
                hud.icon = nullptr;
            }

            auto [__itC, __insertedC] = combos.try_emplace(pr.id);
            auto& vec = __itC->second;
            std::erase_if(vec, [nowRt, nowH](const ActiveReactionHUD& c) {
                return c.realTime ? (nowRt >= c.endRtS) : (nowH >= c.endH);
            });
            vec.push_back(std::move(hud));
        }
    }

    void GetActiveReactions(RE::FormID id, double nowRt, float nowH, std::vector<ActiveReactionHUD>& out) {
        out.clear();

        const auto it = combos.find(id);
        if (it == combos.end()) return;

        auto& vec = it->second;

        std::erase_if(vec, [nowRt, nowH](const ActiveReactionHUD& c) {
            return c.realTime ? (nowRt >= c.endRtS) : (nowH >= c.endH);
        });

        if (out.capacity() < vec.size()) out.reserve(vec.size());
        out.insert(out.end(), vec.begin(), vec.end());
    }

    inline bool IsPlayerActor(RE::Actor* a) { return a && a->IsPlayerRef(); }

    void FollowPlayerFixed(InjectHUD::ERFWidget& w) {
        if (!w._view) return;

        const RE::GRectF rect = w._view->GetVisibleFrameRect();
        const double targetX = rect.left + InjectHUD::ERFWidget::kPlayerMarginLeftPx + g_snap.playerX;
        const double targetY = rect.bottom - InjectHUD::ERFWidget::kPlayerMarginBottomPx - g_snap.playerY;

        if (w._needsSnap || std::isnan(w._lastX) || std::isnan(w._lastY) || std::isnan(w._lastScale)) {
            w._lastX = targetX;
            w._lastY = targetY;
            w._lastScale = 100.0f * InjectHUD::ERFWidget::kPlayerScale * g_snap.playerScale;
            w._needsSnap = false;
        }

        const double px = std::floor(targetX + 0.5);
        const double py = std::floor(targetY + 0.5);
        const float sc = 100.0f * InjectHUD::ERFWidget::kPlayerScale * g_snap.playerScale;

        constexpr double EPS_POS = 0.5;
        constexpr float EPS_SCL = 0.01f;
        const bool posChanged = (std::fabs(px - w._lastX) > EPS_POS) || (std::fabs(py - w._lastY) > EPS_POS);

        if (const bool sclChanged = (!std::isfinite(w._lastScale)) || (std::fabs(sc - w._lastScale) > EPS_SCL);
            posChanged || sclChanged) {
            RE::GFxValue::DisplayInfo di;
            di.SetPosition(static_cast<float>(px), static_cast<float>(py));
            di.SetScale(sc, sc);
            w._object.SetDisplayInfo(di);
            w._lastX = px;
            w._lastY = py;
            w._lastScale = sc;
        }

        if (!w._lastVisible) {
            RE::GFxValue vis;
            vis.SetBoolean(true);
            w._object.SetMember("_visible", vis);
            w._lastVisible = true;
        }
    }

    static inline std::uint64_t fnv1a64_init() { return 1469598103934665603ull; }
    static inline std::uint64_t fnv1a64_mix(std::uint64_t h, std::uint8_t b) {
        h ^= b;
        return h * 1099511628211ull;
    }
    template <class It>
    static std::uint64_t hash_bytes(It first, It last) {
        auto h = fnv1a64_init();
        for (; first != last; ++first) h = fnv1a64_mix(h, static_cast<std::uint8_t>(*first));
        return h;
    }
    static std::uint64_t hash_doubles_q(const std::vector<double>& v) {
        auto h = fnv1a64_init();
        for (double d : v) {
            long long q = llround(d * 1000.0);
            const auto* p = reinterpret_cast<const std::uint8_t*>(&q);
            for (std::size_t i = 0; i < sizeof(q); ++i) h = fnv1a64_mix(h, p[i]);
        }
        return h;
    }
    static std::uint64_t hash_u32(const std::vector<std::uint32_t>& v) {
        auto h = fnv1a64_init();
        for (std::uint32_t x : v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&x);
            for (std::size_t i = 0; i < sizeof(x); ++i) h = fnv1a64_mix(h, p[i]);
        }
        return h;
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
        if (_lastVisible) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            _object.SetMember("_visible", vis);
            _lastVisible = false;
        }
        return;
    }

    RE::NiPoint3 world{};
    if (!Utils::GetTargetPos(actor->CreateRefHandle(), world, true)) {
        if (_lastVisible) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            _object.SetMember("_visible", vis);
            _lastVisible = false;
        }
        return;
    }

    static constexpr float kWorldOffsetZ = 70.0f;
    world.z += kWorldOffsetZ;

    float nx = 0.f, ny = 0.f, depth = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, world, nx, ny, depth, 1e-5f);

    if (depth < 0.f || nx < 0.f || nx > 1.f || ny < 0.f || ny > 1.f) {
        if (_lastVisible) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            _object.SetMember("_visible", vis);
            _lastVisible = false;
        }
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

    if (_needsSnap || std::isnan(_lastX) || std::isnan(_lastY) || std::isnan(_lastScale)) {
        _lastX = px;
        _lastY = py;
        _lastScale = scalePct * std::max(0.0f, g_snap.npcScale);
        _needsSnap = false;
    }

    const double dx = px - _lastX;

    if (const double dy = py - _lastY; std::abs(dx) > MIN_DELTA || std::abs(dy) > MIN_DELTA) {
        px = _lastX + dx * SMOOTH_FACTOR;
        py = _lastY + dy * SMOOTH_FACTOR;
    } else {
        px = _lastX;
        py = _lastY;
    }

    const float offX = g_snap.npcX;
    const float offY = g_snap.npcY;
    const float scMul = std::max(0.0f, g_snap.npcScale);

    px += offX;
    py -= offY;

    px = std::floor(px + 0.5);
    py = std::floor(py + 0.5);

    const float finalScalePct = scalePct * scMul;

    constexpr double EPS_POS = 0.5;
    constexpr float EPS_SCL = 0.01f;
    const bool posChanged = (std::fabs(px - _lastX) > EPS_POS) || (std::fabs(py - _lastY) > EPS_POS);
    if (const bool sclChanged = (!std::isfinite(_lastScale)) || (std::fabs(finalScalePct - _lastScale) > EPS_SCL);
        posChanged || sclChanged) {
        RE::GFxValue::DisplayInfo di;
        di.SetPosition(static_cast<float>(px), static_cast<float>(py));
        di.SetScale(finalScalePct, finalScalePct);
        _object.SetDisplayInfo(di);
        _lastX = px;
        _lastY = py;
        _lastScale = finalScalePct;
    }

    if (!_lastVisible) {
        RE::GFxValue vis;
        vis.SetBoolean(true);
        _object.SetMember("_visible", vis);
        _lastVisible = true;
    }
}

void InjectHUD::ERFWidget::EnsureArrays() {
    if (!(_view && !_arraysInit)) return;

    _view->CreateArray(&_arrComboRemain);
    _view->CreateArray(&_arrComboTints);
    _view->CreateArray(&_arrAccumVals);
    _view->CreateArray(&_arrAccumCols);
    _view->CreateArray(&_arrIconNames);

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;
    _args[4] = _arrIconNames;

    _arraysInit = true;
}

bool InjectHUD::ERFWidget::FillArrayNames(RE::GFxValue& arr, const std::vector<const char*>& names,
                                          std::uint64_t& lastHash) {
    bool changed = false;

    const std::uint32_t cur = arr.GetArraySize();

    if (const auto want = static_cast<std::uint32_t>(names.size()); cur != want) {
        _view->CreateArray(&arr);
        arr.SetArraySize(want);
        for (std::uint32_t i = 0; i < want; ++i) {
            RE::GFxValue v;
            if (names[i] && names[i][0] != '\0')
                v.SetString(names[i]);
            else
                v.SetNull();
            arr.SetElement(i, v);
        }
        changed = true;
    } else {
        for (std::uint32_t i = 0; i < want; ++i) {
            RE::GFxValue v;
            if (names[i] && names[i][0] != '\0')
                v.SetString(names[i]);
            else
                v.SetNull();
            arr.SetElement(i, v);
        }
    }

    std::uint64_t h = fnv1a64_init();
    for (const char* s : names) {
        if (!s) {
            h = fnv1a64_mix(h, 0);
            continue;
        }
        for (const auto* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
            h = fnv1a64_mix(h, *p);
        }
        h = fnv1a64_mix(h, 0);
    }
    if (h != lastHash) {
        lastHash = h;
        changed = true;
    }
    return changed;
}

bool InjectHUD::ERFWidget::FillArrayDoubles(RE::GFxValue& arr, const std::vector<double>& src,
                                            std::uint64_t& lastHash) {
    bool changed = false;
    const std::uint32_t cur = arr.GetArraySize();
    if (const auto want = static_cast<std::uint32_t>(src.size()); cur != want) {
        _view->CreateArray(&arr);
        for (double d : src) {
            RE::GFxValue v;
            v.SetNumber(d);
            arr.PushBack(v);
        }
        changed = true;
    } else {
        for (std::uint32_t i = 0; i < cur; ++i) {
            RE::GFxValue v;
            v.SetNumber(src[i]);
            arr.SetElement(i, v);
        }
    }

    if (const std::uint64_t h = hash_doubles_q(src); h != lastHash) {
        changed = true;
        lastHash = h;
    }
    return changed;
}

bool InjectHUD::ERFWidget::FillArrayU32AsNumber(RE::GFxValue& arr, const std::vector<std::uint32_t>& src,
                                                std::uint64_t& lastHash) {
    bool changed = false;
    const std::uint32_t cur = arr.GetArraySize();

    if (const auto want = static_cast<std::uint32_t>(src.size()); cur != want) {
        _view->CreateArray(&arr);
        for (std::uint32_t u : src) {
            RE::GFxValue v;
            v.SetNumber(static_cast<double>(u));
            arr.PushBack(v);
        }
        changed = true;
    } else {
        for (std::uint32_t i = 0; i < cur; ++i) {
            RE::GFxValue v;
            v.SetNumber(static_cast<double>(src[i]));
            arr.SetElement(i, v);
        }
    }

    if (const std::uint64_t h = hash_u32(src); h != lastHash) {
        changed = true;
        lastHash = h;
    }
    return changed;
}

void InjectHUD::ERFWidget::ClearAndHide(bool isSingle, bool isHorizontal, float spacingPx) {
    if (!_view) return;

    EnsureArrays();

    _view->CreateArray(&_arrComboRemain);
    _view->CreateArray(&_arrComboTints);
    _view->CreateArray(&_arrAccumVals);
    _view->CreateArray(&_arrAccumCols);
    _view->CreateArray(&_arrIconNames);

    _isSingle.SetBoolean(isSingle);
    _isHorin.SetBoolean(isHorizontal);
    _spacing.SetNumber(spacingPx);

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;
    _args[4] = _arrIconNames;
    _args[5] = _isSingle;
    _args[6] = _isHorin;
    _args[7] = _spacing;

    RE::GFxValue ret;
    _object.Invoke("setAll", &ret, _args, 8);

    if (_lastVisible) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        _lastVisible = false;
    }
    _lastIsSingle = isSingle;
    _lastIsHor = isHorizontal;
    _lastSpacing = spacingPx;
}

void InjectHUD::ERFWidget::SetAll(const std::vector<double>& comboRemain01,
                                  const std::vector<std::uint32_t>& comboTintsRGB,
                                  const std::vector<double>& accumValues,
                                  const std::vector<std::uint32_t>& accumColorsRGB,
                                  const std::vector<const char*>& iconNames, bool isSingle, bool isHorizontal,
                                  float spacingPx) {
    if (!_view) return;

    EnsureArrays();

    const bool chComboR = FillArrayDoubles(_arrComboRemain, comboRemain01, _hComboRemain);
    const bool chComboT = FillArrayU32AsNumber(_arrComboTints, comboTintsRGB, _hComboTints);
    const bool chAccumV = FillArrayDoubles(_arrAccumVals, accumValues, _hAccumVals);
    const bool chAccumC = FillArrayU32AsNumber(_arrAccumCols, accumColorsRGB, _hAccumCols);
    const bool chIcons = FillArrayNames(_arrIconNames, iconNames, _hIconNames);

    _isSingle.SetBoolean(isSingle);
    _isHorin.SetBoolean(isHorizontal);
    _spacing.SetNumber(spacingPx);

    _args[0] = _arrComboRemain;
    _args[1] = _arrComboTints;
    _args[2] = _arrAccumVals;
    _args[3] = _arrAccumCols;
    _args[4] = _arrIconNames;
    _args[5] = _isSingle;
    _args[6] = _isHorin;
    _args[7] = _spacing;

    const bool flagsChanged = (_lastIsSingle != isSingle) || (_lastIsHor != isHorizontal) ||
                              !(std::isfinite(_lastSpacing) && std::abs(_lastSpacing - spacingPx) < 1e-6);

    const bool needInvoke = flagsChanged || chComboR || chComboT || chAccumV || chAccumC || chIcons;

    RE::GFxValue ret;
    bool ok = true;

    if (needInvoke) {
        ok = _object.Invoke("setAll", &ret, _args, 8);
        _lastIsSingle = isSingle;
        _lastIsHor = isHorizontal;
        _lastSpacing = spacingPx;
    }

    if (_lastVisible != ok) {
        RE::GFxValue vis;
        vis.SetBoolean(ok);
        _object.SetMember("_visible", vis);
        _lastVisible = ok;
    }
}

void InjectHUD::AddFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) {
        return;
    }

    const auto id = actor->GetFormID();
    auto [__itW, __insertedW] = widgets.try_emplace(id);
    auto& entry = __itW->second;
    entry.handle = actor->CreateRefHandle();

    if (entry.widget) {
        return;
    }

    const auto h = actor->GetHandle();

    auto w = std::make_shared<ERFWidget>();
    w->_isPlayerWidget = actor->IsPlayerRef();
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

    auto& acts = g_hudTLS.actives;
    acts.clear();
    GetActiveReactions(id, nowRt, nowH, acts);

    auto bundleOpt = ElementalGauges::PickHudDecayed(id, nowRt, nowH);
    const bool haveTotals =
        bundleOpt && !bundleOpt->values.empty() &&
        std::any_of(bundleOpt->values.begin(), bundleOpt->values.end(), [](std::uint32_t v) { return v > 0; });

    if (const int needed = static_cast<int>(acts.size()) + (haveTotals ? 1 : 0); needed == 0) {
        if (w._view && w._lastVisible) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            w._object.SetMember("_visible", vis);
            w._lastVisible = false;
        }
        return;
    }

    const auto h = actor->GetHandle();

    auto& comboRemain01 = g_hudTLS.comboRemain01;
    auto& comboTintsRGB = g_hudTLS.comboTintsRGB;
    auto& IconNames = g_hudTLS.IconNames;
    comboRemain01.clear();
    comboTintsRGB.clear();
    IconNames.clear();

    const std::size_t n = std::min<std::size_t>(acts.size(), 3);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = acts[i];

        const double remainS = r.realTime ? (r.endRtS - nowRt) : (static_cast<double>(r.endH - nowH) * 3600.0);

        const double denom = std::max(0.001, static_cast<double>(r.durationS));
        const double frac = std::clamp(remainS / denom, 0.0, 1.0);

        comboRemain01.push_back(frac);
        comboTintsRGB.push_back(r.tint);
        IconNames.push_back(r.icon ? r.icon : nullptr);
    }

    auto& accumValues = g_hudTLS.accumValues;
    auto& accumColorsRGB = g_hudTLS.accumColorsRGB;
    accumValues.clear();
    accumColorsRGB.clear();

    if (haveTotals) {
        const auto& b = *bundleOpt;
        if (accumValues.capacity() < b.values.size()) accumValues.reserve(b.values.size());
        if (accumColorsRGB.capacity() < b.colors.size()) accumColorsRGB.reserve(b.colors.size());
        for (auto v : b.values) accumValues.push_back(double(std::clamp<std::uint32_t>(v, 0, 100)));
        accumColorsRGB.assign(b.colors.begin(), b.colors.end());
        for (auto s : b.icons) IconNames.push_back(s);
    }

    w.FollowActorHead(actor);
    const bool isSingle = g_snap.isSingle;
    const bool isHor = w._isPlayerWidget ? g_snap.playerHorizontal : g_snap.npcHorizontal;
    const float space = w._isPlayerWidget ? g_snap.playerSpacing : g_snap.npcSpacing;
    w.SetAll(comboRemain01, comboTintsRGB, accumValues, accumColorsRGB, IconNames, isSingle, isHor, space);
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

    std::scoped_lock lk(g_comboMx);
    g_comboQueue.push_back(std::move(pr));

    if (ERF::GetConfig().hudEnabled.load(std::memory_order_relaxed)) {
        HUD::StartHUDTick();
    }
}

bool InjectHUD::HideFor(RE::FormID id) {
    auto it = widgets.find(id);
    if (it == widgets.end() || !it->second.widget) return false;

    auto& w = *it->second.widget;
    if (!w._view) return false;

    const bool isSingle = g_snap.isSingle;
    const bool isHor = w._isPlayerWidget ? g_snap.playerHorizontal : g_snap.npcHorizontal;
    const float space = w._isPlayerWidget ? g_snap.playerSpacing : g_snap.npcSpacing;
    w.ClearAndHide(isSingle, isHor, space);
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
    HUD::StopHUDTick();
}

void InjectHUD::OnUIFrameBegin(double nowRtS, float nowH) {
    const auto& cfg = ERF::GetConfig();
    g_snap.hudEnabled = cfg.hudEnabled.load(std::memory_order_relaxed);
    g_snap.isSingle = cfg.isSingle.load(std::memory_order_relaxed);
    g_snap.playerHorizontal = cfg.playerHorizontal.load(std::memory_order_relaxed);
    g_snap.npcHorizontal = cfg.npcHorizontal.load(std::memory_order_relaxed);
    g_snap.playerSpacing = cfg.playerSpacing.load(std::memory_order_relaxed);
    g_snap.npcSpacing = cfg.npcSpacing.load(std::memory_order_relaxed);
    g_snap.playerX = cfg.playerXPosition.load(std::memory_order_relaxed);
    g_snap.playerY = cfg.playerYPosition.load(std::memory_order_relaxed);
    g_snap.playerScale = cfg.playerScale.load(std::memory_order_relaxed);
    g_snap.npcX = cfg.npcXPosition.load(std::memory_order_relaxed);
    g_snap.npcY = cfg.npcYPosition.load(std::memory_order_relaxed);
    g_snap.npcScale = cfg.npcScale.load(std::memory_order_relaxed);
    g_snap.nowRtS = nowRtS;
    g_snap.nowH = nowH;
    DrainComboQueueOnUI(nowRtS, nowH);
}

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