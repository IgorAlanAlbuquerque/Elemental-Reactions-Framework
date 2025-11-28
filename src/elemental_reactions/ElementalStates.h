#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_element.h"
#include "erf_state.h"

namespace ElementalStates {
    void RegisterStore();

    bool SetActive(RE::Actor* a, ERF_StateHandle sh, bool value);
    bool IsActive(RE::Actor* a, ERF_StateHandle sh);

    void Activate(RE::Actor* a, ERF_StateHandle sh);
    void Deactivate(RE::Actor* a, ERF_StateHandle sh);

    void Clear(RE::Actor* a);
    void ClearAll();

    std::vector<ERF_StateHandle> GetActive(RE::Actor* a);

    double GetGaugeMultiplierFor(RE::Actor* a, ERF_ElementHandle elem);

    double GetHealthMultiplierFor(RE::Actor* a, ERF_ElementHandle elem);
}