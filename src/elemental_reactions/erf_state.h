#pragma once
#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "RE/Skyrim.h"

using ERF_StateHandle = std::uint16_t;

struct ERF_StateDesc {
    std::string name;
    RE::BGSKeyword* keyword = nullptr;
};

class StateRegistry {
public:
    static StateRegistry& get();

    ERF_StateHandle registerState(const ERF_StateDesc& d);

    const ERF_StateDesc* get(ERF_StateHandle h) const;
    std::optional<ERF_StateHandle> findByName(std::string_view name) const;
    std::optional<ERF_StateHandle> findByKeyword(const RE::BGSKeyword* kw) const;

    std::size_t size() const noexcept;

    void freeze();
    bool isFrozen() const noexcept { return _frozen; }

private:
    StateRegistry() = default;
    std::vector<ERF_StateDesc> _states;
    bool _frozen = false;
    ankerl::unordered_dense::map<std::string_view, ERF_StateHandle> _nameIndex;
    ankerl::unordered_dense::map<RE::FormID, ERF_StateHandle> _kwIndex;
};
