#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

constexpr std::uint32_t FOURCC(char a, char b, char c, char d) noexcept {
    return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | std::uint32_t(d);
}

namespace ElementalStates {
    enum class Flag : std::uint8_t { Wet = 0, Rubber = 1, Fur = 2, Fat = 3 };

    void RegisterSerialization();

    void Set(const RE::Actor* a, Flag f, bool value);
    bool Get(const RE::Actor* a, Flag f);
    void Clear(const RE::Actor* a);
    void ClearAll();
}

namespace ElementalStatesTest {
    void RunOnce();
}