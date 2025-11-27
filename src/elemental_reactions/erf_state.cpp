#include "erf_state.h"

StateRegistry& StateRegistry::get() {
    static StateRegistry R;
    if (R._states.empty()) R._states.resize(1);
    return R;
}

ERF_StateHandle StateRegistry::registerState(  // NOSONAR - this method intentionally mutates the registry state
    const ERF_StateDesc& d) {
    if (_frozen) {
        return 0;
    }
    auto& R = get();
    if (R._states.empty()) R._states.resize(1);
    R._states.push_back(d);
    return static_cast<ERF_StateHandle>(R._states.size() - 1);
}

ERF_StateElementMult StateRegistry::getElementMultipliers(ERF_StateHandle state, std::uint16_t elemHandle) const {
    ERF_StateElementMult def{1.0, 1.0};

    if (state == 0 || elemHandle == 0) {
        return def;
    }

    const auto si = static_cast<std::size_t>(state);
    const auto ei = static_cast<std::size_t>(elemHandle);

    if (si >= _perElementMult.size()) {
        return def;
    }
    const auto& row = _perElementMult[si];
    if (ei >= row.size()) {
        return def;
    }
    return row[ei];
}

void StateRegistry::setElementMultipliers(  // NOSONAR - this method intentionally mutates the registry state
    ERF_StateHandle state, std::uint16_t elemHandle, double gaugeMult, double healthMult) {
    if (state == 0 || elemHandle == 0) {
        return;
    }

    auto& R = get();
    const auto si = static_cast<std::size_t>(state);
    const auto ei = static_cast<std::size_t>(elemHandle);

    if (R._perElementMult.size() <= si) {
        R._perElementMult.resize(si + 1);
    }
    auto& row = R._perElementMult[si];
    if (row.size() <= ei) {
        row.resize(ei + 1, ERF_StateElementMult{1.0, 1.0});
    }

    row[ei].gaugeMult = gaugeMult;
    row[ei].healthMult = healthMult;
}

double StateRegistry::getGaugeMultiplier(ERF_StateHandle state, std::uint16_t elemHandle) const {
    return getElementMultipliers(state, elemHandle).gaugeMult;
}

double StateRegistry::getHealthMultiplier(ERF_StateHandle state, std::uint16_t elemHandle) const {
    return getElementMultipliers(state, elemHandle).healthMult;
}

void StateRegistry::freeze() {
    if (_frozen) return;
    if (_states.empty()) _states.resize(1);

    _nameIndex.clear();
    _kwIndex.clear();
    _nameIndex.reserve(_states.size());
    _kwIndex.reserve(_states.size());

    for (ERF_StateHandle h = 1; h < _states.size(); ++h) {
        auto const& s = _states[h];
        if (!s.name.empty()) _nameIndex.emplace(std::string_view{s.name}, h);
        if (s.keyword) _kwIndex.emplace(s.keyword->GetFormID(), h);
    }
    _frozen = true;
}

const ERF_StateDesc* StateRegistry::get(ERF_StateHandle h) const {
    if (h == 0) return nullptr;
    if (static_cast<std::size_t>(h) >= _states.size()) return nullptr;
    return &_states[h];
}

std::optional<ERF_StateHandle> StateRegistry::findByName(std::string_view name) const {
    if (_frozen) {
        if (auto it = _nameIndex.find(name); it != _nameIndex.end()) return it->second;
        return std::nullopt;
    }
    for (ERF_StateHandle h = 1; h < _states.size(); ++h) {
        const auto& s = _states[h];
        if (s.name == name) return h;
    }
    return std::nullopt;
}

std::optional<ERF_StateHandle> StateRegistry::findByKeyword(const RE::BGSKeyword* kw) const {
    if (!kw) return std::nullopt;
    if (_frozen) {
        if (auto it = _kwIndex.find(kw->GetFormID()); it != _kwIndex.end()) return it->second;
        return std::nullopt;
    }
    const auto want = kw->GetFormID();
    for (ERF_StateHandle h = 1; h < _states.size(); ++h) {
        const auto& s = _states[h];
        if (s.keyword && s.keyword->GetFormID() == want) return h;
    }
    return std::nullopt;
}

std::size_t StateRegistry::size() const noexcept { return (!_states.empty()) ? (_states.size() - 1) : 0; }
