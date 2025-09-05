#pragma once

#include <cstdint>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace ElementalGauges {
    enum class Type : std::uint8_t { Fire = 0, Frost = 1, Shock = 2 };

    void RegisterStore();

    std::uint8_t Get(const RE::Actor* a, Type t);
    void Set(const RE::Actor* a, Type t, std::uint8_t value);  // clamp 0..100
    void Add(const RE::Actor* a, Type t, int delta);           // saturating add
    void Clear(const RE::Actor* a);
    void ClearAll();
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
