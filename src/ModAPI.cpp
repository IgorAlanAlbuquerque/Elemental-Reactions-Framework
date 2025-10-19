#include "ModAPI.h"

#include "ElementalReactionsAPI.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "elemental_reactions/ElementalGauges.h"
#include "elemental_reactions/ElementalStates.h"
#include "elemental_reactions/erf_element.h"
#include "elemental_reactions/erf_preeffect.h"
#include "elemental_reactions/erf_reaction.h"
#include "elemental_reactions/erf_state.h"
#include "spdlog/spdlog.h"

namespace {
    std::atomic<int> g_reg_barrier{0};
    std::atomic<bool> g_reg_open{false};
    std::atomic<bool> g_frozen{false};
    std::atomic<std::uint32_t> g_timeout_ms{8000};
    void FreezeRegistriesOnce() {
        if (g_frozen.exchange(true)) return;
        ElementRegistry::get().freeze();
        StateRegistry::get().freeze();
        ReactionRegistry::get().freeze();
        ElementalGauges::BuildColorLUTOnce();
        spdlog::info("[ERF] Registries selados.");
    }
}

static ERF_ElementHandle API_RegisterElement(const ERF_ElementDesc_Public& d) noexcept {
    ERF_ElementDesc in{};
    in.name = d.name;
    in.colorRGB = d.colorRGB;
    in.keyword = d.keywordID ? RE::TESForm::LookupByID<RE::BGSKeyword>(d.keywordID) : nullptr;
    return ElementRegistry::get().registerElement(in);
}

static ERF_ReactionHandle API_RegisterReaction(const ERF_ReactionDesc_Public& d) noexcept {
    ERF_ReactionDesc in{};
    in.name = d.name ? d.name : "";

    if (d.elements && d.elementCount > 0) {
        in.elements.assign(d.elements, d.elements + d.elementCount);
    }

    in.ordered = d.ordered;
    in.minPctEach = d.minPctEach;
    in.minSumSelected = d.minSumSelected;
    in.cooldownSeconds = d.cooldownSeconds;
    in.cooldownIsRealTime = d.cooldownIsRealTime;
    in.elementLockoutSeconds = d.elementLockoutSeconds;
    in.elementLockoutIsRealTime = d.elementLockoutIsRealTime;
    in.clearAllOnTrigger = d.clearAllOnTrigger;

    if (d.hudIconPath && *d.hudIconPath) {
        in.hud.iconPath = d.hudIconPath;
        in.hud.iconTint = d.hudIconTint ? d.hudIconTint : 0xFFFFFF;
    }

    in.cb = d.cb;
    in.user = d.user;

    return ReactionRegistry::get().registerReaction(in);
}

static ERF_PreEffectHandle API_RegisterPreEffect(const ERF_PreEffectDesc_Public& d) noexcept {
    ERF_PreEffectDesc in{};
    in.name = d.name ? d.name : "";
    in.element = d.element;
    in.minGauge = d.minGauge;
    in.baseIntensity = d.baseIntensity;
    in.scalePerPoint = d.scalePerPoint;
    in.minIntensity = d.minIntensity;
    in.maxIntensity = d.maxIntensity;
    in.durationSeconds = d.durationSeconds;
    in.durationIsRealTime = d.durationIsRealTime;
    in.cooldownSeconds = d.cooldownSeconds;
    in.cooldownIsRealTime = d.cooldownIsRealTime;

    in.cb = d.cb;
    in.user = d.user;

    return PreEffectRegistry::get().registerPreEffect(in);
}

static ERF_StateHandle API_RegisterState(const ERF_StateDesc_Public& d) noexcept {
    ERF_StateDesc in{};
    in.name = d.name;
    in.keyword = d.keywordID ? RE::TESForm::LookupByID<RE::BGSKeyword>(d.keywordID) : nullptr;
    return StateRegistry::get().registerState(in);
}

static void API_SetElementStateMultiplier(ERF_ElementHandle elem, ERF_StateHandle state, double mult) noexcept {
    if (auto* e = ElementRegistry::get().get(elem)) {
        const_cast<ERF_ElementDesc*>(e)->setMultiplierForState(state, mult);
    }
}

static bool API_ActivateState(RE::Actor* actor, ERF_StateHandle state) noexcept {
    return ElementalStates::SetActive(actor, state, true);
}
static bool API_DeactivateState(RE::Actor* actor, ERF_StateHandle state) noexcept {
    return ElementalStates::SetActive(actor, state, false);
}

// Implementações usadas na API V1:
static bool API_BeginBatchRegistration() noexcept {
    if (!g_reg_open.load(std::memory_order_acquire)) return false;
    g_reg_barrier.fetch_add(1, std::memory_order_acq_rel);
    return true;
}
static void API_EndBatchRegistration() noexcept { g_reg_barrier.fetch_sub(1, std::memory_order_acq_rel); }
static void API_SetFreezeTimeoutMs(std::uint32_t ms) noexcept { g_timeout_ms.store(ms, std::memory_order_release); }
static bool API_IsRegistrationOpen() noexcept { return g_reg_open.load(std::memory_order_acquire); }
static bool API_IsFrozen() noexcept { return g_frozen.load(std::memory_order_acquire); }
static void API_FreezeNow() noexcept { FreezeRegistriesOnce(); }

static ERF_API_V1 g_api = {ERF_API_VERSION,
                           &API_RegisterElement,
                           &API_RegisterReaction,
                           &API_RegisterPreEffect,
                           &API_RegisterState,
                           &API_SetElementStateMultiplier,
                           &API_ActivateState,
                           &API_DeactivateState,
                           &API_BeginBatchRegistration,
                           &API_EndBatchRegistration,
                           &API_SetFreezeTimeoutMs,
                           &API_IsRegistrationOpen,
                           &API_IsFrozen,
                           &API_FreezeNow};

void ERF::API::OpenRegistrationWindowAndScheduleFreeze() {
    g_reg_open.store(true, std::memory_order_release);
    std::thread([] {
        const auto ms = g_timeout_ms.load(std::memory_order_acquire);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));

        // aguarda a barreira (com timeout de proteção igual ao ms)
        auto start = std::chrono::steady_clock::now();
        while (g_reg_barrier.load(std::memory_order_acquire) > 0) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(ms)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        g_reg_open.store(false, std::memory_order_release);
        FreezeRegistriesOnce();
    }).detach();
}

ERF_API_V1* ERF::API::Get() { return &g_api; }