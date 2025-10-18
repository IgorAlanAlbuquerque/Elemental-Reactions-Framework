#include "erf_element.h"

#include <algorithm>
#include <cctype>

ElementRegistry& ElementRegistry::get() {
    static ElementRegistry g;  // NOSONAR
    return g;
}

ERF_ElementHandle ElementRegistry::registerElement(const ERF_ElementDesc& d) {
    if (_frozen) {
        spdlog::error("[ERF][ElementRegistry] registerElement após freeze()");
        return 0;
    }
    if (_elems.empty()) _elems.emplace_back("", 0, nullptr);
    _elems.push_back(d);
    return static_cast<ERF_ElementHandle>(_elems.size() - 1);
}

void ElementRegistry::freeze() {
    if (_frozen) return;
    // garante sentinela
    if (_elems.empty()) _elems.emplace_back("", 0, nullptr);

    _nameIndex.clear();
    _kwIndex.clear();
    _nameIndex.reserve(_elems.size());
    _kwIndex.reserve(_elems.size());

    // constrói índices a partir do armazenamento estável do vetor
    const std::size_t cap = StateRegistry::get().size() + 1;
    for (ERF_ElementHandle h = 1; h <= static_cast<ERF_ElementHandle>(size()); ++h) {
        auto& d = _elems[static_cast<std::size_t>(h)];
        if (d.stateMultDense.size() < cap) d.stateMultDense.resize(cap, 1.0);
        if (!d.name.empty()) _nameIndex.emplace(std::string_view{d.name}, h);
        if (d.keyword) _kwIndex.emplace(d.keyword->GetFormID(), h);
    }
    _frozen = true;
}

const ERF_ElementDesc* ElementRegistry::get(ERF_ElementHandle h) const {
    if (h == 0) return nullptr;
    const auto i = static_cast<std::size_t>(h);
    if (i >= _elems.size()) return nullptr;
    return &_elems[i];
}

std::optional<ERF_ElementHandle> ElementRegistry::findByName(std::string_view name) const {
    if (_frozen) {
        auto it = _nameIndex.find(name);
        if (it != _nameIndex.end()) return it->second;
        return std::nullopt;
    }
    // caminho antigo (pré-freeze)
    const auto n = static_cast<ERF_ElementHandle>(this->size());
    for (ERF_ElementHandle h = 1; h <= n; ++h) {
        const ERF_ElementDesc* d = this->get(h);
        if (d && d->name == name) return h;
    }
    return std::nullopt;
}

std::optional<ERF_ElementHandle> ElementRegistry::findByKeyword(const RE::BGSKeyword* kw) const {
    if (!kw) return std::nullopt;
    if (_frozen) {
        auto it = _kwIndex.find(kw->GetFormID());
        if (it != _kwIndex.end()) return it->second;
        return std::nullopt;
    }
    const auto want = kw->GetFormID();
    const auto n = static_cast<ERF_ElementHandle>(this->size());
    for (ERF_ElementHandle h = 1; h <= n; ++h) {
        const ERF_ElementDesc* d = this->get(h);
        if (d && d->keyword && d->keyword->GetFormID() == want) return h;
    }
    return std::nullopt;
}

std::size_t ElementRegistry::size() const { return !_elems.empty() ? (_elems.size() - 1) : 0; }