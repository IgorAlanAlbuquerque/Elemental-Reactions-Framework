#include "erf_reaction.h"

#include <algorithm>
#include <cassert>
#include <numeric>

#include "../common/Helpers.h"

namespace {
    static int valueForHandle(std::span<const std::uint8_t> totals, ERF_ElementHandle h) {
        if (h == 0) return 0;
        const std::size_t idx = static_cast<std::size_t>(h);
        return (idx < totals.size()) ? totals[idx] : 0;
    }
}

ReactionRegistry& ReactionRegistry::get() {
    static ReactionRegistry R;
    if (R._reactions.empty()) R._reactions.resize(1);
    return R;
}

ERF_ReactionHandle ReactionRegistry::registerReaction(const ERF_ReactionDesc& d) {
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

std::size_t ReactionRegistry::size() const noexcept { return (_reactions.size() > 0) ? (_reactions.size() - 1) : 0; }

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

void ReactionRegistry::freeze() { buildIndex_(); }

std::optional<ERF_ReactionHandle> ReactionRegistry::pickBest_core(std::span<const std::uint8_t> totals,
                                                                  std::span<const ERF_ElementHandle> present,
                                                                  float invSumAll, const ReactionRegistry* self,
                                                                  const std::vector<bool>* used) {
    self->buildIndex_();

    ReactionRegistry::Mask presentMask = 0;
    for (auto h : present) {
        if (!h) continue;
        const unsigned bit = static_cast<unsigned>(h - 1);
        if (bit < 64) presentMask |= (ReactionRegistry::Mask(1) << bit);
    }
    if (!presentMask) return std::nullopt;

    auto popcount64 = [](ReactionRegistry::Mask m) -> int {
#if defined(_MSC_VER)
        return static_cast<int>(__popcnt64(m));
#else
        return __builtin_popcountll(m);
#endif
    };

    ERF_ReactionHandle bestH = 0;
    float bestScore = 0.0f;
    std::size_t bestK = 0;

    auto evalBucket = [&](ReactionRegistry::Mask m) {
        auto itB = self->_byMask.find(m);
        if (itB == self->_byMask.end()) return;

        const auto& bucket = itB->second;
        for (ERF_ReactionHandle h : bucket) {
            if (used && h < used->size() && (*used)[h]) {
                continue;
            }

            const auto& r = self->_reactions[h];

            int sumSel = 0;
            bool okPctEach = true;

            for (auto eh : r.elements) {
                const int v = valueForHandle(totals, eh);
                sumSel += v;

                const float req = self->_minPctEachByH[h];
                if (req > 0.0f) {
                    const float p = static_cast<float>(v) * invSumAll;
                    if (p + 1e-6f < req) {
                        okPctEach = false;
                        break;
                    }
                }
            }
            if (!okPctEach) continue;

            const float fracSel = static_cast<float>(sumSel) * invSumAll;
            if (self->_minSumSelByH[h] > 0.0f && fracSel + 1e-6f < self->_minSumSelByH[h]) continue;

            const std::size_t k = self->_kByH[h];
            const bool better = (fracSel > bestScore) || ((std::abs(fracSel - bestScore) < 1e-6f) && (k > bestK)) ||
                                ((std::abs(fracSel - bestScore) < 1e-6f) && (k == bestK) && (h < bestH));

            if (better) {
                bestScore = fracSel;
                bestK = k;
                bestH = h;

                if (bestScore >= 1.0f - 1e-6f) return;
            }
        }
    };

    evalBucket(presentMask);
    if (bestH != 0 && bestScore >= 1.0f - 1e-6f) return bestH;

    for (auto sub = presentMask; sub; sub = (sub - 1) & presentMask) {
        if (sub == presentMask) continue;

        evalBucket(sub);
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
    if (!rh) return std::nullopt;

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
        if (!rh) break;

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