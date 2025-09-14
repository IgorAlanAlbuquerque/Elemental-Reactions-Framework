#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace ElementalStates {
    enum class Flag : std::uint8_t { Wet = 0, Rubber = 1, Fur = 2 };

    void RegisterStore();

    void Set(RE::Actor* a, Flag f, bool value);
    bool Get(RE::Actor* a, Flag f);
    void Clear(RE::Actor* a);
    void ClearAll();
}
