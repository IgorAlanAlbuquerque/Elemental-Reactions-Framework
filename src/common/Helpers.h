#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "RE/Skyrim.h"

[[nodiscard]] consteval std::uint32_t FOURCC(char a, char b, char c, char d) {
    return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | std::uint32_t(d);
}

[[nodiscard]] constexpr std::uint8_t clamp100(int v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 100));
}

inline RE::Actor* ResolveTarget(RE::ActiveEffect* ae, RE::MagicTarget* mt) noexcept {
    if constexpr (requires(RE::ActiveEffect* x) { x->GetTargetActor(); }) {
        if (ae) {
            if (auto* a = ae->GetTargetActor()) return a;
        }
    }
    if (mt) {
        if (auto* ref = skyrim_cast<RE::TESObjectREFR*>(mt)) {
            if (auto* a = ref->As<RE::Actor>()) return a;
        }
    }
    return nullptr;
}

inline RE::Actor* AsActor(RE::MagicTarget* mt) noexcept {
    if (!mt) return nullptr;
    if (auto* ref = skyrim_cast<RE::TESObjectREFR*>(mt))
        if (auto* a = ref->As<RE::Actor>()) return a;
    return dynamic_cast<RE::Actor*>(mt);
}

[[nodiscard]] inline float GetHealth(RE::Actor* a) noexcept {
    if (!a) return 0.0f;
    if (auto avo = skyrim_cast<RE::ActorValueOwner*>(a)) {
        return avo->GetActorValue(RE::ActorValue::kHealth);
    }
    return 0.0f;
}

[[nodiscard]] constexpr float safeDiv(float a, float b) noexcept { return (b <= 0.0f) ? 0.0f : (a / b); }
[[nodiscard]] constexpr double safeDiv(double a, double b) noexcept { return (b <= 0.0) ? 0.0 : (a / b); }

[[nodiscard]] constexpr std::uint64_t MakeWidgetID(std::uint32_t formID) { return (std::uint64_t(formID) << 32); }

namespace ERF {

    using _PO3_GetFormEditorID = const char* (*)(std::uint32_t);

    inline std::string GetEditorID_po3(const RE::TESForm* form) {
        if (!form) {
            return {};
        }

        // mesmo padrão que o exemplo da página do Tweaks
        // https://www.nexusmods.com/skyrimspecialedition/mods/51073
        static HMODULE tweaks = ::GetModuleHandleW(L"po3_Tweaks");
        if (!tweaks) {
            return {};
        }

        static auto func = reinterpret_cast<_PO3_GetFormEditorID>(::GetProcAddress(tweaks, "GetFormEditorID"));

        if (!func) {
            return {};
        }

        const char* ed = func(form->formID);
        if (ed && *ed) {
            return std::string(ed);
        }

        return {};
    }

}