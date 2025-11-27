#pragma once

#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_element.h"

using ERF_ReactionHandle = std::uint16_t;

struct ERF_ReactionContext;

using ERF_ReactionCallback = void (*)(const ERF_ReactionContext& ctx, void* user);

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

    std::uint32_t Tint = 0xFFFFFF;
    std::string iconName;

    ERF_ReactionCallback cb = nullptr;
    void* user = nullptr;
};

struct ERF_PickBestInfo {
    ERF_ReactionHandle handle{0};
    std::uint32_t colorRGB{0xFFFFFF};
    const char* icon{nullptr};
};

class ReactionRegistry {
public:
    static ReactionRegistry& get();
    ERF_ReactionHandle registerReaction(const ERF_ReactionDesc& d);
    const ERF_ReactionDesc* get(ERF_ReactionHandle h) const;
    std::optional<ERF_PickBestInfo> pickBestFast(std::span<const std::uint8_t> totals,
                                                 std::span<const ERF_ElementHandle> present, int sumAll,
                                                 float invSumAll) const;
    void pickBestFastMulti(std::span<const std::uint8_t> totals, std::span<const ERF_ElementHandle> present, int sumAll,
                           float invSumAll, int maxCount, std::vector<ERF_PickBestInfo>& out) const;
    std::size_t size() const noexcept;
    void freeze();
    bool isFrozen() const noexcept { return _indexed; }

private:
    ReactionRegistry() = default;
    std::vector<ERF_ReactionDesc> _reactions;

    using Mask = std::uint64_t;

    mutable bool _indexed = false;
    mutable std::vector<Mask> _maskByH;
    mutable std::vector<std::uint8_t> _kByH;
    mutable std::vector<std::uint32_t> _minTotalByH;
    mutable std::vector<float> _minPctEachByH;
    mutable std::vector<float> _minSumSelByH;
    mutable ankerl::unordered_dense::map<Mask, std::vector<ERF_ReactionHandle>> _byMask;

    void buildIndex_() const;
    static Mask makeMask_(const std::vector<ERF_ElementHandle>& elems);
    static std::optional<ERF_ReactionHandle> pickBest_core(std::span<const std::uint8_t> totals,
                                                           std::span<const ERF_ElementHandle> present, float invSumAll,
                                                           const ReactionRegistry* self,
                                                           const std::vector<bool>* used = nullptr);
    void evalBucketForPickBest(Mask m, std::span<const std::uint8_t> totals, float invSumAll,
                               const std::vector<bool>* used, ERF_ReactionHandle& bestH, float& bestScore,
                               std::size_t& bestK) const;
    bool checkMinSumSel(ERF_ReactionHandle h, int sumSel, float invSumAll, float& fracSelOut) const;

    bool checkEachElementPct(ERF_ReactionHandle h, const ERF_ReactionDesc& r, std::span<const std::uint8_t> totals,
                             float invSumAll, int& outSumSel) const;
};