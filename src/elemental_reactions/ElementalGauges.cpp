#include "ElementalGauges.h"

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include "../Config.h"
#include "../common/Helpers.h"
#include "../common/PluginSerialization.h"
#include "../hud/HUDTick.h"
#include "../hud/InjectHUD.h"
#include "ElementalStates.h"
#include "erf_preeffect.h"

using namespace ElementalGaugesDecay;
using Elem = ERF_ElementHandle;

namespace Gauges {
    inline constexpr std::uint32_t kRecordID = FOURCC('G', 'A', 'U', 'V');
    inline constexpr std::uint32_t kVersion = 4;
    inline constexpr float increaseMult = 1.30f;
    inline constexpr float decreaseMult = 0.10f;
    constexpr bool kReserveZero = true;

    struct Entry {
        std::vector<std::uint8_t> v;
        std::vector<float> lastHitH;
        std::vector<float> lastEvalH;
        std::vector<float> blockUntilH;
        std::vector<double> blockUntilRtS;

        std::vector<double> reactCdRtS;
        std::vector<float> reactCdH;
        std::vector<std::uint8_t> inReaction;

        std::vector<std::uint8_t> preActive;
        std::vector<float> preIntensity;
        std::vector<double> preExpireRtS;
        std::vector<float> preExpireH;

        std::vector<double> effMult;
        bool effDirty = true;

        bool sized = false;
        std::uint64_t presentMask = 0;
        std::vector<ERF_ElementHandle> presentList;
        int sumAll = 0;
        std::vector<std::uint16_t> posInList;
    };

    using Map = ankerl::unordered_dense::map<RE::FormID, Entry>;
    inline Map& state() noexcept {
        static Map m;
        if (m.bucket_count() == 0) m.reserve(512);
        return m;
    }

    inline std::size_t firstIndex() { return kReserveZero ? 1u : 0u; }
    inline std::size_t idx(ERF_ElementHandle h) { return static_cast<std::size_t>(h == 0 ? 1 : h); }
    inline std::size_t idxElem(ERF_ElementHandle h) { return static_cast<std::size_t>(h == 0 ? 1 : h); }
    inline std::size_t idxReact(ERF_ReactionHandle r) { return static_cast<std::size_t>(r == 0 ? 1 : r); }
    inline std::size_t idxPre(ERF_PreEffectHandle p) { return static_cast<std::size_t>(p == 0 ? 1 : p); }

    inline void initEntryDenseIfNeeded(Entry& e) {
        if (e.sized) return;
        const auto& caps = ERF::API::Caps();

        const std::size_t nE = static_cast<std::size_t>(caps.numElements) + 1;
        const std::size_t nR = static_cast<std::size_t>(caps.numReactions) + 1;
        const std::size_t nP = static_cast<std::size_t>(caps.numPreEffects) + 1;

        e.v.assign(nE, 0);
        e.lastHitH.assign(nE, 0.f);
        e.lastEvalH.assign(nE, 0.f);
        e.blockUntilH.assign(nE, 0.f);
        e.blockUntilRtS.assign(nE, 0.0);
        e.effMult.assign(nE, 1.0);

        e.reactCdRtS.assign(nR, 0.0);
        e.reactCdH.assign(nR, 0.f);
        e.inReaction.assign(nR, 0u);

        e.preActive.assign(nP, 0u);
        e.preIntensity.assign(nP, 0.f);
        e.preExpireRtS.assign(nP, 0.0);
        e.preExpireH.assign(nP, 0.f);

        e.effDirty = true;
        e.sized = true;

        e.presentMask = 0;
        e.presentList.clear();
        e.sumAll = 0;
        e.posInList.assign(nE, 0xFFFF);
    }

    struct DecaySnapshot {
        float ratePerHour;
        float graceHours;
    };

    inline DecaySnapshot SnapshotDecay() {
        const float ts = Timescale();
        DecaySnapshot s{kRealDecayPerSec * 3600.0f / ts, (kGraceSec * ts) / 3600.0f};
        return s;
    }

    inline ERF_ElementHandle handleFromIndex(std::size_t idx) { return static_cast<ERF_ElementHandle>(idx); }

    inline void makePresent(Entry& e, std::size_t i) {
        if (i < firstIndex()) return;
        if (i >= e.posInList.size()) return;
        if (e.posInList[i] != 0xFFFF) return;
        const ERF_ElementHandle h = handleFromIndex(i);

        if (const auto bit = static_cast<unsigned>(h - 1); bit < 64) e.presentMask |= (UINT64_C(1) << bit);
        e.posInList[i] = static_cast<std::uint16_t>(e.presentList.size());
        e.presentList.push_back(h);
    }
    inline void dropPresent(Entry& e, std::size_t i) {
        if (i < firstIndex()) return;
        if (i >= e.posInList.size()) return;
        auto pos = e.posInList[i];
        if (pos == 0xFFFF) return;
        const auto lastIdx = static_cast<std::uint16_t>(e.presentList.size() - 1);
        const ERF_ElementHandle h = handleFromIndex(i);

        if (const auto bit = static_cast<unsigned>(h - 1); bit < 64) e.presentMask &= ~(UINT64_C(1) << bit);
        if (pos != lastIdx) {
            const ERF_ElementHandle lastH = e.presentList[lastIdx];
            e.presentList[pos] = lastH;
            const std::size_t lastI = idx(lastH);
            if (lastI < e.posInList.size()) e.posInList[lastI] = pos;
        }
        e.presentList.pop_back();
        e.posInList[i] = 0xFFFF;
    }
    inline void onValChange(Entry& e, std::size_t i, int before, int after) {
        if (i < firstIndex()) return;
        e.sumAll += (after - before);
        if (e.sumAll < 0) e.sumAll = 0;
        const bool was = (before > 0);
        const bool is = (after > 0);
        if (was == is) return;
        if (is)
            makePresent(e, i);
        else
            dropPresent(e, i);
    }

    inline void tickOne(Entry& e, std::size_t i, float nowH, const DecaySnapshot& snap) {
        auto& val = e.v[i];
        auto& eval = e.lastEvalH[i];
        const float hit = e.lastHitH[i];

        if (val == 0) {
            eval = nowH;
            return;
        }

        const float rate = snap.ratePerHour;
        if (rate <= 0.f) {
            eval = nowH;
            return;
        }

        const float graceEnd = hit + snap.graceHours;
        if (nowH <= graceEnd) return;

        const float startH = std::max(eval, graceEnd);
        const float elapsedH = nowH - startH;
        if (elapsedH <= 0.f) return;

        const float decF = elapsedH * rate;
        const auto decI = static_cast<int>(decF);
        if (decI <= 0) return;

        const auto before = static_cast<int>(val);
        int next = before - decI;
        if (next < 0) next = 0;
        val = static_cast<std::uint8_t>(next);
        if (next != before) onValChange(e, i, before, next);

        const float rem = decF - static_cast<float>(decI);
        eval = nowH - (rem / rate);
    }

    inline void tickAll(Entry& e, float nowH, const DecaySnapshot& snap) {
        const auto n = e.v.size();
        for (std::size_t i = firstIndex(); i < n; ++i) {
            tickOne(e, i, nowH, snap);
        }
    }

    inline void rebuildPresence(Entry& e) {
        e.presentMask = 0;
        e.presentList.clear();
        e.sumAll = 0;
        e.posInList.assign(e.v.size(), 0xFFFF);
        for (std::size_t i = firstIndex(); i < e.v.size(); ++i) {
            const int v = e.v[i];
            if (v > 0) {
                e.sumAll += v;
                makePresent(e, i);
            }
        }
    }
}

namespace {
    thread_local std::vector<std::uint32_t> TL_vals32;
    thread_local std::vector<std::uint32_t> TL_cols32;
    thread_local std::vector<const char*> TL_accumIcons;
    thread_local std::vector<ERF_ElementHandle> TL_elemsNZ;

    static std::vector<std::uint32_t> g_colorLUT;

    inline double NowRealSeconds() {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        return std::chrono::duration<double>(clock::now() - t0).count();
    }

    inline int SumAll(const Gauges::Entry& e) {
        int s = 0;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) s += e.v[i];
        return s;
    }

    static inline void ApplyElementLocksForReaction(Gauges::Entry& e, const std::vector<ERF_ElementHandle>& elems,
                                                    float nowH, float seconds, bool isRealTime) {
        if (seconds <= 0.f) return;

        const double nowRt = NowRealSeconds();
        const double untilRt = nowRt + seconds;
        const float untilH = nowH + static_cast<float>(seconds / 3600.0);

        for (ERF_ElementHandle h : elems) {
            const std::size_t idx = Gauges::idx(h);
            if (idx >= e.v.size()) continue;
            if (isRealTime) {
                e.blockUntilRtS[idx] = std::max(e.blockUntilRtS[idx], untilRt);
            } else {
                e.blockUntilH[idx] = std::max(e.blockUntilH[idx], untilH);
            }
        }
    }

    static inline void SetReactionCooldown(Gauges::Entry& e, ERF_ReactionHandle rh, float nowH, float cooldownSeconds,
                                           bool cooldownIsRealTime) {
        if (cooldownSeconds <= 0.f || rh == 0) return;

        const std::size_t ri = Gauges::idxReact(rh);

        if (cooldownIsRealTime) {
            const double untilRt = NowRealSeconds() + static_cast<double>(cooldownSeconds);
            if (ri < e.reactCdRtS.size()) e.reactCdRtS[ri] = std::max(e.reactCdRtS[ri], untilRt);
        } else {
            const float untilH = nowH + (cooldownSeconds / 3600.0f);
            if (ri < e.reactCdH.size()) e.reactCdH[ri] = std::max(e.reactCdH[ri], untilH);
        }
    }

    static bool TriggerReaction(RE::Actor* a, Gauges::Entry& e, ERF_ElementHandle elem) {
        if (!a) {
            return false;
        }
        if (e.sumAll <= 0) {
            return false;
        }

        auto const& RR = ReactionRegistry::get();

        int maxCount = ERF::GetConfig().maxReactionsPerTrigger.load(std::memory_order_relaxed);
        if (maxCount < 1) {
            maxCount = 1;
        }

        std::vector<ERF_PickBestInfo> picks;
        picks.reserve(static_cast<std::size_t>(maxCount));

        if (elem == 0) {
            const int sum = e.sumAll;
            if (sum <= 0 || e.presentList.empty()) {
                return false;
            }

            const float invSum = 1.0f / static_cast<float>(sum);

            RR.pickBestFastMulti(e.v, e.presentList, sum, invSum, maxCount, picks);
            if (picks.empty()) {
                return false;
            }

            const float nowH = NowHours();

            bool clearedAll = false;
            bool any = false;

            for (const auto& info : picks) {
                const ERF_ReactionHandle rh = info.handle;
                if (!rh) {
                    continue;
                }

                const ERF_ReactionDesc* r = RR.get(rh);
                if (!r) {
                    continue;
                }

                if (r->clearAllOnTrigger && !clearedAll) {
                    for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
                        e.v[i] = 0;
                        e.lastHitH[i] = nowH;
                        e.lastEvalH[i] = nowH;
                    }
                    e.presentMask = 0;
                    e.presentList.clear();
                    e.sumAll = 0;
                    e.posInList.assign(e.v.size(), 0xFFFF);
                    clearedAll = true;
                }

                ApplyElementLocksForReaction(e, r->elements, nowH, r->elementLockoutSeconds,
                                             r->elementLockoutIsRealTime);

                SetReactionCooldown(e, rh, nowH, r->cooldownSeconds, r->cooldownIsRealTime);

                const float durS =
                    (r->elementLockoutSeconds > 0.f) ? r->elementLockoutSeconds : std::max(0.5f, r->cooldownSeconds);
                const bool durRT =
                    (r->elementLockoutSeconds > 0.f) ? r->elementLockoutIsRealTime : r->cooldownIsRealTime;

                InjectHUD::BeginReaction(a, rh, durS, durRT);

                if (r->cb) {
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        RE::ActorHandle h = a->CreateRefHandle();
                        auto cb = r->cb;
                        void* user = r->user;
                        tasks->AddTask([h, cb, user]() {
                            if (auto actorNi = h.get()) {
                                if (auto* target = actorNi.get()) {
                                    ERF_ReactionContext ctx{};
                                    ctx.target = target;
                                    cb(ctx, user);
                                }
                            }
                        });
                    } else {
                        ERF_ReactionContext ctx{};
                        ctx.target = a;
                        r->cb(ctx, r->user);
                    }
                }

                any = true;
            }

            return any;
        }

        const std::size_t idx = Gauges::idx(elem);
        const int v = (idx < e.v.size()) ? static_cast<int>(e.v[idx]) : 0;
        if (v <= 0) {
            return false;
        }

        std::vector<std::uint8_t> totals(e.v.size(), 0);
        totals[idx] = static_cast<std::uint8_t>(v);

        std::vector<ERF_ElementHandle> present;
        present.reserve(1);
        present.push_back(elem);

        const int sum = v;
        const float invSum = 1.0f / static_cast<float>(sum);

        RR.pickBestFastMulti(totals, std::span<const ERF_ElementHandle>(present.data(), present.size()), sum, invSum,
                             maxCount, picks);
        if (picks.empty()) {
            return false;
        }

        const float nowH = NowHours();
        const int beforeVal = v;
        bool clearedElem = false;
        bool any = false;

        for (const auto& info : picks) {
            const ERF_ReactionHandle rh = info.handle;
            if (!rh) {
                continue;
            }

            const ERF_ReactionDesc* r = RR.get(rh);
            if (!r) {
                continue;
            }

            if (r->clearAllOnTrigger && !clearedElem) {
                e.v[idx] = 0;
                e.lastHitH[idx] = nowH;
                e.lastEvalH[idx] = nowH;
                Gauges::onValChange(e, idx, beforeVal, 0);
                clearedElem = true;
            }

            ApplyElementLocksForReaction(e, r->elements, nowH, r->elementLockoutSeconds, r->elementLockoutIsRealTime);

            SetReactionCooldown(e, rh, nowH, r->cooldownSeconds, r->cooldownIsRealTime);

            const float durS =
                (r->elementLockoutSeconds > 0.f) ? r->elementLockoutSeconds : std::max(0.5f, r->cooldownSeconds);
            const bool durRT = (r->elementLockoutSeconds > 0.f) ? r->elementLockoutIsRealTime : r->cooldownIsRealTime;

            InjectHUD::BeginReaction(a, rh, durS, durRT);

            if (r->cb) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    RE::ActorHandle h = a->CreateRefHandle();
                    auto cb = r->cb;
                    void* user = r->user;
                    tasks->AddTask([h, cb, user]() {
                        if (auto actorNi = h.get()) {
                            if (auto* target = actorNi.get()) {
                                ERF_ReactionContext ctx{};
                                ctx.target = target;
                                cb(ctx, user);
                            }
                        }
                    });
                } else {
                    ERF_ReactionContext ctx{};
                    ctx.target = a;
                    r->cb(ctx, r->user);
                }
            }

            any = true;
        }

        return any;
    }

    static bool MaybeTriggerPreEffectsFor(RE::Actor* a, Gauges::Entry& e, ERF_ElementHandle elem,
                                          std::uint8_t gaugeNow) {
        if (!a || elem == 0) return false;

        const auto list = PreEffectRegistry::get().listByElement(elem);
        if (list.empty()) return false;

        const double nowRt = NowRealSeconds();
        const float nowH = NowHours();

        bool any = false;

        for (auto ph : list) {
            const auto* pd = PreEffectRegistry::get().get(ph);
            if (!pd || pd->element != elem) continue;

            const std::size_t pi = Gauges::idxPre(ph);
            if (pi >= e.preActive.size()) continue;

            if (const bool above = (gaugeNow >= pd->minGauge); !above) {
                if (e.preActive[pi]) {
                    e.preActive[pi] = 0u;
                    e.preIntensity[pi] = 0.f;
                    e.preExpireRtS[pi] = 0.0;
                    e.preExpireH[pi] = 0.f;

                    if (pd->cb) {
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            RE::ActorHandle h = a->CreateRefHandle();
                            auto cb = pd->cb;
                            auto user = pd->user;
                            const auto passElem = elem;
                            const auto passGauge = gaugeNow;
                            tasks->AddTask([h, cb, user, passElem, passGauge]() {
                                if (auto actorPtr = h.get().get()) {
                                    cb(actorPtr, passElem, passGauge, 0.0f, user);
                                }
                            });
                        } else {
                            pd->cb(a, elem, gaugeNow, 0.0f, pd->user);
                        }
                    }
                    any = true;
                }
                continue;
            }

            float intensity = pd->baseIntensity + pd->scalePerPoint * static_cast<float>(gaugeNow - pd->minGauge);
            if (intensity < pd->minIntensity) intensity = pd->minIntensity;
            if (intensity > pd->maxIntensity) intensity = pd->maxIntensity;

            bool needApply = false;

            if (!e.preActive[pi]) {
                needApply = true;
            } else {
                if (const float last = e.preIntensity[pi]; std::fabs(last - intensity) > 1e-3f) {
                    needApply = true;
                }

                if (!needApply && pd->durationSeconds > 0.0f) {
                    constexpr double marginRt = 0.20;
                    constexpr float marginH = 0.20f / 3600.0f;
                    if (pd->durationIsRealTime) {
                        if (nowRt + marginRt >= e.preExpireRtS[pi]) needApply = true;
                    } else {
                        if (nowH + marginH >= e.preExpireH[pi]) needApply = true;
                    }
                }
            }

            if (!needApply) continue;

            if (pd->cb) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    RE::ActorHandle h = a->CreateRefHandle();
                    auto cb = pd->cb;
                    auto user = pd->user;
                    const auto passElem = elem;
                    const auto passGauge = gaugeNow;
                    const auto passIntensity = intensity;
                    tasks->AddTask([h, cb, user, passElem, passGauge, passIntensity]() {
                        if (auto actorPtr = h.get().get()) {
                            cb(actorPtr, passElem, passGauge, passIntensity, user);
                        }
                    });
                } else {
                    pd->cb(a, elem, gaugeNow, intensity, pd->user);
                }
            }

            e.preActive[pi] = 1u;
            e.preIntensity[pi] = intensity;

            if (pd->durationSeconds > 0.0f) {
                if (pd->durationIsRealTime) {
                    const double untilRt = nowRt + static_cast<double>(pd->durationSeconds);
                    e.preExpireRtS[pi] = std::max(e.preExpireRtS[pi], untilRt);
                } else {
                    const float untilH = nowH + static_cast<float>(pd->durationSeconds / 3600.0);
                    e.preExpireH[pi] = std::max(e.preExpireH[pi], untilH);
                }
            }

            any = true;
        }

        return any;
    }

    inline void RecomputeEffMultipliers(RE::Actor* a, Gauges::Entry& e) {
        const auto begin = Gauges::firstIndex();
        const auto n = e.v.size();

        std::fill(e.effMult.begin() + begin, e.effMult.begin() + n, 1.0);

        if (!a) {
            e.effDirty = false;
            return;
        }

        for (std::size_t i = begin; i < n; ++i) {
            const auto elem = static_cast<ERF_ElementHandle>(i);
            const double m = ElementalStates::GetGaugeMultiplierFor(a, elem);
            e.effMult[i] = m;
        }

        e.effDirty = false;
    }

    std::span<const std::uint32_t> GetColorLUT() {
        return std::span<const std::uint32_t>(g_colorLUT.data(), g_colorLUT.size());
    }
}

namespace {
    static bool Save(SKSE::SerializationInterface* ser, bool dryRun) {
        const auto& m = Gauges::state();
        if (dryRun) {
            return !m.empty();
        }

        const std::uint32_t count = static_cast<std::uint32_t>(m.size());
        if (!ser->WriteRecordData(&count, sizeof(count))) return false;

        auto writeVecU8 = [&](const std::vector<std::uint8_t>& v) {
            const std::uint32_t n = static_cast<std::uint32_t>(v.size());
            return ser->WriteRecordData(&n, sizeof(n)) && (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
        };
        auto writeVecF = [&](const std::vector<float>& v) {
            const std::uint32_t n = static_cast<std::uint32_t>(v.size());
            return ser->WriteRecordData(&n, sizeof(n)) && (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
        };
        auto writeVecD = [&](const std::vector<double>& v) {
            const std::uint32_t n = static_cast<std::uint32_t>(v.size());
            return ser->WriteRecordData(&n, sizeof(n)) && (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
        };

        for (const auto& [id, e] : m) {
            if (!ser->WriteRecordData(&id, sizeof(id))) return false;

            if (!writeVecU8(e.v)) return false;
            if (!writeVecF(e.lastHitH)) return false;
            if (!writeVecF(e.lastEvalH)) return false;
            if (!writeVecF(e.blockUntilH)) return false;
            if (!writeVecD(e.blockUntilRtS)) return false;

            if (!writeVecD(e.reactCdRtS)) return false;
            if (!writeVecF(e.reactCdH)) return false;
            if (!writeVecU8(e.inReaction)) return false;

            if (!writeVecU8(e.preActive)) return false;
            if (!writeVecF(e.preIntensity)) return false;
            if (!writeVecD(e.preExpireRtS)) return false;
            if (!writeVecF(e.preExpireH)) return false;
        }
        return true;
    }

    bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t) {
        if (version != Gauges::kVersion) return false;

        auto& m = Gauges::state();
        m.clear();

        std::uint32_t count{};
        if (!ser->ReadRecordData(&count, sizeof(count))) return false;

        auto readVecU8 = [&](std::vector<std::uint8_t>& v) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            v.resize(n);
            return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
        };
        auto readVecF = [&](std::vector<float>& v) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            v.resize(n);
            return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
        };
        auto readVecD = [&](std::vector<double>& v) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            v.resize(n);
            return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
        };

        const auto& caps = ERF::API::Caps();
        const std::size_t nE = static_cast<std::size_t>(caps.numElements) + 1;
        const std::size_t nR = static_cast<std::size_t>(caps.numReactions) + 1;
        const std::size_t nP = static_cast<std::size_t>(caps.numPreEffects) + 1;

        auto padU8 = [&](std::vector<std::uint8_t>& v, std::size_t need) {
            if (v.size() < need) v.resize(need, 0u);
        };
        auto padF = [&](std::vector<float>& v, std::size_t need) {
            if (v.size() < need) v.resize(need, 0.f);
        };
        auto padD = [&](std::vector<double>& v, std::size_t need) {
            if (v.size() < need) v.resize(need, 0.0);
        };

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldID{}, newID{};
            if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;

            if (!ser->ResolveFormID(oldID, newID)) {
                std::vector<std::uint8_t> u8;
                std::vector<float> f;
                std::vector<double> d;
                if (!readVecU8(u8)) return false;
                if (!readVecF(f)) return false;
                if (!readVecF(f)) return false;
                if (!readVecF(f)) return false;
                if (!readVecD(d)) return false;

                if (!readVecD(d)) return false;
                if (!readVecF(f)) return false;
                if (!readVecU8(u8)) return false;

                if (!readVecU8(u8)) return false;
                if (!readVecF(f)) return false;
                if (!readVecD(d)) return false;
                if (!readVecF(f)) return false;
                continue;
            }

            Gauges::Entry e{};

            if (!readVecU8(e.v)) return false;
            if (!readVecF(e.lastHitH)) return false;
            if (!readVecF(e.lastEvalH)) return false;
            if (!readVecF(e.blockUntilH)) return false;
            if (!readVecD(e.blockUntilRtS)) return false;

            if (!readVecD(e.reactCdRtS)) return false;
            if (!readVecF(e.reactCdH)) return false;
            if (!readVecU8(e.inReaction)) return false;

            if (!readVecU8(e.preActive)) return false;
            if (!readVecF(e.preIntensity)) return false;
            if (!readVecD(e.preExpireRtS)) return false;
            if (!readVecF(e.preExpireH)) return false;

            padU8(e.v, nE);
            padF(e.lastHitH, nE);
            padF(e.lastEvalH, nE);
            padF(e.blockUntilH, nE);
            padD(e.blockUntilRtS, nE);

            padD(e.reactCdRtS, nR);
            padF(e.reactCdH, nR);
            padU8(e.inReaction, nR);

            padU8(e.preActive, nP);
            padF(e.preIntensity, nP);
            padD(e.preExpireRtS, nP);
            padF(e.preExpireH, nP);

            e.effMult.assign(nE, 1.0);
            e.effDirty = true;
            e.sized = true;

            Gauges::rebuildPresence(e);

            auto [it, ins] = m.try_emplace(newID, std::move(e));
            if (!ins) it->second = std::move(e);
        }
        return true;
    }

    void Revert() { Gauges::state().clear(); }
}

void ElementalGauges::Add(RE::Actor* a, ERF_ElementHandle elem, int delta) {
    if (!a || delta <= 0) return;

    auto& M = Gauges::state();
    auto [__it, __inserted] = M.try_emplace(a->GetFormID());
    auto& e = __it->second;
    if (__inserted) Gauges::initEntryDenseIfNeeded(e);

    const auto i = Gauges::idx(elem);
    const float nowH = NowHours();
    const double nowRt = NowRealSeconds();

    const auto snap = Gauges::SnapshotDecay();
    Gauges::tickAll(e, nowH, snap);

    if (nowH < e.blockUntilH[i] || nowRt < e.blockUntilRtS[i]) {
        return;
    }
    if (e.effDirty) {
        RecomputeEffMultipliers(a, e);
    }

    const int before = e.v[i];
    const float mult = (a->IsPlayerRef() ? ERF::GetConfig().playerMult.load(std::memory_order_relaxed)
                                         : ERF::GetConfig().npcMult.load(std::memory_order_relaxed));
    const double scaled = std::round(static_cast<double>(delta) * mult * e.effMult[i]);
    const int adj = (scaled <= 0.0) ? 0 : static_cast<int>(scaled);
    const int afterI = std::clamp(before + adj, 0, 100);

    const int sumBefore = e.sumAll;
    e.v[i] = static_cast<std::uint8_t>(afterI);
    e.lastHitH[i] = nowH;
    e.lastEvalH[i] = nowH;
    onValChange(e, i, before, afterI);

    if (ERF::GetConfig().hudEnabled.load(std::memory_order_relaxed)) {
        HUD::StartHUDTick();
    }

    if (const bool singleMode = ERF::GetConfig().isSingle.load(std::memory_order_relaxed); singleMode) {
        if (before < 100 && afterI >= 100) {
            TriggerReaction(a, e, elem);
        }
    } else {
        if (sumBefore < 100 && e.sumAll >= 100) {
            TriggerReaction(a, e, 0);
        }
    }
    (void)MaybeTriggerPreEffectsFor(a, e, elem, static_cast<std::uint8_t>(afterI));
}

std::uint8_t ElementalGauges::Get(RE::Actor* a, ERF_ElementHandle elem) {
    if (!a) return 0;
    auto it = Gauges::state().find(a->GetFormID());
    if (it == Gauges::state().end()) return 0;
    auto& e = it->second;
    if (!e.sized) Gauges::initEntryDenseIfNeeded(e);

    const auto i = Gauges::idx(elem);
    const auto snap = Gauges::SnapshotDecay();
    Gauges::tickOne(e, i, NowHours(), snap);
    return e.v[i];
}

void ElementalGauges::Set(RE::Actor* a, ERF_ElementHandle elem, std::uint8_t value) {
    if (!a || elem == 0) return;

    auto& M = Gauges::state();
    auto [__it, __inserted] = M.try_emplace(a->GetFormID());
    auto& e = __it->second;
    if (__inserted) Gauges::initEntryDenseIfNeeded(e);

    const std::size_t i = Gauges::idx(elem);
    const float nowH = NowHours();

    const auto snap = Gauges::SnapshotDecay();
    Gauges::tickAll(e, nowH, snap);

    const auto afterDecay = static_cast<int>(e.v[i]);

    if (const auto afterSet = static_cast<int>(clamp100(value)); afterSet != afterDecay) {
        e.v[i] = static_cast<std::uint8_t>(afterSet);
        Gauges::onValChange(e, i, afterDecay, afterSet);
    } else {
        e.v[i] = static_cast<std::uint8_t>(afterDecay);
    }

    e.lastHitH[i] = nowH;
    e.lastEvalH[i] = nowH;
}

void ElementalGauges::Clear(RE::Actor* a) {
    if (!a) return;
    auto& m = Gauges::state();
    m.erase(a->GetFormID());
}

void ElementalGauges::ForEachDecayed(const std::function<void(RE::FormID, TotalsView)>& fn) {
    auto& m = Gauges::state();
    const float nowH = NowHours();
    const double nowRt = NowRealSeconds();

    const auto snap = Gauges::SnapshotDecay();
    for (auto it = m.begin(); it != m.end();) {
        auto& e = it->second;

        Gauges::tickAll(e, nowH, snap);

        const std::size_t beginE = Gauges::firstIndex();
        const std::size_t nE = e.v.size();
        const std::size_t countE = (nE > beginE) ? (nE - beginE) : 0;

        bool anyElemLock = false;
        for (std::size_t i = beginE; i < nE && !anyElemLock; ++i) {
            const bool lockH = (i < e.blockUntilH.size()) && (e.blockUntilH[i] > nowH);
            const bool lockRt = (i < e.blockUntilRtS.size()) && (e.blockUntilRtS[i] > nowRt);
            anyElemLock = lockH || lockRt;
        }

        bool anyReactCd = false;
        const std::size_t beginR = Gauges::firstIndex();
        if (!anyReactCd && !e.reactCdH.empty()) {
            const std::size_t nR = e.reactCdH.size();
            for (std::size_t ri = beginR; ri < nR; ++ri) {
                if (e.reactCdH[ri] > nowH) {
                    anyReactCd = true;
                    break;
                }
            }
        }
        if (!anyReactCd && !e.reactCdRtS.empty()) {
            const std::size_t nR = e.reactCdRtS.size();
            for (std::size_t ri = beginR; ri < nR; ++ri) {
                if (e.reactCdRtS[ri] > nowRt) {
                    anyReactCd = true;
                    break;
                }
            }
        }

        bool anyReactFlag = false;
        if (!e.inReaction.empty()) {
            const std::size_t nR = e.inReaction.size();
            for (std::size_t ri = beginR; ri < nR; ++ri) {
                if (e.inReaction[ri] != 0) {
                    anyReactFlag = true;
                    break;
                }
            }
        }

        if (const bool anyVal = (e.sumAll > 0); !anyVal && !anyElemLock && !anyReactCd && !anyReactFlag) {
            it = m.erase(it);
            continue;
        }

        const std::span<const std::uint8_t> spanVals{e.v.data() + beginE, countE};
        TotalsView view{spanVals};
        fn(it->first, view);

        ++it;
    }
}

std::optional<ElementalGauges::HudGaugeBundle> ElementalGauges::PickHudDecayed(RE::FormID id, double nowRt,
                                                                               float nowH) {
    auto& m = Gauges::state();
    auto it = m.find(id);
    if (it == m.end()) return std::nullopt;

    auto& e = it->second;

    const auto snap = Gauges::SnapshotDecay();
    Gauges::tickAll(e, nowH, snap);

    HudGaugeBundle bundle{};

    TL_vals32.clear();
    TL_cols32.clear();
    TL_accumIcons.clear();
    TL_elemsNZ.clear();
    TL_vals32.reserve(e.presentList.size());
    TL_cols32.reserve(e.presentList.size());
    TL_elemsNZ.reserve(e.presentList.size());

    const auto colors = GetColorLUT();
    const std::size_t beginE = Gauges::firstIndex();

    int newSum = 0;
    for (ERF_ElementHandle h : e.presentList) {
        const std::size_t i = Gauges::idx(h);
        if (i >= e.v.size()) continue;
        const std::uint8_t vv = e.v[i];
        if (vv == 0) continue;

        newSum += vv;
        TL_vals32.push_back(static_cast<std::uint32_t>(std::min<int>(vv, 100)));
        TL_elemsNZ.push_back(h);

        const std::size_t colorIdx = i;
        const std::uint32_t rgb = (colorIdx < colors.size()) ? colors[colorIdx] : 0xFFFFFFu;
        TL_cols32.push_back(rgb);
    }

    bundle.values = std::span<const std::uint32_t>(TL_vals32.data(), TL_vals32.size());
    bundle.colors = std::span<const std::uint32_t>(TL_cols32.data(), TL_cols32.size());

    if (newSum == 0) {
        const bool anyElemLock = std::any_of(e.blockUntilH.begin() + std::min(beginE, e.blockUntilH.size()),
                                             e.blockUntilH.end(), [&](float x) { return x > nowH; }) ||
                                 std::any_of(e.blockUntilRtS.begin() + std::min(beginE, e.blockUntilRtS.size()),
                                             e.blockUntilRtS.end(), [&](double x) { return x > nowRt; });

        bool anyReactCd = false;
        if (!e.reactCdH.empty()) {
            for (std::size_t ri = Gauges::firstIndex(); ri < e.reactCdH.size(); ++ri) {
                if (e.reactCdH[ri] > nowH) {
                    anyReactCd = true;
                    break;
                }
            }
        }
        if (!anyReactCd && !e.reactCdRtS.empty()) {
            for (std::size_t ri = Gauges::firstIndex(); ri < e.reactCdRtS.size(); ++ri) {
                if (e.reactCdRtS[ri] > nowRt) {
                    anyReactCd = true;
                    break;
                }
            }
        }

        bool anyReactFlag = false;
        if (!e.inReaction.empty()) {
            for (std::size_t ri = Gauges::firstIndex(); ri < e.inReaction.size(); ++ri) {
                if (e.inReaction[ri] != 0) {
                    anyReactFlag = true;
                    break;
                }
            }
        }

        if (!anyElemLock && !anyReactCd && !anyReactFlag) {
            m.erase(it);
        }
        bundle.icons = std::span<const char* const>();
        return std::nullopt;
    }

    auto const& RR = ReactionRegistry::get();
    if (const bool singleMode = ERF::GetConfig().isSingle.load(std::memory_order_relaxed); singleMode) {
        TL_accumIcons.reserve(TL_vals32.size());
        thread_local std::vector<std::uint8_t> TL_totals;

        if (TL_totals.size() < e.v.size()) TL_totals.resize(e.v.size());
        TL_totals.assign(e.v.size(), 0);
        for (std::size_t k = 0; k < TL_vals32.size(); ++k) {
            const auto elemH = TL_elemsNZ[k];
            const auto idx = Gauges::idx(elemH);

            std::fill(TL_totals.begin(), TL_totals.end(), 0);
            TL_totals[idx] = static_cast<std::uint8_t>(std::min<std::uint32_t>(TL_vals32[k], 100));

            const int sum = TL_totals[idx];
            if (sum <= 0) {
                TL_accumIcons.push_back(nullptr);
                continue;
            }

            const float inv = 1.0f / static_cast<float>(sum);
            std::array<ERF_ElementHandle, 1> present{elemH};
            auto best = RR.pickBestFast(TL_totals, {present.data(), present.size()}, sum, inv);
            TL_accumIcons.push_back(best && best->icon ? best->icon : nullptr);
        }
    } else {
        const int sumAll = newSum;
        const float invAll = 1.0f / static_cast<float>(sumAll);
        auto best = RR.pickBestFast(e.v, e.presentList, sumAll, invAll);
        TL_accumIcons.push_back(best && best->icon ? best->icon : nullptr);
    }

    bundle.icons = std::span<const char* const>(TL_accumIcons.data(), TL_accumIcons.size());

    return bundle;
}

void ElementalGauges::RegisterStore() { Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Save, &Load, &Revert}); }

void ElementalGauges::InvalidateStateMultipliers(RE::Actor* a) {
    if (!a) return;
    auto& m = Gauges::state();
    const auto it = m.find(a->GetFormID());
    if (it == m.end()) return;
    it->second.effDirty = true;
}

void ElementalGauges::BuildColorLUTOnce() {
    auto const& ER = ElementRegistry::get();
    const std::size_t n = ER.size() + 1;
    g_colorLUT.resize(n, 0xFFFFFFu);
    for (std::size_t i = 1; i < n; ++i) {
        if (const ERF_ElementDesc* d = ER.get(static_cast<ERF_ElementHandle>(i))) {
            g_colorLUT[i] = d->colorRGB;
        }
    }
}