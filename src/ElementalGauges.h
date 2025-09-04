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

namespace ElementalGaugesTest {
    void RunOnce();
}
