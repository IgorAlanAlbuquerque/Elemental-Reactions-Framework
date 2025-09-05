#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

constexpr std::uint32_t FOURCC(char a, char b, char c, char d) noexcept {
    return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | std::uint32_t(d);
}

constexpr std::uint8_t clamp100(int v) noexcept {
    int result;
    if (v < 0) {
        result = 0;
    } else if (v > 100) {
        result = 100;
    } else {
        result = v;
    }
    return static_cast<std::uint8_t>(result);
}

static RE::Actor* ResolveTarget(RE::ActiveEffect* ae, RE::MagicTarget* mt) {
    if constexpr (requires(RE::ActiveEffect* x) { x->GetTargetActor(); }) {
        if (auto* a = ae->GetTargetActor()) return a;
    }
    if (mt) {
        if (auto* ref = skyrim_cast<RE::TESObjectREFR*>(mt)) {
            if (auto* a = ref->As<RE::Actor>()) return a;
        }
    }
    return nullptr;
}

static RE::Actor* AsActor(RE::MagicTarget* mt) {
    if (!mt) return nullptr;

    // NG fornece skyrim_cast<> para atravessar herança múltipla com RTTI
    if (auto a = skyrim_cast<RE::Actor*>(mt)) return a;

    // fallback caso você esteja sem skyrim_cast por algum motivo
    return dynamic_cast<RE::Actor*>(mt);
}

static float GetHealth(RE::Actor* a) {
    if (!a) return 0.0f;
    if (auto avo = skyrim_cast<RE::ActorValueOwner*>(a)) {
        return avo->GetActorValue(RE::ActorValue::kHealth);
    }
    return 0.0f;
}