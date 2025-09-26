#include "ElementalEffects.h"

#include "../hud/InjectHUD.h"
#include "ElementalGauges.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    using Combo = ElementalGauges::Combo;

    struct HudComboParams {
        float seconds;
        bool realTime;
    };
    static HudComboParams gHUD_All{10.0f, true};

    static void FxSoloFire(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] SOLO Fire");
    }
    static void FxSoloFrost(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] SOLO Frost");
    }
    static void FxSoloShock(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] SOLO Shock");
    }

    static void FxPairFireFrost(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair FireFrost");
    }
    static void FxPairFrostFire(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair FrostFire");
    }
    static void FxPairFireShock(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair FireShock");
    }
    static void FxPairShockFire(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair ShockFire");
    }
    static void FxPairFrostShock(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair FrostShock");
    }
    static void FxPairShockFrost(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] Pair ShockFrost");
    }

    static void FxTriple(RE::Actor* a, Combo, void*) {
        if (a) spdlog::info("[ERF] TRIPLE Fire+Frost+Shock");
    }

    static void OnComboHUD(RE::Actor* a, Combo which, void* user) {
        auto* p = static_cast<HudComboParams*>(user);
        const float sec = p ? p->seconds : 10.0f;
        const bool rt = p ? p->realTime : true;
        InjectHUD::BeginCombo(a, which, sec, rt);
    }

    static void OnComboHUDPlusFx(RE::Actor* a, Combo c, void* user) {
        OnComboHUD(a, c, user);

        switch (c) {
            case Combo::Fire:
                FxSoloFire(a, c, nullptr);
                break;
            case Combo::Frost:
                FxSoloFrost(a, c, nullptr);
                break;
            case Combo::Shock:
                FxSoloShock(a, c, nullptr);
                break;

            case Combo::FireFrost:
                FxPairFireFrost(a, c, nullptr);
                break;
            case Combo::FrostFire:
                FxPairFrostFire(a, c, nullptr);
                break;
            case Combo::FireShock:
                FxPairFireShock(a, c, nullptr);
                break;
            case Combo::ShockFire:
                FxPairShockFire(a, c, nullptr);
                break;
            case Combo::FrostShock:
                FxPairFrostShock(a, c, nullptr);
                break;
            case Combo::ShockFrost:
                FxPairShockFrost(a, c, nullptr);
                break;

            case Combo::FireFrostShock:
                FxTriple(a, c, nullptr);
                break;
            default:
                break;
        }
    }
}

void ElementalEffects::ConfigurarGatilhos() {
    using ElementalGauges::SetOnSumCombo;
    using ElementalGauges::SumComboTrigger;

    SumComboTrigger cx{};
    cx.cb = &OnComboHUDPlusFx;
    cx.user = &gHUD_All;
    cx.deferToTask = true;
    cx.clearAllOnTrigger = true;

    cx.cooldownSeconds = 0.5f;
    cx.cooldownIsRealTime = true;

    cx.elementLockoutSeconds = 10.0f;
    cx.elementLockoutIsRealTime = true;

    cx.majorityPct = 0.85f;
    cx.tripleMinPct = 0.28f;

    for (int i = 0; i < static_cast<int>(Combo::_COUNT); ++i) {
        SetOnSumCombo(static_cast<Combo>(i), cx);
    }

    spdlog::info(
        "[ERF] Combos registrados p/ HUD+lockout (cooldown=0.5s RT; lockout=10s RT; majority=85%; tripleMin=28%).");
}