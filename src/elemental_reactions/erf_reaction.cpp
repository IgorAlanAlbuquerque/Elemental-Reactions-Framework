#include "erf_reaction.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

ReactionRegistry& ReactionRegistry::get() {
    static ReactionRegistry R;
    if (R._reactions.empty()) {
        R._reactions.resize(1);
    }
    return R;
}

ERF_ReactionHandle ReactionRegistry::registerReaction(const ERF_ReactionDesc& d) {
    auto& R = get();
    if (R._reactions.empty()) R._reactions.resize(1);

    R._reactions.push_back(d);
    const auto h = static_cast<ERF_ReactionHandle>(R._reactions.size() - 1);
    return h;
}

const ERF_ReactionDesc* ReactionRegistry::get(ERF_ReactionHandle h) const {
    if (h == 0 || h >= _reactions.size()) return nullptr;
    return &_reactions[h];
}

std::size_t ReactionRegistry::size() const noexcept { return (_reactions.size() > 0) ? (_reactions.size() - 1) : 0; }

namespace {
    inline std::uint8_t valueForHandle(const std::vector<std::uint8_t>& totals, ERF_ElementHandle h) {
        if (h == 0) return 0;
        const std::size_t idx = static_cast<std::size_t>(h - 1);
        return (idx < totals.size()) ? totals[idx] : 0;
    }

    inline float safeDiv(float a, float b) { return (b <= 0.0f) ? 0.0f : (a / b); }
}

std::optional<ERF_ReactionHandle> ReactionRegistry::pickBest(const std::vector<std::uint8_t>& totals,
                                                             const std::vector<ERF_ElementHandle>& present) const {
    const int sumAll = std::accumulate(totals.begin(), totals.end(), 0);
    if (sumAll <= 0) return std::nullopt;

    std::unordered_set<ERF_ElementHandle> pres;
    pres.reserve(present.size());
    for (auto h : present) {
        if (h != 0) pres.insert(h);
    }

    ERF_ReactionHandle bestH = 0;
    float bestScore = 0.0f;
    std::size_t bestElemCount = 0;

    const float fSumAll = static_cast<float>(sumAll);

    for (ERF_ReactionHandle h = 1; h < _reactions.size(); ++h) {
        const auto& r = _reactions[h];

        bool missing = false;
        for (auto eh : r.elements) {
            const auto v = valueForHandle(totals, eh);
            if (v <= 0) {
                missing = true;
                break;
            }
        }
        if (missing) continue;

        int sumSel = 0;
        bool okPctEach = true;
        for (auto eh : r.elements) {
            const int v = valueForHandle(totals, eh);
            sumSel += v;
            if (r.minPctEach > 0.0f) {
                const float p = safeDiv(static_cast<float>(v), fSumAll);
                if (p + 1e-6f < r.minPctEach) {
                    okPctEach = false;
                    break;
                }
            }
        }
        if (!okPctEach) continue;

        if (r.minTotalGauge > 0 && sumAll < static_cast<int>(r.minTotalGauge)) continue;

        const float fracSel = safeDiv(static_cast<float>(sumSel), fSumAll);
        if (r.minSumSelected > 0.0f && fracSel + 1e-6f < r.minSumSelected) continue;

        const std::size_t elemCount = r.elements.size();
        const bool better = (fracSel > bestScore) ||
                            ((std::abs(fracSel - bestScore) < 1e-6f) && (elemCount > bestElemCount)) ||
                            ((std::abs(fracSel - bestScore) < 1e-6f) && (elemCount == bestElemCount) && (h < bestH));

        if (better) {
            bestScore = fracSel;
            bestElemCount = elemCount;
            bestH = h;
        }
    }

    if (bestH == 0) return std::nullopt;
    return bestH;
}

std::optional<ERF_ReactionHandle> ReactionRegistry::pickBestForHud(
    const std::vector<std::uint8_t>& totals, const std::vector<ERF_ElementHandle>& present) const {
    const int sumAll = std::accumulate(totals.begin(), totals.end(), 0);
    if (sumAll <= 0) return std::nullopt;

    std::unordered_set<ERF_ElementHandle> pres;
    pres.reserve(present.size());
    for (auto h : present)
        if (h != 0) pres.insert(h);

    ERF_ReactionHandle bestH = 0;
    float bestScore = 0.0f;
    std::size_t bestElemCount = 0;

    const float fSumAll = static_cast<float>(sumAll);

    for (ERF_ReactionHandle h = 1; h < _reactions.size(); ++h) {
        const auto& r = _reactions[h];

        bool missing = false;
        int sumSel = 0;
        bool okPctEach = true;

        for (auto eh : r.elements) {
            const int v = valueForHandle(totals, eh);
            if (v <= 0) {
                missing = true;
                break;
            }
            sumSel += v;

            if (r.minPctEach > 0.0f) {
                const float p = safeDiv(static_cast<float>(v), fSumAll);
                if (p + 1e-6f < r.minPctEach) {
                    okPctEach = false;
                    break;
                }
            }
        }
        if (missing || !okPctEach) continue;

        const float fracSel = safeDiv(static_cast<float>(sumSel), fSumAll);

        if (r.minSumSelected > 0.0f && fracSel + 1e-6f < r.minSumSelected) continue;

        const std::size_t elemCount = r.elements.size();
        const bool better = (fracSel > bestScore) ||
                            ((std::abs(fracSel - bestScore) < 1e-6f) && (elemCount > bestElemCount)) ||
                            ((std::abs(fracSel - bestScore) < 1e-6f) && (elemCount == bestElemCount) && (h < bestH));

        if (better) {
            bestScore = fracSel;
            bestElemCount = elemCount;
            bestH = h;
        }
    }

    if (bestH == 0) return std::nullopt;
    return bestH;
}