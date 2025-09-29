#include "ElementalGauges.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/Helpers.h"
#include "../common/PluginSerialization.h"
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

        std::unordered_map<ERF_ReactionHandle, double> reactBlockUntilRtS;
        std::unordered_map<ERF_ReactionHandle, float> reactBlockUntilH;
        std::unordered_map<ERF_ReactionHandle, std::uint8_t> inReaction;
        std::unordered_set<ERF_PreEffectHandle> preActive;
        std::unordered_map<ERF_PreEffectHandle, float> preLastIntensity;
        std::unordered_map<ERF_PreEffectHandle, double> preExpireRtS;
        std::unordered_map<ERF_PreEffectHandle, float> preExpireH;
    };

    using Map = std::unordered_map<RE::FormID, Entry>;

    inline Map& state() noexcept {
        static Map m;  // NOSONAR estado gerenciado de forma centralizada
        return m;
    }

    inline std::size_t idx(ERF_ElementHandle h) { return static_cast<std::size_t>(h == 0 ? 1 : h); }

    inline std::size_t firstIndex() { return kReserveZero ? 1u : 0u; }

    inline std::size_t elemCount() { return ElementRegistry::get().size() + 1; }

    inline void ensureSized(Entry& e) {
        const auto need = elemCount();
        if (e.v.size() < need) {
            e.v.resize(need, 0);
            e.lastHitH.resize(need, 0.f);
            e.lastEvalH.resize(need, 0.f);
            e.blockUntilH.resize(need, 0.f);
            e.blockUntilRtS.resize(need, 0.0);
        }
    }

    inline void tickOne(Entry& e, std::size_t i, float nowH) {
        if (i >= e.v.size()) return;

        auto& val = e.v[i];
        auto& eval = e.lastEvalH[i];
        const float hit = e.lastHitH[i];

        if (val == 0) {
            eval = nowH;
            return;
        }

        const float rate = DecayPerGameHour();
        if (rate <= 0.f) {
            eval = nowH;
            return;
        }

        const float graceEnd = hit + GraceGameHours();
        if (nowH <= graceEnd) return;

        const float startH = std::max(eval, graceEnd);
        const float elapsedH = nowH - startH;
        if (elapsedH <= 0.f) return;

        const float decF = elapsedH * rate;
        const auto decI = static_cast<int>(decF);
        if (decI <= 0) return;

        int next = static_cast<int>(val) - decI;
        if (next < 0) next = 0;
        val = static_cast<std::uint8_t>(next);

        const float rem = decF - static_cast<float>(decI);
        eval = nowH - (rem / rate);
    }

    inline void tickAll(Entry& e, float nowH) {
        ensureSized(e);
        const auto n = e.v.size();
        for (std::size_t i = firstIndex(); i < n; ++i) {
            tickOne(e, i, nowH);
        }
    }

    static int AdjustByStates(RE::Actor* a, ERF_ElementHandle h, int delta) {
        if (!a || delta <= 0) return delta;

        const ERF_ElementDesc* ed = ElementRegistry::get().get(h);
        if (!ed || ed->stateMultipliers.empty()) return delta;

        double f = 1.0;

        for (const auto& [sh, mult] : ed->stateMultipliers) {
            if (sh == 0) continue;
            if (ElementalStates::IsActive(a, sh)) {
                f *= mult;
            }
        }

        const double out = std::round(static_cast<double>(delta) * f);
        if (out <= 0.0) return 0;
        return static_cast<int>(out);
    }
}

namespace {
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
        if (cooldownSeconds <= 0.f) return;

        const double nowRt = NowRealSeconds();
        const double untilRt = nowRt + cooldownSeconds;
        const float untilH = nowH + static_cast<float>(cooldownSeconds / 3600.0);

        if (cooldownIsRealTime) {
            e.reactBlockUntilRtS[rh] = std::max(e.reactBlockUntilRtS[rh], untilRt);
        } else {
            e.reactBlockUntilH[rh] = std::max(e.reactBlockUntilH[rh], untilH);
        }
    }

    static bool MaybeTriggerReaction(RE::Actor* a, Gauges::Entry& e) {
        if (!a) return false;

        std::vector<std::uint8_t> totals;
        totals.reserve(e.v.size() - Gauges::firstIndex());
        std::vector<ERF_ElementHandle> present;

        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            const auto v = e.v[i];
            totals.push_back(static_cast<std::uint8_t>(std::clamp<int>(int(std::round(v)), 0, 100)));
            if (v > 0) present.push_back(static_cast<ERF_ElementHandle>(i));
        }

        auto& RR = ReactionRegistry::get();
        auto rhOpt = RR.pickBest(totals, present);
        if (!rhOpt) return false;

        const ERF_ReactionHandle rh = *rhOpt;
        const ERF_ReactionDesc* r = RR.get(rh);
        if (!r) return false;

        const float nowH = NowHours();

        if (r->clearAllOnTrigger) {
            for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
                e.v[i] = 0;
                e.lastHitH[i] = nowH;
                e.lastEvalH[i] = nowH;
            }
        }

        ApplyElementLocksForReaction(e, r->elements, nowH, r->elementLockoutSeconds, r->elementLockoutIsRealTime);

        SetReactionCooldown(e, rh, nowH, r->cooldownSeconds, r->cooldownIsRealTime);

        float durS = (r->elementLockoutSeconds > 0.f) ? r->elementLockoutSeconds : std::max(0.5f, r->cooldownSeconds);
        bool durIsRT = (r->elementLockoutSeconds > 0.f) ? r->elementLockoutIsRealTime : r->cooldownIsRealTime;
        InjectHUD::BeginReaction(a, rh, durS, durIsRT);

        if (r->cb && a) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                RE::ActorHandle h = a->CreateRefHandle();
                auto cb = r->cb;
                void* user = r->user;
                tasks->AddTask([h, cb, user]() {
                    if (auto actor = h.get().get()) cb(actor, user);
                });
            } else {
                r->cb(a, r->user);
            }
        }

        return true;
    }

    static bool MaybeTriggerPreEffectsFor(RE::Actor* a, Gauges::Entry& e, ERF_ElementHandle elem,
                                          std::uint8_t gaugeNow) {
        if (!a || elem == 0) return false;

        auto list = PreEffectRegistry::get().listByElement(elem);
        if (list.empty()) return false;

        const double nowRt = NowRealSeconds();
        const float nowH = NowHours();

        bool any = false;

        for (auto ph : list) {
            const auto* pd = PreEffectRegistry::get().get(ph);
            if (!pd || pd->element != elem) continue;

            const bool above = gaugeNow >= pd->minGauge;

            // Se caiu abaixo do limiar e estava ativo: remover
            if (!above) {
                if (e.preActive.erase(ph) > 0) {
                    e.preLastIntensity.erase(ph);
                    e.preExpireRtS.erase(ph);
                    e.preExpireH.erase(ph);
                    if (pd->cb) {
                        // Convencione: intensidade 0 => remover/dispell
                        pd->cb(a, elem, gaugeNow, 0.0f, pd->user);
                    }
                    any = true;
                }
                continue;
            }

            // Calcula intensidade pela regra do descriptor
            float intensity = pd->baseIntensity + pd->scalePerPoint * static_cast<float>(gaugeNow - pd->minGauge);
            if (intensity < pd->minIntensity) intensity = pd->minIntensity;
            if (intensity > pd->maxIntensity) intensity = pd->maxIntensity;

            // Decidir se precisa aplicar/atualizar
            bool needApply = false;

            // 1) nunca esteve ativo
            if (!e.preActive.count(ph)) {
                needApply = true;
            } else {
                // 2) mudou materialmente a intensidade
                auto it = e.preLastIntensity.find(ph);
                const float last = (it != e.preLastIntensity.end()) ? it->second : -9999.0f;
                if (std::abs(last - intensity) > 1e-3f) {
                    needApply = true;
                }

                // 3) vai expirar logo (caso você use duração finita no Apply)
                if (!needApply && pd->durationSeconds > 0.0f) {
                    const double marginRt = 0.20;  // renova 0.2s antes
                    const float marginH = 0.20f / 3600.0f;
                    if (pd->durationIsRealTime) {
                        auto ex = e.preExpireRtS.find(ph);
                        if (ex != e.preExpireRtS.end() && nowRt + marginRt >= ex->second) needApply = true;
                    } else {
                        auto ex = e.preExpireH.find(ph);
                        if (ex != e.preExpireH.end() && nowH + marginH >= ex->second) needApply = true;
                    }
                }
            }

            if (!needApply) continue;

            // Aplica/atualiza o efeito contínuo
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

            // Marca ativo e renova “expiração” se você usa duração no Apply
            e.preActive.insert(ph);
            e.preLastIntensity[ph] = intensity;

            if (pd->durationSeconds > 0.0f) {
                if (pd->durationIsRealTime) {
                    e.preExpireRtS[ph] = std::max(e.preExpireRtS[ph], nowRt + pd->durationSeconds);
                } else {
                    e.preExpireH[ph] =
                        std::max(e.preExpireH[ph], nowH + static_cast<float>(pd->durationSeconds / 3600.0));
                }
            }

            any = true;
        }

        return any;
    }
}

namespace {
    bool Save(SKSE::SerializationInterface* ser) {
        const auto& m = Gauges::state();
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

        auto writeMapD = [&](const std::unordered_map<ERF_ReactionHandle, double>& mp) {
            const std::uint32_t n = static_cast<std::uint32_t>(mp.size());
            if (!ser->WriteRecordData(&n, sizeof(n))) return false;
            for (const auto& [k, v] : mp) {
                if (!ser->WriteRecordData(&k, sizeof(k))) return false;
                if (!ser->WriteRecordData(&v, sizeof(v))) return false;
            }
            return true;
        };
        auto writeMapF = [&](const std::unordered_map<ERF_ReactionHandle, float>& mp) {
            const std::uint32_t n = static_cast<std::uint32_t>(mp.size());
            if (!ser->WriteRecordData(&n, sizeof(n))) return false;
            for (const auto& [k, v] : mp) {
                if (!ser->WriteRecordData(&k, sizeof(k))) return false;
                if (!ser->WriteRecordData(&v, sizeof(v))) return false;
            }
            return true;
        };
        auto writeMapU8 = [&](const std::unordered_map<ERF_ReactionHandle, std::uint8_t>& mp) {
            const std::uint32_t n = static_cast<std::uint32_t>(mp.size());
            if (!ser->WriteRecordData(&n, sizeof(n))) return false;
            for (const auto& [k, v] : mp) {
                if (!ser->WriteRecordData(&k, sizeof(k))) return false;
                if (!ser->WriteRecordData(&v, sizeof(v))) return false;
            }
            return true;
        };

        for (const auto& [id, e] : m) {
            if (!ser->WriteRecordData(&id, sizeof(id))) return false;

            if (!writeVecU8(e.v)) return false;
            if (!writeVecF(e.lastHitH)) return false;
            if (!writeVecF(e.lastEvalH)) return false;
            if (!writeVecF(e.blockUntilH)) return false;
            if (!writeVecD(e.blockUntilRtS)) return false;

            if (!writeMapD(e.reactBlockUntilRtS)) return false;
            if (!writeMapF(e.reactBlockUntilH)) return false;
            if (!writeMapU8(e.inReaction)) return false;
        }
        return true;
    }

    // ======== LOAD ========
    bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t) {
        if (version > Gauges::kVersion) return false;
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

        auto readMapD = [&](std::unordered_map<ERF_ReactionHandle, double>& mp) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            mp.clear();
            mp.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                ERF_ReactionHandle k{};
                double v{};
                if (!ser->ReadRecordData(&k, sizeof(k))) return false;
                if (!ser->ReadRecordData(&v, sizeof(v))) return false;
                mp.emplace(k, v);
            }
            return true;
        };
        auto readMapF = [&](std::unordered_map<ERF_ReactionHandle, float>& mp) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            mp.clear();
            mp.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                ERF_ReactionHandle k{};
                float v{};
                if (!ser->ReadRecordData(&k, sizeof(k))) return false;
                if (!ser->ReadRecordData(&v, sizeof(v))) return false;
                mp.emplace(k, v);
            }
            return true;
        };
        auto readMapU8 = [&](std::unordered_map<ERF_ReactionHandle, std::uint8_t>& mp) -> bool {
            std::uint32_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            mp.clear();
            mp.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                ERF_ReactionHandle k{};
                std::uint8_t v{};
                if (!ser->ReadRecordData(&k, sizeof(k))) return false;
                if (!ser->ReadRecordData(&v, sizeof(v))) return false;
                mp.emplace(k, v);
            }
            return true;
        };

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldID{}, newID{};
            if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;
            if (!ser->ResolveFormID(oldID, newID)) {
                Gauges::Entry dummy{};
                if (!readVecU8(dummy.v)) return false;
                if (!readVecF(dummy.lastHitH)) return false;
                if (!readVecF(dummy.lastEvalH)) return false;
                if (!readVecF(dummy.blockUntilH)) return false;
                if (!readVecD(dummy.blockUntilRtS)) return false;
                if (version >= 3) {
                    if (!readMapD(dummy.reactBlockUntilRtS)) return false;
                    if (!readMapF(dummy.reactBlockUntilH)) return false;
                    if (!readMapU8(dummy.inReaction)) return false;
                }
                continue;
            }

            Gauges::Entry e{};

            if (!readVecU8(e.v)) return false;
            if (!readVecF(e.lastHitH)) return false;
            if (!readVecF(e.lastEvalH)) return false;
            if (!readVecF(e.blockUntilH)) return false;
            if (!readVecD(e.blockUntilRtS)) return false;

            if (version >= 3) {
                if (!readMapD(e.reactBlockUntilRtS)) return false;
                if (!readMapF(e.reactBlockUntilH)) return false;
                if (!readMapU8(e.inReaction)) return false;
            } else {
                e.reactBlockUntilRtS.clear();
                e.reactBlockUntilH.clear();
                e.inReaction.clear();
            }

            m[newID] = std::move(e);
        }
        return true;
    }

    void Revert() { Gauges::state().clear(); }
}

void ElementalGauges::Add(RE::Actor* a, ERF_ElementHandle elem, int delta) {
    if (!a || delta <= 0) return;

    auto& e = Gauges::state()[a->GetFormID()];
    Gauges::ensureSized(e);

    const auto i = Gauges::idx(elem);
    const float nowH = NowHours();
    const double nowRt = NowRealSeconds();

    Gauges::tickAll(e, nowH);

    if (i < e.blockUntilH.size() && (nowH < e.blockUntilH[i] || nowRt < e.blockUntilRtS[i])) {
        return;
    }

    const int before = (i < e.v.size()) ? e.v[i] : 0;
    const int adj = Gauges::AdjustByStates(a, elem, delta);
    const int afterI = std::clamp(before + adj, 0, 100);

    if (i < e.v.size()) {
        const int sumBefore = SumAll(e);
        e.v[i] = static_cast<std::uint8_t>(afterI);
        e.lastHitH[i] = nowH;
        e.lastEvalH[i] = nowH;

        (void)MaybeTriggerPreEffectsFor(a, e, elem, static_cast<std::uint8_t>(afterI));
        const int sumAfter = SumAll(e);
        if (sumBefore < 100 && sumAfter >= 100 && (MaybeTriggerReaction(a, e))) return;
    }
}

std::uint8_t ElementalGauges::Get(RE::Actor* a, ERF_ElementHandle elem) {
    if (!a) return 0;
    auto& m = Gauges::state();
    auto it = m.find(a->GetFormID());
    if (it == m.end()) return 0;

    auto& e = it->second;
    Gauges::ensureSized(e);

    const auto i = Gauges::idx(elem);
    if (i >= e.v.size()) return 0;

    Gauges::tickOne(e, i, NowHours());
    return e.v[i];
}

void ElementalGauges::Set(RE::Actor* a, ERF_ElementHandle elem, std::uint8_t value) {
    if (!a) return;
    auto& e = Gauges::state()[a->GetFormID()];
    Gauges::ensureSized(e);

    const auto i = Gauges::idx(elem);
    if (i >= e.v.size()) return;

    const float nowH = NowHours();
    Gauges::tickOne(e, i, nowH);

    e.v[i] = clamp100(value);
    e.lastEvalH[i] = nowH;
}

void ElementalGauges::Clear(RE::Actor* a) {
    if (!a) return;
    auto& m = Gauges::state();
    m.erase(a->GetFormID());
}

void ElementalGauges::ForEachDecayed(const std::function<void(RE::FormID, const Totals&)>& fn) {
    auto& m = Gauges::state();
    const float nowH = NowHours();
    const double nowRt = NowRealSeconds();

    for (auto it = m.begin(); it != m.end();) {
        auto& e = it->second;
        Gauges::tickAll(e, nowH);

        bool anyVal = false;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            if (e.v[i] > 0) {
                anyVal = true;
                break;
            }
        }

        bool anyElemLock = false;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            if (e.blockUntilH[i] > nowH || e.blockUntilRtS[i] > nowRt) {
                anyElemLock = true;
                break;
            }
        }

        bool anyReactCd = false, anyReactFlag = false;
        for (const auto& kv : e.reactBlockUntilH) {
            if (kv.second > nowH) {
                anyReactCd = true;
                break;
            }
        }
        if (!anyReactCd)
            for (const auto& kv : e.reactBlockUntilRtS) {
                if (kv.second > nowRt) {
                    anyReactCd = true;
                    break;
                }
            }
        for (const auto& kv : e.inReaction) {
            if (kv.second != 0) {
                anyReactFlag = true;
                break;
            }
        }

        Totals t{};
        if (e.v.size() > Gauges::firstIndex()) t.values.reserve(e.v.size() - Gauges::firstIndex());
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) t.values.push_back(e.v[i]);

        if (anyVal) {
            fn(it->first, t);
            ++it;
        } else if (anyElemLock || anyReactCd || anyReactFlag) {
            std::fill(t.values.begin(), t.values.end(), 0);
            fn(it->first, t);
            ++it;
        } else {
            it = m.erase(it);
        }
    }
}

std::optional<ElementalGauges::HudGaugeBundle> ElementalGauges::PickHudIconDecayed(RE::FormID id) {
    auto& m = Gauges::state();
    auto it = m.find(id);
    if (it == m.end()) return std::nullopt;

    auto& e = it->second;
    const float nowH = NowHours();
    Gauges::tickAll(e, nowH);

    HudGaugeBundle bundle{};
    auto& R = ElementRegistry::get();

    // monta valores/cores para o anel
    for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
        const std::uint8_t v = e.v[i];
        bundle.values.push_back(static_cast<std::uint32_t>(v));
        const ERF_ElementHandle h = static_cast<ERF_ElementHandle>(i);
        if (const ERF_ElementDesc* d = R.get(h))
            bundle.colors.push_back(d->colorRGB);
        else
            bundle.colors.push_back(0xFFFFFFu);
    }

    const bool anyVal = std::any_of(bundle.values.begin(), bundle.values.end(), [](std::uint32_t v) { return v > 0; });

    if (!anyVal) {
        const double nowRt = NowRealSeconds();

        const bool anyElemLock = std::any_of(e.blockUntilH.begin() + Gauges::firstIndex(), e.blockUntilH.end(),
                                             [&](float x) { return x > nowH; }) ||
                                 std::any_of(e.blockUntilRtS.begin() + Gauges::firstIndex(), e.blockUntilRtS.end(),
                                             [&](double x) { return x > nowRt; });

        bool anyReactCd = false;
        for (const auto& kv : e.reactBlockUntilH) {
            if (kv.second > nowH) {
                anyReactCd = true;
                break;
            }
        }
        if (!anyReactCd) {
            for (const auto& kv : e.reactBlockUntilRtS) {
                if (kv.second > nowRt) {
                    anyReactCd = true;
                    break;
                }
            }
        }

        bool anyReactFlag = false;
        for (const auto& kv : e.inReaction) {
            if (kv.second != 0) {
                anyReactFlag = true;
                break;
            }
        }

        if (!anyElemLock && !anyReactCd && !anyReactFlag) m.erase(it);
        return std::nullopt;
    }

    // Escolhe melhor reação para sugerir ícone no HUD
    std::vector<std::uint8_t> totals8;
    totals8.reserve(e.v.size() - Gauges::firstIndex());
    std::vector<ERF_ElementHandle> present;
    present.reserve(e.v.size() - Gauges::firstIndex());

    for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
        const auto v = e.v[i];
        totals8.push_back(v);
        if (v > 0) present.push_back(static_cast<ERF_ElementHandle>(i));
    }

    auto& RR = ReactionRegistry::get();
    if (auto rh = RR.pickBestForHud(totals8, present)) {
        if (const ERF_ReactionDesc* rd = RR.get(*rh)) {
            if (!rd->hud.iconPath.empty()) {
                bundle.iconPath = rd->hud.iconPath;  // << só path
                bundle.iconTint = rd->hud.iconTint;
                return bundle;
            }
        }
    }

    return std::nullopt;
}

void ElementalGauges::RegisterStore() { Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Save, &Load, &Revert}); }
