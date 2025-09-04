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
    // 5 segundos de “graça” antes de começar a decair (em horas de jogo)
    constexpr float kGraceSec = 3.0f;
    constexpr float kGraceHours = kGraceSec / 3600.0f;

    // taxa de decaimento (unidades por segundo de jogo)
    constexpr float kDecayPerSec = 15.0f;
    constexpr float kDecayPerHour = kDecayPerSec * 3600.0f;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }
}

namespace ElementalGaugesTest {
    void RunOnce();
}
