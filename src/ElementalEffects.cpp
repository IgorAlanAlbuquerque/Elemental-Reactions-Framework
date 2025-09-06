#include "ElementalEffects.h"

namespace ElementalEffects {
    using Combo = ElementalGauges::Combo;

    // --- SOLO ---
    static void FxSoloFire(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] SOLO Fire");
    }
    static void FxSoloFrost(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] SOLO Frost");
    }
    static void FxSoloShock(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] SOLO Shock");
    }

    // --- PARES DIRECIONAIS ---
    static void FxPairFireFrost(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair FireFrost");
    }

    static void FxPairFrostFire(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair FrostFire");
    }

    static void FxPairFireShock(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair FireShock");
    }

    static void FxPairShockFire(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair ShockFire");
    }

    static void FxPairFrostShock(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair FrostShock");
    }

    static void FxPairShockFrost(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] Pair ShockFrost");
    }

    // --- TRIPLO ---
    static void FxTriple(RE::Actor* a, Combo, void*) {
        if (!a) return;
        spdlog::info("[SMSO] TRIPLE Fire+Frost+Shock");
    }

    void ConfigurarGatilhos() {
        using ElementalGauges::SetOnSumCombo;
        using ElementalGauges::SumComboTrigger;

        // Thresholds das TUAS regras
        SumComboTrigger cx{};
        cx.majorityPct = 0.85f;     // solo se ≥85%
        cx.tripleMinPct = 0.28f;    // triplo se min ≥28% e 3 presentes
        cx.cooldownSeconds = 0.5f;  // anti-spam
        cx.cooldownIsRealTime = true;
        cx.clearAllOnTrigger = true;         // zera F/Fr/S ao disparar
        cx.deferToTask = true;               // roda fora do hook crítico
        cx.elementLockoutSeconds = 2.0f;     // 2s sem acumular os elementos envolvidos
        cx.elementLockoutIsRealTime = true;  // em tempo real

        // --- SOLO ---
        cx.cb = &FxSoloFire;
        SetOnSumCombo(Combo::Fire, cx);
        cx.cb = &FxSoloFrost;
        SetOnSumCombo(Combo::Frost, cx);
        cx.cb = &FxSoloShock;
        SetOnSumCombo(Combo::Shock, cx);

        // --- PARES DIRECIONAIS ---
        cx.cb = &FxPairFireFrost;
        SetOnSumCombo(Combo::FireFrost, cx);
        cx.cb = &FxPairFrostFire;
        SetOnSumCombo(Combo::FrostFire, cx);

        cx.cb = &FxPairFireShock;
        SetOnSumCombo(Combo::FireShock, cx);
        cx.cb = &FxPairShockFire;
        SetOnSumCombo(Combo::ShockFire, cx);

        cx.cb = &FxPairFrostShock;
        SetOnSumCombo(Combo::FrostShock, cx);
        cx.cb = &FxPairShockFrost;
        SetOnSumCombo(Combo::ShockFrost, cx);

        // --- TRIPLO ---
        cx.cb = &FxTriple;
        SetOnSumCombo(Combo::FireFrostShock, cx);

        spdlog::info("[SMSO] Combos registrados (majority=85%%, tripleMin=28%%).");
    }
}