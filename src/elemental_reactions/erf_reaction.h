#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_element.h"

using ERF_ReactionHandle = std::uint16_t;

struct ERF_ReactionContext;

using ERF_ReactionCallback = void (*)(const ERF_ReactionContext& ctx, void* user);

struct ERF_ReactionHudIcon {
    std::string iconPath;
    std::uint32_t iconTint = 0xFFFFFF;
};

struct ERF_ReactionDesc {
    std::string name;
    std::vector<ERF_ElementHandle> elements;

    float minPctEach = 0.0f;
    float minSumSelected = 0.0f;
    float cooldownSeconds = 0.0f;
    float elementLockoutSeconds = 0.0f;

    bool ordered = false;
    bool cooldownIsRealTime = true;
    bool elementLockoutIsRealTime = true;
    bool clearAllOnTrigger = true;

    ERF_ReactionHudIcon hud{};

    ERF_ReactionCallback cb = nullptr;
    void* user = nullptr;
};

class ReactionRegistry {
public:
    static ReactionRegistry& get();
    ERF_ReactionHandle registerReaction(const ERF_ReactionDesc& d);
    const ERF_ReactionDesc* get(ERF_ReactionHandle h) const;
    std::optional<ERF_ReactionHandle> pickBestFast(const std::vector<std::uint8_t>& totals,
                                                   const std::vector<ERF_ElementHandle>& present, int sumAll,
                                                   float invSumAll) const;
    std::size_t size() const noexcept;
    void freeze();
    bool isFrozen() const noexcept { return _indexed; }

private:
    ReactionRegistry() = default;
    std::vector<ERF_ReactionDesc> _reactions;  // [0] inválido

    using Mask = std::uint64_t;

    // caches/índices (mutáveis para construir em métodos const)
    mutable bool _indexed = false;
    mutable std::vector<Mask> _maskByH;
    mutable std::vector<std::uint8_t> _kByH;
    mutable std::vector<std::uint32_t> _minTotalByH;
    mutable std::vector<float> _minPctEachByH;
    mutable std::vector<float> _minSumSelByH;
    mutable std::unordered_map<Mask, std::vector<ERF_ReactionHandle>> _byMask;

    void buildIndex_() const;
    static Mask makeMask_(const std::vector<ERF_ElementHandle>& elems);
    static std::optional<ERF_ReactionHandle> pickBest_core(const std::vector<std::uint8_t>& totals,
                                                           const std::vector<ERF_ElementHandle>& present,
                                                           float invSumAll, const ReactionRegistry* self);
};