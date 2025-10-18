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
    std::vector<double> stateMultDense;
    void setMultiplierForState(ERF_StateHandle sh, double mult) {
        if (sh == 0) return;
        const std::size_t need = static_cast<std::size_t>(sh) + 1;
        if (stateMultDense.size() < need) stateMultDense.resize(need, 1.0);
        stateMultDense[sh] = mult;
    }
    double getMultiplierForState(ERF_StateHandle sh, double fallback = 1.0) const {
        if (sh == 0) return fallback;
        const std::size_t i = static_cast<std::size_t>(sh);
        return (i < stateMultDense.size()) ? stateMultDense[i] : fallback;
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

    void freeze();
    bool isFrozen() const noexcept { return _frozen; }

private:
    bool _frozen = false;
    std::vector<ERF_ElementDesc> _elems;
    std::unordered_map<std::string_view, ERF_ElementHandle> _nameIndex;
    std::unordered_map<RE::FormID, ERF_ElementHandle> _kwIndex;
};