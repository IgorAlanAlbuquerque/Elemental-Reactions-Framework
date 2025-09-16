#include "InjectHUD.h"

#include <memory>
#include <string_view>

#include "../elemental_reactions/ElementalGauges.h"
#include "Offsets.h"

namespace {
    RE::NiPoint3 GetActorHeadWorldPos(RE::Actor* actor) {
        RE::NiPoint3 world = actor->GetPosition();
        if (auto* root = actor->Get3D(true)) {
            if (auto* head = root->GetObjectByName("NPC Head [Head]"sv)) {
                world = head->world.translate;
            } else {
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
                return 0xB671FF;
            case FireFrost:
                return 0xE04E9B;
            case FrostFire:
                return 0x6A7DFF;
            case FireShock:
                return 0xD955C3;
            case ShockFire:
                return 0xBF4EEA;
            case FrostShock:
                return 0x6F84FF;
            case ShockFrost:
                return 0x9F7BFF;
            case FireFrostShock:
                return 0xD986FF;
            default:
                return 0xFFFFFF;
        }
    }
}

namespace InjectHUD {
    TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
    SKSE::PluginHandle g_pluginHandle = static_cast<SKSE::PluginHandle>(-1);
    std::unordered_map<RE::FormID, std::shared_ptr<SMSOWidget>> widgets{};
    std::unordered_map<RE::FormID, ComboHUD> combos;
}

void InjectHUD::SMSOWidget::FollowActorHead(RE::Actor* actor) {
    if (!actor || !_view) return;

    const RE::NiPoint3 head = GetActorHeadWorldPos(actor);

    float nx1 = 0.f, ny1 = 0.f, d1 = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, head, nx1, ny1, d1, 1e-5f);
    if (d1 <= 0.f || nx1 < 0.f || nx1 > 1.f || ny1 < 0.f || ny1 > 1.f) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    RE::NiPoint3 headUp = head;
    headUp.z += 100.f;
    float nx2 = 0.f, ny2 = 0.f, d2 = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, headUp, nx2, ny2, d2, 1e-5f);

    RE::GRectF rect = _view->GetVisibleFrameRect();
    const float stageW = rect.right - rect.left;
    const float stageH = rect.bottom - rect.top;

    const float sx = rect.left + stageW * nx1;
    const float sy = rect.top + stageH * (1.f - ny1);
    const float sy2 = rect.top + stageH * (1.f - ny2);

    const float pxPer100u = std::fabs(sy2 - sy);
    constexpr float kRefPxPer100u = 60.0f;
    float scale = pxPer100u > 1e-3f ? (pxPer100u / kRefPxPer100u) : 1.0f;
    scale = std::clamp(scale, 0.5f, 1.2f);

    constexpr float kOffsetX_px = -48.0f;

    const bool isFP = RE::PlayerCamera::GetSingleton() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
    constexpr float kUpWU_TP = 30.0f;
    constexpr float kUpWU_FP = 34.0f;
    const float pxPerU = pxPer100u / 100.0f;
    const float upWU = isFP ? kUpWU_FP : kUpWU_TP;

    constexpr float kCompX_perScale = -6.0f;
    constexpr float kCompY_perScale = -30.0f;

    const float x = sx + kOffsetX_px + kCompX_perScale * (scale - 1.0f);
    const float y = sy - (upWU * pxPerU) + kCompY_perScale * (scale - 1.0f);

    RE::GFxValue vx, vy, s, vis;
    vx.SetNumber(x);
    vy.SetNumber(y);
    s.SetNumber(100.0f * scale);
    vis.SetBoolean(true);

    _object.SetMember("_x", vx);
    _object.SetMember("_y", vy);
    _object.SetMember("_xscale", s);
    _object.SetMember("_yscale", s);
    _object.SetMember("_visible", vis);
}

void InjectHUD::SMSOWidget::SetIconAndGauge(uint32_t iconId, uint32_t fire, uint32_t frost, uint32_t shock,
                                            uint32_t tintRGB) {
    spdlog::info("[SMSO] SetIconAndGauge(icon={}, F={}, R={}, S={})", iconId, fire, frost, shock);
    if (!_view) return;

    if (RE::GFxValue ready; !_object.Invoke("isReady", &ready, nullptr, 0) || !ready.GetBool()) {
        return;
    }

    const bool any = (fire + frost + shock) > 0;

    RE::GFxValue vis;
    vis.SetBoolean(any);
    _object.SetMember("_visible", vis);

    RE::GFxValue acc[3];
    acc[0].SetNumber(static_cast<double>(fire));
    acc[1].SetNumber(static_cast<double>(frost));
    acc[2].SetNumber(static_cast<double>(shock));
    _object.Invoke("setAccumulators", nullptr, acc, 3);

    if (!any) return;

    spdlog::info("ativar o icone");
    RE::GFxValue args[2];
    args[0].SetNumber(iconId);
    args[1].SetNumber(tintRGB);
    RE::GFxValue ret;
    bool ok2 = _object.Invoke("setIcon", &ret, args, 2);
    spdlog::info("setIcon invoke={} retBool?{}", ok2, ret.IsBool() ? ret.GetBool() : -1);
}

void InjectHUD::AddFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) return;

    const auto id = actor->GetFormID();
    if (widgets.contains(id)) return;

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true)) {
        return;
    }

    auto w = std::make_shared<SMSOWidget>();
    g_trueHUD->AddWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id, SMSO_SYMBOL_NAME, w);
    widgets.emplace(id, std::move(w));
    spdlog::info("[SMSO] AddWidget: actor ID {:08X}", id);
}

void InjectHUD::UpdateFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) return;
    const auto id = actor->GetFormID();

    auto it = widgets.find(id);
    if (it == widgets.end() || !it->second || !it->second->_view) return;

    // 1) Combo ativo?
    if (auto cIt = combos.find(id); cIt != combos.end()) {
        const ComboHUD& ch = cIt->second;
        double remain01 = 0.0;
        if (ch.realTime) {
            remain01 = (ch.endRtS - NowRtS()) / std::max(0.001, double(ch.durationS));
        } else {
            const double durH = ch.durationS / 3600.0;
            remain01 = (ch.endH - NowHours()) / std::max(1e-6, durH);
        }
        if (remain01 <= 0.0) {
            combos.erase(cIt);
        } else {
            it->second->FollowActorHead(actor);
            it->second->SetCombo(ch.iconId, float(std::min(1.0, remain01)), ch.tint);
            return;
        }
    }

    if (auto iconOpt = ElementalGauges::PickHudIconDecayed(id)) {
        if (auto totalsOpt = ElementalGauges::GetTotalsDecayed(id)) {
            auto& w = *it->second;
            w.FollowActorHead(actor);
            const auto& icon = *iconOpt;
            const auto& t = *totalsOpt;
            w.SetIconAndGauge(icon.id, t.fire, t.frost, t.shock, icon.tintRGB);
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
        combos[id] = ch;
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

    g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id, TRUEHUD_API::WidgetRemovalMode::Immediate);
    widgets.erase(it);
    combos.erase(id);
    return true;
}

void InjectHUD::SMSOWidget::SetCombo(int iconId, float remaining01, std::uint32_t tintRGB) {
    if (!_view) return;

    RE::GFxValue ready;
    if (!_object.Invoke("isReady", &ready, nullptr, 0) || !ready.GetBool()) return;

    {
        RE::GFxValue vis;
        vis.SetBoolean(true);
        _object.SetMember("_visible", vis);
    }

    {
        RE::GFxValue args[2], ret;
        args[0].SetNumber(iconId);
        args[1].SetNumber(tintRGB);
        _object.Invoke("setIcon", &ret, args, 2);
    }

    {
        RE::GFxValue args[2], ret;
        const double f = std::clamp<double>(remaining01, 0.0, 1.0);
        args[0].SetNumber(f);
        args[1].SetNumber(tintRGB);
        _object.Invoke("setComboFill", &ret, args, 2);
    }
}

void InjectHUD::RemoveAllWidgets() {
    if (!g_trueHUD) {
        widgets.clear();
        return;
    }

    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        const RE::FormID id = it->first;
        g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id, TRUEHUD_API::WidgetRemovalMode::Immediate);
    }
    widgets.clear();
}

void InjectHUD::OnTrueHUDClose() {
    spdlog::info("[SMSO] OnTrueHUDClose -> limpando widgets");
    RemoveAllWidgets();
    combos.clear();
    HUD::ResetTracking();
}

void InjectHUD::OnTrueHUDOpen() { spdlog::info("[SMSO] OnTrueHUDOpen"); }
