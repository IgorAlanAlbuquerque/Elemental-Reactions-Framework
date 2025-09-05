#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace ElementalStates {
    enum class Flag : std::uint8_t { Wet = 0, Rubber = 1, Fur = 2, Fat = 3 };

    void RegisterStore();

    void Set(const RE::Actor* a, Flag f, bool value);
    bool Get(const RE::Actor* a, Flag f);
    void Clear(const RE::Actor* a);
    void ClearAll();
}
