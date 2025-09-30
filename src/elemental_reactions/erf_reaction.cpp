#include "erf_reaction.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <unordered_set>

namespace {
    inline std::uint8_t valueForHandle(const std::vector<std::uint8_t>& totals, ERF_ElementHandle h) {
        if (h == 0) return 0;
        const std::size_t idx = static_cast<std::size_t>(h - 1);
        return (idx < totals.size()) ? totals[idx] : 0;
    }
    inline float safeDiv(float a, float b) { return (b <= 0.0f) ? 0.0f : (a / b); }
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
    R._indexed = false;  // invalida índice
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
        const auto bit = static_cast<unsigned>(h - 1);  // idx começa em 0
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
    _byMask.reserve(N);

    for (ERF_ReactionHandle h = 1; h < N; ++h) {
        const auto& r = _reactions[h];
        const auto m = makeMask_(r.elements);
        const auto k = static_cast<std::uint8_t>(r.elements.size());

        _maskByH[h] = m;
        _kByH[h] = k;
        _minPctEachByH[h] = r.minPctEach;
        _minSumSelByH[h] = r.minSumSelected;

        _byMask[m].push_back(h);
    }

    _indexed = true;
}

void ReactionRegistry::freeze() {
    buildIndex_();  // só garante que está pronto
}

std::optional<ERF_ReactionHandle> ReactionRegistry::pickBest(const std::vector<std::uint8_t>& totals,
                                                             const std::vector<ERF_ElementHandle>& present) const {
    buildIndex_();
    const int sumAll = std::accumulate(totals.begin(), totals.end(), 0);
    if (sumAll <= 0) return std::nullopt;

    Mask presentMask = 0;
    for (auto h : present) {
        if (!h) continue;
        const unsigned bit = static_cast<unsigned>(h - 1);
        if (bit < 64) presentMask |= (Mask(1) << bit);
    }
    if (!presentMask) return std::nullopt;

    const float fSumAll = static_cast<float>(sumAll);
    ERF_ReactionHandle bestH = 0;
    float bestScore = 0.0f;
    std::size_t bestK = 0;

    for (Mask sub = presentMask; sub; sub = (sub - 1) & presentMask) {
        auto it = _byMask.find(sub);
        if (it == _byMask.end()) continue;
        const auto& bucket = it->second;

        for (ERF_ReactionHandle h : bucket) {
            // *** DIFERENÇA CRÍTICA: NÃO checar _minTotalByH[h] aqui ***

            const auto& r = _reactions[h];
            int sumSel = 0;
            bool okPctEach = true;

            for (auto eh : r.elements) {
                const int v = valueForHandle(totals, eh);
                sumSel += v;
                const float req = _minPctEachByH[h];
                if (req > 0.0f) {
                    const float p = safeDiv(static_cast<float>(v), fSumAll);
                    if (p + 1e-6f < req) {
                        okPctEach = false;
                        break;
                    }
                }
            }
            if (!okPctEach) continue;

            const float fracSel = safeDiv(static_cast<float>(sumSel), fSumAll);
            if (_minSumSelByH[h] > 0.0f && fracSel + 1e-6f < _minSumSelByH[h]) continue;

            const std::size_t k = _kByH[h];
            const bool better = (fracSel > bestScore) || ((std::abs(fracSel - bestScore) < 1e-6f) && (k > bestK)) ||
                                ((std::abs(fracSel - bestScore) < 1e-6f) && (k == bestK) && (h < bestH));

            if (better) {
                bestScore = fracSel;
                bestK = k;
                bestH = h;
            }
        }
    }

    if (bestH == 0) return std::nullopt;
    return bestH;
}