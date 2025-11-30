#include "erf_reaction.h"

#include <algorithm>
#include <cassert>
#include <numeric>

namespace {
    int valueForHandle(std::span<const std::uint8_t> totals, ERF_ElementHandle h) {
        if (h == 0) return 0;
        const auto idx = static_cast<std::size_t>(h);
        return (idx < totals.size()) ? totals[idx] : 0;
    }
    constexpr float EPS = 1e-6f;

    bool isReactionUsed(const std::vector<bool>* used, ERF_ReactionHandle h) {
        return used && h < used->size() && (*used)[h];
    }

    bool isBetterCandidate(ERF_ReactionHandle h, float fracSel, std::size_t k, ERF_ReactionHandle bestH,
                           float bestScore, std::size_t bestK) {
        if (fracSel > bestScore) {
            return true;
        }

        if (const bool tieScore = std::abs(fracSel - bestScore) < EPS; !tieScore) {
            return false;
        }

        if (k > bestK) {
            return true;
        }

        if (k < bestK) {
            return false;
        }

        return h < bestH;
    }
}

ReactionRegistry& ReactionRegistry::get() {
    static ReactionRegistry R;
    if (R._reactions.empty()) R._reactions.resize(1);
    return R;
}

bool ReactionRegistry::checkOrdered(const ERF_ReactionDesc& r, std::span<const std::uint8_t> totals) const {
    if (!r.ordered || r.elements.empty()) {
        return true;
    }

    const ERF_ElementHandle hFirst = r.elements[0];
    const int vFirst = valueForHandle(totals, hFirst);

    for (std::size_t i = 1; i < r.elements.size(); ++i) {
        const ERF_ElementHandle h = r.elements[i];
        const int v = valueForHandle(totals, h);

        if (v > vFirst) {
            return false;
        }
    }

    return true;
}

ERF_ReactionHandle
ReactionRegistry::registerReaction(  // NOSONAR - this method intentionally mutates the reaction registry state
    const ERF_ReactionDesc& d) {
    auto& R = get();
    if (R._reactions.empty()) R._reactions.resize(1);
    R._reactions.push_back(d);
    R._indexed = false;
    return static_cast<ERF_ReactionHandle>(R._reactions.size() - 1);
}

const ERF_ReactionDesc* ReactionRegistry::get(ERF_ReactionHandle h) const {
    if (h == 0 || h >= _reactions.size()) return nullptr;
    return &_reactions[h];
}

std::size_t ReactionRegistry::size() const noexcept { return (!_reactions.empty()) ? (_reactions.size() - 1) : 0; }

ReactionRegistry::Mask ReactionRegistry::makeMask_(const std::vector<ERF_ElementHandle>& elems) {
    Mask m = 0;
    for (auto h : elems) {
        if (h == 0) continue;
        const auto bit = static_cast<unsigned>(h - 1);
        if (bit < 64) m |= (Mask(1) << bit);
    }
    return m;
}

void ReactionRegistry::buildIndex_() const {
    if (_indexed) return;

    const auto N = _reactions.size();
    _maskByH.clear();
    _maskByH.resize(N, 0);
    _kByH.clear();
    _kByH.resize(N, 0);
    _minTotalByH.clear();
    _minTotalByH.resize(N, 0);
    _minPctEachByH.clear();
    _minPctEachByH.resize(N, 0.0f);
    _minSumSelByH.clear();
    _minSumSelByH.resize(N, 0.0f);

    _byMask.clear();
    _byMask.reserve(N * 2);

    for (ERF_ReactionHandle h = 1; h < N; ++h) {
        const auto& r = _reactions[h];
        const auto m = makeMask_(r.elements);
        const auto k = static_cast<std::uint8_t>(r.elements.size());

        _maskByH[h] = m;
        _kByH[h] = k;
        _minPctEachByH[h] = r.minPctEach;
        _minSumSelByH[h] = r.minSumSelected;

        auto [__itB, __insertedB] = _byMask.try_emplace(m);
        auto& bucket = __itB->second;
        if (bucket.empty()) bucket.reserve(4);
        bucket.push_back(h);
    }

    _indexed = true;
}

void ReactionRegistry::freeze() { buildIndex_(); }  // NOSONAR - freeze() intentionally mutates the registry state

bool ReactionRegistry::checkEachElementPct(ERF_ReactionHandle h, const ERF_ReactionDesc& r,
                                           std::span<const std::uint8_t> totals, float invSumAll,
                                           int& outSumSel) const {
    outSumSel = 0;
    const float req = _minPctEachByH[h];

    if (req <= 0.0f) {
        for (auto eh : r.elements) {
            const int v = valueForHandle(totals, eh);
            outSumSel += v;
        }
        return true;
    }

    for (auto eh : r.elements) {
        const int v = valueForHandle(totals, eh);
        outSumSel += v;

        const float p = static_cast<float>(v) * invSumAll;
        if (p + EPS < req) {
            return false;
        }
    }
    return true;
}

bool ReactionRegistry::checkMinSumSel(ERF_ReactionHandle h, int sumSel, float invSumAll, float& fracSelOut) const {
    const float fracSel = static_cast<float>(sumSel) * invSumAll;
    fracSelOut = fracSel;

    if (const float minReq = _minSumSelByH[h]; minReq > 0.0f && fracSel + EPS < minReq) {
        return false;
    }
    return true;
}

void ReactionRegistry::evalBucketForPickBest(Mask m, std::span<const std::uint8_t> totals, float invSumAll,
                                             const std::vector<bool>* used, ERF_ReactionHandle& bestH, float& bestScore,
                                             std::size_t& bestK) const {
    auto itB = _byMask.find(m);
    if (itB == _byMask.end()) {
        return;
    }

    const auto& bucket = itB->second;
    for (ERF_ReactionHandle h : bucket) {
        if (isReactionUsed(used, h)) {
            continue;
        }

        const auto& r = _reactions[h];

        int sumSel = 0;
        if (!checkEachElementPct(h, r, totals, invSumAll, sumSel)) {
            continue;
        }

        float fracSel = 0.0f;
        if (!checkMinSumSel(h, sumSel, invSumAll, fracSel)) {
            continue;
        }

        if (!checkOrdered(r, totals)) {
            continue;
        }

        const std::size_t k = _kByH[h];
        if (!isBetterCandidate(h, fracSel, k, bestH, bestScore, bestK)) {
            continue;
        }

        bestScore = fracSel;
        bestK = k;
        bestH = h;

        if (bestScore >= 1.0f - EPS) {
            return;
        }
    }
}

std::optional<ERF_ReactionHandle> ReactionRegistry::pickBest_core(std::span<const std::uint8_t> totals,
                                                                  std::span<const ERF_ElementHandle> present,
                                                                  float invSumAll, const ReactionRegistry* self,
                                                                  const std::vector<bool>* used) {
    self->buildIndex_();

    Mask presentMask = 0;
    for (auto h : present) {
        if (!h) continue;
        const auto bit = static_cast<unsigned>(h - 1);
        if (bit < 64) presentMask |= (Mask(1) << bit);
    }
    if (!presentMask) return std::nullopt;

    ERF_ReactionHandle bestH = 0;
    float bestScore = 0.0f;
    std::size_t bestK = 0;

    self->evalBucketForPickBest(presentMask, totals, invSumAll, used, bestH, bestScore, bestK);
    if (bestH != 0 && bestScore >= 1.0f - 1e-6f) return bestH;

    for (auto sub = presentMask; sub; sub = (sub - 1) & presentMask) {
        if (sub == presentMask) continue;

        self->evalBucketForPickBest(sub, totals, invSumAll, used, bestH, bestScore, bestK);
        if (bestH != 0 && bestScore >= 1.0f - 1e-6f) break;
    }

    if (bestH == 0) return std::nullopt;
    return bestH;
}

std::optional<ERF_PickBestInfo> ReactionRegistry::pickBestFast(std::span<const std::uint8_t> totals,
                                                               std::span<const ERF_ElementHandle> present, int sumAll,
                                                               float invSumAll) const {
    if (sumAll <= 0) return std::nullopt;

    auto rh = pickBest_core(totals, present, invSumAll, this);
    if (!rh.has_value()) return std::nullopt;

    const ERF_ReactionDesc* d = get(*rh);
    if (!d) return std::nullopt;

    ERF_PickBestInfo info;
    info.handle = *rh;
    info.colorRGB = d->Tint;
    info.icon = d->iconName.empty() ? nullptr : d->iconName.c_str();
    return info;
}

void ReactionRegistry::pickBestFastMulti(std::span<const std::uint8_t> totals,
                                         std::span<const ERF_ElementHandle> present, int sumAll, float invSumAll,
                                         int maxCount, std::vector<ERF_PickBestInfo>& out) const {
    out.clear();
    if (sumAll <= 0 || maxCount <= 0) return;

    std::vector<bool> used(_reactions.size(), false);

    while (maxCount-- > 0) {
        auto rh = pickBest_core(totals, present, invSumAll, this, &used);
        if (!rh.has_value()) break;

        const ERF_ReactionDesc* d = get(*rh);

        if (*rh < used.size()) {
            used[*rh] = true;
        }

        if (!d) continue;

        ERF_PickBestInfo info;
        info.handle = *rh;
        info.colorRGB = d->Tint;
        info.icon = d->iconName.empty() ? nullptr : d->iconName.c_str();

        out.push_back(info);
    }
}