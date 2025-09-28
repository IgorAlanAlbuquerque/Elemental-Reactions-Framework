#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_state.h"

using ERF_ElementHandle = std::uint16_t;

struct ERF_ElementDesc {
    std::string name;
    std::uint32_t colorRGB;
    RE::BGSKeyword* keyword;
    std::unordered_map<ERF_StateHandle, double> stateMultipliers;
    void setMultiplierForState(ERF_StateHandle sh, double mult) {
        if (sh != 0) stateMultipliers[sh] = mult;
    }
    double getMultiplierForState(ERF_StateHandle sh, double fallback = 1.0) const {
        if (sh == 0) return fallback;
        if (auto it = stateMultipliers.find(sh); it != stateMultipliers.end()) return it->second;
        return fallback;
    }
};

class ElementRegistry {
public:
    static ElementRegistry& get();

    ERF_ElementHandle registerElement(const ERF_ElementDesc& d);

    const ERF_ElementDesc* get(ERF_ElementHandle h) const;
    std::optional<ERF_ElementHandle> findByName(std::string_view name) const;
    std::optional<ERF_ElementHandle> findByKeyword(const RE::BGSKeyword* kw) const;

    std::size_t size() const;

private:
    std::vector<ERF_ElementDesc> _elems;
};