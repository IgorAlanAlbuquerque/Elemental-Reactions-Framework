#include "InjectHUD.h"

#include <memory>
#include <string_view>

#include "../elemental_reactions/ElementalGauges.h"
#include "Offsets.h"

namespace {
    using namespace InjectHUD;
    static RE::NiPoint3 GetStableAnchorWorldPos(RE::Actor* actor) {
        RE::NiPoint3 world = actor->GetPosition();
        if (auto* root = actor->Get3D(true)) {
            if (auto* com = root->GetObjectByName("NPC COM [COM ]"sv))
                world = com->world.translate;
            else if (auto* sp2 = root->GetObjectByName("NPC Spine2 [Spn2]"sv))
                world = sp2->world.translate;
            else if (auto* head = root->GetObjectByName("NPC Head [Head]"sv))
                world = head->world.translate;
            else {
                world.z += actor->GetHeight() * 0.9f;
            }
        }
        return world;
    }

    inline void WorldToScreen01(const RE::NiPoint3& world, float& nx, float& ny, float& depth) {
        RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, world, nx, ny, depth, 1e-5f);
    }

    inline double NowRtS() {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        return std::chrono::duration<double>(clock::now() - t0).count();
    }
    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    // mapeia Combo -> id e tint do HUD
    inline int IconIdFor(ElementalGauges::Combo c) {
        using enum ElementalGauges::Combo;
        switch (c) {
            case Fire:
                return 0;
            case Frost:
                return 1;
            case Shock:
                return 2;
            case FireFrost:
            case FrostFire:
                return 3;
            case FireShock:
            case ShockFire:
                return 4;
            case FrostShock:
            case ShockFrost:
                return 5;
            case FireFrostShock:
                return 6;
            default:
                return 0;
        }
    }
    inline std::uint32_t TintFor(ElementalGauges::Combo c) {
        using enum ElementalGauges::Combo;
        switch (c) {
            case Fire:
                return 0xF04A3A;
            case Frost:
                return 0x4FB2FF;
            case Shock:
                return 0xFFD02A;
            case FireFrost:
                return 0xE65ACF;
            case FrostFire:
                return 0x7A73FF;
            case FireShock:
                return 0xFF8A2A;
            case ShockFire:
                return 0xF6B22E;
            case FrostShock:
                return 0x49C9F0;
            case ShockFrost:
                return 0xB8E34D;
            case FireFrostShock:
                return 0xFFF0CC;
            default:
                return 0xFFFFFF;
        }
    }

    static std::vector<ComboHUD> GetActiveCombos(RE::FormID id) {
        std::vector<ComboHUD> out;
        auto it = combos.find(id);
        if (it == combos.end()) return out;
        const double nowRt = NowRtS();
        const float nowH = NowHours();
        for (auto& c : it->second) {
            const bool alive = c.realTime ? (nowRt < c.endRtS) : (nowH < c.endH);
            if (alive) out.push_back(c);
        }
        // ordene por “restante” desc (opcional)
        std::sort(out.begin(), out.end(), [&](const ComboHUD& a, const ComboHUD& b) {
            const double ra = a.realTime ? (a.endRtS - nowRt) : (double(a.endH - nowH) * 3600.0);
            const double rb = b.realTime ? (b.endRtS - nowRt) : (double(b.endH - nowH) * 3600.0);
            return ra > rb;
        });
        if (out.size() > 2) out.resize(2);
        return out;
    }
}

namespace InjectHUD {
    TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
    SKSE::PluginHandle g_pluginHandle = static_cast<SKSE::PluginHandle>(-1);
    std::unordered_map<RE::FormID, std::vector<std::shared_ptr<SMSOWidget>>> widgets{};
    std::unordered_map<RE::FormID, std::vector<ComboHUD>> combos;
}

void InjectHUD::SMSOWidget::FollowActorHead(RE::Actor* actor) {
    if (!actor || !_view) return;

    const RE::NiPoint3 anchor = GetStableAnchorWorldPos(actor);

    float nx1 = 0, ny1 = 0, d1 = 0;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, anchor, nx1, ny1, d1, 1e-5f);
    if (d1 <= 0.f || nx1 < 0.f || nx1 > 1.f || ny1 < 0.f || ny1 > 1.f) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    RE::NiPoint3 up = anchor;
    up.z += 100.f;
    float nx2 = 0, ny2 = 0, d2 = 0;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, up, nx2, ny2, d2, 1e-5f);

    RE::GRectF rect = _view->GetVisibleFrameRect();
    const float stageW = rect.right - rect.left;
    const float stageH = rect.bottom - rect.top;

    const float sx = rect.left + stageW * nx1;
    const float sy = rect.top + stageH * (1.f - ny1);
    const float sy2 = rect.top + stageH * (1.f - ny2);

    const float pxPer100u = std::fabs(sy2 - sy);
    constexpr float kRefPxPer100u = 60.f;
    float scale = pxPer100u > 1e-3f ? (pxPer100u / kRefPxPer100u) : 1.f;
    scale = std::clamp(scale, 0.5f, 1.2f);

    constexpr float kOffsetX_px = -76.f;
    const bool isFP = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
    constexpr float kUpWU_TP = 80.f, kUpWU_FP = 88.f;
    const float pxPerU = pxPer100u / 100.f;
    const float upWU = isFP ? kUpWU_FP : kUpWU_TP;

    constexpr float kCompX_perScale = -6.f;
    constexpr float kCompY_perScale = -64.f;

    double targetX = sx + kOffsetX_px + kCompX_perScale * (scale - 1.0);
    double targetY = sy - (upWU * pxPerU) + kCompY_perScale * (scale - 1.0);

    if (std::isnan(_lastX)) _lastX = targetX;
    if (std::isnan(_lastY)) _lastY = targetY;

    // suavização + clamp vertical
    auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    const double smooth = 0.35;  // 0..1 (maior = acompanha mais)
    const double baseMax = 3.0;  // clamp mínimo por frame (px)
    const double fastPx = 12.0;  // acima disso, considere “movimento rápido”
    const double snapPx = 24.0;  // acima disso, faça SNAP (sem lerp)
    const double deadpx = 2.0;   // deadzone p/ jitter

    const double dx = targetX - _lastX;
    const double dy = targetY - _lastY;
    const double dist = std::hypot(dx, dy);

    if (!(dist >= snapPx)) {
        // 2) clamp elástico: quanto maior a distância, maior o passo permitido
        const double k = std::clamp(dist / fastPx, 0.0, 1.0);  // 0..1
        const double maxDX = baseMax + k * 12.0;               // até ~15 px/frame
        const double maxDY = baseMax + k * 12.0;

        // 3) aplicar deadzone e lerp
        double candX = lerp(_lastX, targetX, smooth);
        if (std::fabs(candX - _lastX) < deadpx) candX = _lastX;
        candX = std::clamp(candX, _lastX - maxDX, _lastX + maxDX);

        double candY = lerp(_lastY, targetY, smooth);
        if (std::fabs(candY - _lastY) < deadpx) candY = _lastY;
        candY = std::clamp(candY, _lastY - maxDY, _lastY + maxDY);

        targetX = candX;
        targetY = candY;
    }

    targetX += _slot * _slotSpacingPx;
    RE::GFxValue vx, vy, s, vis;
    vx.SetNumber(targetX);
    vy.SetNumber(targetY);
    s.SetNumber(100.0 * scale);
    vis.SetBoolean(true);
    _object.SetMember("_x", vx);
    _object.SetMember("_y", vy);
    _object.SetMember("_xscale", s);
    _object.SetMember("_yscale", s);
    _object.SetMember("_visible", vis);

    _lastX = targetX;
    _lastY = targetY;
}

void InjectHUD::SMSOWidget::SetIconAndGauge(uint32_t iconId, uint32_t fire, uint32_t frost, uint32_t shock,
                                            uint32_t tintRGB) {
    spdlog::info("[SMSO] SetIconAndGauge(icon={}, F={}, I={}, S={})", iconId, fire, frost, shock);
    if (!_view) return;

    if (RE::GFxValue ready; !_object.Invoke("isReady", &ready, nullptr, 0) || !ready.GetBool()) {
        return;
    }

    const bool any = (fire + frost + shock) > 0;

    RE::GFxValue args[3];
    args[0].SetNumber(iconId);
    args[1].SetNumber(tintRGB);
    args[2].SetNumber(1);
    RE::GFxValue ret;
    _object.Invoke("setIcon", &ret, args, 3);
    spdlog::info("setIcon retBool?{}", ret.IsBool() ? ret.GetBool() : -1);

    double now = NowRtS();
    double dt = 1.0 / 60.0;
    if (!std::isnan(_lastGaugeRtS)) dt = std::clamp(now - _lastGaugeRtS, 1.0 / 240.0, 0.1);
    _lastGaugeRtS = now;

    auto smoothRiseOnly = [&](double& cur, double tgt, double dt) {
        if (tgt <= cur) {
            cur = tgt;
            return;
        }
        const double maxStep = _risePerSec * dt;
        const double d = tgt - cur;
        cur += (d <= maxStep) ? d : maxStep;
        if (cur > 100.0) cur = 100.0;
        if (cur < 0.0) cur = 0.0;
    };

    auto seed = _hadContent ? static_cast<double>(fire) : 0.0;
    if (!_fireDisp.init) {
        _fireDisp.v = seed;
        _fireDisp.init = true;
    }
    seed = _hadContent ? static_cast<double>(frost) : 0.0;
    if (!_frostDisp.init) {
        _frostDisp.v = seed;
        _frostDisp.init = true;
    }
    seed = _hadContent ? static_cast<double>(shock) : 0.0;
    if (!_shockDisp.init) {
        _shockDisp.v = seed;
        _shockDisp.init = true;
    }

    smoothRiseOnly(_fireDisp.v, static_cast<double>(fire), dt);
    smoothRiseOnly(_frostDisp.v, static_cast<double>(frost), dt);
    smoothRiseOnly(_shockDisp.v, static_cast<double>(shock), dt);

    RE::GFxValue acc[3];
    acc[0].SetNumber(_fireDisp.v);
    acc[1].SetNumber(_frostDisp.v);
    acc[2].SetNumber(_shockDisp.v);
    _object.Invoke("setAccumulators", nullptr, acc, 3);

    RE::GFxValue vis;
    vis.SetBoolean(any);
    _object.SetMember("_visible", vis);
    if (!any) {
        _fireDisp.v = 0.0;
        _fireDisp.init = true;
        _frostDisp.v = 0.0;
        _frostDisp.init = true;
        _shockDisp.v = 0.0;
        _shockDisp.init = true;
        _hadContent = false;
        ResetSmoothing();
        return;
    }

    _hadContent = true;
}

void InjectHUD::AddFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) return;

    const auto id = actor->GetFormID();
    auto& vec = widgets[id];

    if (!vec.empty()) return;

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true)) {
        return;
    }

    auto w0 = std::make_shared<SMSOWidget>(0);
    g_trueHUD->AddWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id ^ (0u << 28), SMSO_SYMBOL_NAME, w0);
    vec.push_back(std::move(w0));
    spdlog::info("[SMSO] AddWidget: actor ID {:08X}", id);
}

void InjectHUD::UpdateFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) return;
    const auto id = actor->GetFormID();

    auto it = widgets.find(id);
    if (it == widgets.end()) return;
    auto& list = it->second;
    if (list.empty()) return;

    auto actives = GetActiveCombos(id);
    bool haveTotals = false;
    ElementalGauges::Totals totals{};
    if (auto totalsOpt = ElementalGauges::GetTotalsDecayed(id)) {
        totals = *totalsOpt;
        haveTotals = (totals.fire + totals.frost + totals.shock) > 0;
    }

    int needed = static_cast<int>(actives.size()) + (haveTotals ? 1 : 0);
    needed = std::min(needed, 3);
    if (needed == 0) {
        for (auto& w : list) {
            if (w && w->_view) {
                RE::GFxValue vis;
                vis.SetBoolean(false);
                w->_object.SetMember("_visible", vis);
            }
        }
        return;
    }

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true)) return;

    while ((int)list.size() < needed && (int)list.size() < 3) {
        int slot = (int)list.size();
        auto w = std::make_shared<SMSOWidget>(slot);
        g_trueHUD->AddWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id ^ (slot << 28), SMSO_SYMBOL_NAME, w);
        list.push_back(std::move(w));
        spdlog::info("[SMSO] AddWidget slot{}: actor {:08X}", slot, id);
    }
    while ((int)list.size() > needed) {
        int slot = (int)list.size() - 1;
        g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id ^ (slot << 28),
                                TRUEHUD_API::WidgetRemovalMode::Immediate);
        list.pop_back();
        spdlog::info("[SMSO] RemoveWidget slot{}: actor {:08X}", slot, id);
    }

    int s = 0;
    // 3) preencher slots: combos primeiro, depois acumulador
    // combos
    for (int i = 0; i < (int)actives.size(); ++i, ++s) {
        auto& w = *list[s];
        if (!w._view) continue;
        w._slot = s;
        w.FollowActorHead(actor);

        const auto& c = actives[i];
        const double remain = c.realTime ? (c.endRtS - NowRtS()) : (double(c.endH - NowHours()) * 3600.0);
        const double frac = std::clamp(remain / std::max(0.001, (double)c.durationS), 0.0, 1.0);
        w.SetCombo(c.iconId, (float)frac, c.tint);
    }

    // acumulador (se houver)
    if (haveTotals && s < (int)list.size()) {
        auto& w = *list[s];
        if (w._view) {
            w._slot = s;
            w.FollowActorHead(actor);
            if (auto iconOpt = ElementalGauges::PickHudIconDecayed(id)) {
                const auto& icon = *iconOpt;
                w.SetIconAndGauge(icon.id, totals.fire, totals.frost, totals.shock, icon.tintRGB);
            } else {
                RE::GFxValue vis;
                vis.SetBoolean(false);
                w._object.SetMember("_visible", vis);
            }
        }
    }
}

void InjectHUD::BeginCombo(RE::Actor* a, ElementalGauges::Combo which, float seconds, bool realTime) {
    if (!a || seconds <= 0.f) return;
    const auto id = a->GetFormID();

    auto post = [=]() {
        AddFor(a);
        ComboHUD ch{};
        ch.which = which;
        ch.realTime = realTime;
        ch.durationS = seconds;
        ch.iconId = IconIdFor(which);
        ch.tint = TintFor(which);
        if (realTime) {
            ch.endRtS = NowRtS() + seconds;
        } else {
            ch.endH = NowHours() + (seconds / 3600.0f);
        }
        auto& vec = combos[id];
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&](const ComboHUD& c) {
                                     const bool expired = c.realTime ? (NowRtS() >= c.endRtS) : (NowHours() >= c.endH);
                                     return expired;
                                 }),
                  vec.end());
        vec.push_back(ch);
    };

    if (auto* ti = SKSE::GetTaskInterface()) {
        ti->AddTask(post);
    } else {
        post();
    }
}

bool InjectHUD::RemoveFor(RE::FormID id) {
    if (!g_trueHUD || !id) return false;
    auto it = widgets.find(id);
    if (it == widgets.end()) return false;

    // remova todos os slots existentes
    for (int slot = 0; slot < (int)it->second.size(); ++slot) {
        g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id ^ (slot << 28),
                                TRUEHUD_API::WidgetRemovalMode::Immediate);
    }
    widgets.erase(it);
    combos.erase(id);
    return true;
}

void InjectHUD::SMSOWidget::SetCombo(int iconId, float remaining01, std::uint32_t tintRGB) {
    if (!_view) return;

    RE::GFxValue ready;
    if (!_object.Invoke("isReady", &ready, nullptr, 0) || !ready.GetBool()) return;

    {
        RE::GFxValue args[3], ret;
        args[0].SetNumber(iconId);
        args[1].SetNumber(tintRGB);
        args[2].SetNumber(1);
        _object.Invoke("setIcon", &ret, args, 3);
    }

    {
        RE::GFxValue args[2];
        const double f = std::clamp<double>(remaining01, 0.0, 1.0);
        args[0].SetNumber(f);
        args[1].SetNumber(tintRGB);
        _object.Invoke("setComboFill", nullptr, args, 2);
    }

    {
        RE::GFxValue vis;
        vis.SetBoolean(true);
        _object.SetMember("_visible", vis);
    }
}

void InjectHUD::RemoveAllWidgets() {
    if (!g_trueHUD) {
        widgets.clear();
        return;
    }
    for (auto& [id, vec] : widgets) {
        for (int slot = 0; slot < (int)vec.size(); ++slot) {
            g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id ^ (slot << 28),
                                    TRUEHUD_API::WidgetRemovalMode::Immediate);
        }
    }
    widgets.clear();
}

void InjectHUD::OnTrueHUDClose() {
    RemoveAllWidgets();
    combos.clear();
    HUD::ResetTracking();
}
