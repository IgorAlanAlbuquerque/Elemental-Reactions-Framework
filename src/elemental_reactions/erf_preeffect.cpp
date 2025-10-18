#include "erf_preeffect.h"

PreEffectRegistry& PreEffectRegistry::get() {
    static PreEffectRegistry R;
    if (R._effects.empty()) R._effects.resize(1);
    return R;
}

ERF_PreEffectHandle PreEffectRegistry::registerPreEffect(const ERF_PreEffectDesc& d) {
    auto& R = get();
    if (R._effects.empty()) R._effects.resize(1);
    ERF_PreEffectDesc copy = d;
    R._effects.push_back(std::move(copy));
    const auto h = static_cast<ERF_PreEffectHandle>(R._effects.size() - 1);
    const auto eh = d.element;
    const std::size_t need = static_cast<std::size_t>(eh) + 1;
    if (R._byElem.size() < need) R._byElem.resize(need);
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
