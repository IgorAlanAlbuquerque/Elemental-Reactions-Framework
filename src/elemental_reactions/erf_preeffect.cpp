#include "erf_preeffect.h"

#include <algorithm>

PreEffectRegistry& PreEffectRegistry::get() {
    static PreEffectRegistry R;
    if (R._effects.empty()) R._effects.resize(1);
    return R;
}

ERF_PreEffectHandle
PreEffectRegistry::registerPreEffect(  // NOSONAR - this method intentionally mutates the registry state
    const ERF_PreEffectDesc& d) {
    auto& R = get();
    if (R._frozen) {
        return 0;
    }
    if (R._effects.empty()) R._effects.resize(1);

    ERF_PreEffectDesc copy = d;
    R._effects.push_back(std::move(copy));
    const auto h = static_cast<ERF_PreEffectHandle>(R._effects.size() - 1);

    const auto eh = d.element;

    if (const std::size_t need = static_cast<std::size_t>(eh) + 1; R._byElem.size() < need) R._byElem.resize(need);
    R._byElem[eh].push_back(h);

    return h;
}

const ERF_PreEffectDesc* PreEffectRegistry::get(ERF_PreEffectHandle h) const {
    if (h == 0 || h >= _effects.size()) return nullptr;
    return &_effects[h];
}

std::span<const ERF_PreEffectHandle> PreEffectRegistry::listByElement(ERF_ElementHandle h) const {
    if (h == 0 || h >= _byElem.size()) return {};
    const auto& v = _byElem[h];
    return std::span<const ERF_PreEffectHandle>(v.data(), v.size());
}

void PreEffectRegistry::freeze() {
    if (_frozen) return;
    for (auto& bucket : _byElem) {
        if (bucket.empty()) continue;
        std::ranges::sort(bucket);
        auto unique_range = std::ranges::unique(bucket);
        bucket.erase(unique_range.begin(), unique_range.end());
        bucket.shrink_to_fit();
    }
    _effects.shrink_to_fit();
    _byElem.shrink_to_fit();
    _frozen = true;
}