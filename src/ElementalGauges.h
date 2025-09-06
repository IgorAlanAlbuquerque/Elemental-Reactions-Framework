#pragma once

#include <cstdint>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace ElementalGauges {

    enum class Type : std::uint8_t { Fire = 0, Frost = 1, Shock = 2 };

    enum class Combo : std::uint8_t {
        Fire = 0,
        Frost,
        Shock,
        FireFrost,
        FrostFire,
        FireShock,
        ShockFire,
        FrostShock,
        ShockFrost,
        FireFrostShock,
        _COUNT
    };

    struct SumComboTrigger {
        using Callback = void (*)(RE::Actor* actor, Combo which, void* user);
        Callback cb{nullptr};
        void* user{nullptr};

        // Regras
        float majorityPct{0.85f};
        float tripleMinPct{0.28f};

        // Execução / antispam
        float cooldownSeconds{0.5f};
        bool cooldownIsRealTime{true};
        bool deferToTask{true};
        bool clearAllOnTrigger{true};

        // NOVO: lockout para ACUMULAR GAUGE dos elementos envolvidos no combo
        float elementLockoutSeconds{0.0f};    // 0 = sem lockout de acumulo
        bool elementLockoutIsRealTime{true};  // true = segundos reais; false = tempo de jogo
    };

    void SetOnSumCombo(Combo c, const SumComboTrigger& cfg);

    std::uint8_t Get(RE::Actor* a, Type t);
    void Set(RE::Actor* a, Type t, std::uint8_t value);
    void Add(RE::Actor* a, Type t, int delta);
    void Clear(RE::Actor* a);

    void RegisterStore();
}

namespace ElementalGaugesDecay {
    inline constexpr float kGraceSec = 4.0f;

    inline constexpr float kRealDecayPerSec = 15.0f;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    inline float Timescale() {
        float ts = 20.0f;
        if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
            if (auto* s = gs->GetSetting("fTimescale")) {  // NOSONAR: No const assinatura
                ts = s->GetFloat();
            }
        }
        if (ts < 0.001f) ts = 0.001f;
        return ts;
    }

    inline float DecayPerGameHour() { return kRealDecayPerSec * 3600.0f / Timescale(); }

    inline float GraceGameHours() { return (kGraceSec * Timescale()) / 3600.0f; }
}
