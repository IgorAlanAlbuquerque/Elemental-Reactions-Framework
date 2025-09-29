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

static ERF_ElementHandle API_RegisterElement(const ERF_ElementDesc_Public& d) {
    ERF_ElementDesc in{};
    in.name = d.name;
    in.colorRGB = d.colorRGB;
    in.keyword = d.keywordID ? RE::TESForm::LookupByID<RE::BGSKeyword>(d.keywordID) : nullptr;
    return ElementRegistry::get().registerElement(in);
}

static ERF_ReactionHandle API_RegisterReaction(const ERF_ReactionDesc_Public& d) {
    ERF_ReactionDesc in{};
    in.name = d.name ? d.name : "";

    if (d.elements && d.elementCount > 0) {
        in.elements.assign(d.elements, d.elements + d.elementCount);
    }

    in.ordered = d.ordered;
    in.minTotalGauge = d.minTotalGauge;
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

static ERF_PreEffectHandle API_RegisterPreEffect(const ERF_PreEffectDesc_Public& d) {
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

static ERF_StateHandle API_RegisterState(const ERF_StateDesc_Public& d) {
    ERF_StateDesc in{};
    in.name = d.name;
    in.keyword = d.keywordID ? RE::TESForm::LookupByID<RE::BGSKeyword>(d.keywordID) : nullptr;
    return StateRegistry::get().registerState(in);
}

static void API_SetElementStateMultiplier(ERF_ElementHandle elem, ERF_StateHandle state, double mult) {
    if (auto* e = ElementRegistry::get().get(elem)) {
        const_cast<ERF_ElementDesc*>(e)->setMultiplierForState(state, mult);
    }
}

static bool API_ActivateState(RE::Actor* actor, ERF_StateHandle state) {
    return ElementalStates::SetActive(actor, state, true);
}
static bool API_DeactivateState(RE::Actor* actor, ERF_StateHandle state) {
    return ElementalStates::SetActive(actor, state, false);
}

static ERF_API_V1 g_api = {ERF_API_VERSION,        &API_RegisterElement, &API_RegisterReaction,
                           &API_RegisterPreEffect, &API_RegisterState,   &API_SetElementStateMultiplier,
                           &API_ActivateState,     &API_DeactivateState};

ERF_API_V1* ERF::API::Get() { return &g_api; }