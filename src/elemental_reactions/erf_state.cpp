#include "erf_state.h"

StateRegistry& StateRegistry::get() {
    static StateRegistry R;
    if (R._states.empty()) {
        R._states.resize(1);
    }
    return R;
}

ERF_StateHandle StateRegistry::registerState(const ERF_StateDesc& d) {
    auto& R = get();
    if (R._states.empty()) R._states.resize(1);
    R._states.push_back(d);
    return static_cast<ERF_StateHandle>(R._states.size() - 1);
}

const ERF_StateDesc* StateRegistry::get(ERF_StateHandle h) const {
    if (h == 0) return nullptr;
    if (static_cast<std::size_t>(h) >= _states.size()) return nullptr;
    return &_states[h];
}

std::optional<ERF_StateHandle> StateRegistry::findByName(std::string_view name) const {
    for (ERF_StateHandle h = 1; h < _states.size(); ++h) {
        const auto& s = _states[h];
        if (s.name == name) return h;
    }
    return std::nullopt;
}

std::optional<ERF_StateHandle> StateRegistry::findByKeyword(const RE::BGSKeyword* kw) const {
    if (!kw) return std::nullopt;
    const auto want = kw->GetFormID();
    for (ERF_StateHandle h = 1; h < _states.size(); ++h) {
        const auto& s = _states[h];
        if (s.keyword && s.keyword->GetFormID() == want) return h;
    }
    return std::nullopt;
}

std::size_t StateRegistry::size() const noexcept { return (_states.size() > 0) ? (_states.size() - 1) : 0; }
