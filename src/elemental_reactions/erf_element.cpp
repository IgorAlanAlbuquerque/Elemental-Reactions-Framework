#include "erf_element.h"

#include <algorithm>
#include <cctype>

ElementRegistry& ElementRegistry::get() {
    static ElementRegistry g;  // NOSONAR
    return g;
}

ERF_ElementHandle ElementRegistry::registerElement(const ERF_ElementDesc& d) {
    if (_elems.empty()) {
        _elems.emplace_back("", 0, nullptr);
    }
    _elems.push_back(d);
    const auto h = static_cast<ERF_ElementHandle>(_elems.size() - 1);
    return h;
}

const ERF_ElementDesc* ElementRegistry::get(ERF_ElementHandle h) const {
    if (h == 0) return nullptr;
    const auto i = static_cast<std::size_t>(h);
    if (i >= _elems.size()) return nullptr;
    return &_elems[i];
}

std::optional<ERF_ElementHandle> ElementRegistry::findByName(std::string_view name) const {
    const auto n = static_cast<ERF_ElementHandle>(this->size());
    for (ERF_ElementHandle h = 1; h <= n; ++h) {
        const ERF_ElementDesc* d = this->get(h);
        if (d && d->name == name) {
            return h;
        }
    }
    return std::nullopt;
}

std::optional<ERF_ElementHandle> ElementRegistry::findByKeyword(const RE::BGSKeyword* kw) const {
    if (!kw) return std::nullopt;

    const auto want = kw->GetFormID();
    const auto n = static_cast<ERF_ElementHandle>(this->size());

    for (ERF_ElementHandle h = 1; h <= n; ++h) {
        const ERF_ElementDesc* d = this->get(h);
        if (d && d->keyword && d->keyword->GetFormID() == want) {
            return h;
        }
    }
    return std::nullopt;
}

std::size_t ElementRegistry::size() const { return !_elems.empty() ? (_elems.size() - 1) : 0; }