#pragma once
#include <optional>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_element.h"

using ERF_ReactionHandle = std::uint16_t;

struct ERF_ReactionHudIcon {
    std::string iconPath;
    std::uint32_t iconTint = 0xFFFFFF;
};

struct ERF_ReactionDesc {
    std::string name;

    std::vector<ERF_ElementHandle> elements;
    bool ordered = false;

    std::uint32_t minTotalGauge = 100;
    float minPctEach = 0.0f;

    float cooldownSeconds = 0.5f;
    bool cooldownIsRealTime = true;
    float elementLockoutSeconds = 0.0f;
    bool elementLockoutIsRealTime = true;
    bool clearAllOnTrigger = true;
    float minSumSelected = 0.0f;

    using Callback = void (*)(RE::Actor* actor, void* user);
    Callback cb = nullptr;
    void* user = nullptr;

    ERF_ReactionHudIcon hud{};
};

class ReactionRegistry {
public:
    static ReactionRegistry& get();
    ERF_ReactionHandle registerReaction(const ERF_ReactionDesc& d);
    const ERF_ReactionDesc* get(ERF_ReactionHandle h) const;
    std::optional<ERF_ReactionHandle> pickBest(const std::vector<std::uint8_t>& totals,
                                               const std::vector<ERF_ElementHandle>& present) const;
    std::optional<ERF_ReactionHandle> pickBestForHud(const std::vector<std::uint8_t>& totals,
                                                     const std::vector<ERF_ElementHandle>& present) const;
    std::size_t size() const noexcept;

private:
    ReactionRegistry() = default;
    std::vector<ERF_ReactionDesc> _reactions;
};