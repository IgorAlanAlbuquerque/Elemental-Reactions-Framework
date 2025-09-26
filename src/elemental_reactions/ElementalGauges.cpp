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
#include "ElementalStates.h"

using namespace ElementalGaugesDecay;
using Elem = ERF_ElementHandle;

namespace {
    constexpr std::size_t ComboIndex(ElementalGauges::Combo c) noexcept {
        return static_cast<std::size_t>(std::to_underlying(c));
    }

    constexpr std::size_t kComboCount = ComboIndex(ElementalGauges::Combo::_COUNT);
}

namespace Gauges {
    inline constexpr std::uint32_t kRecordID = FOURCC('G', 'A', 'U', 'V');
    inline constexpr std::uint32_t kVersion = 2;
    inline constexpr float increaseMult = 1.30f;
    inline constexpr float decreaseMult = 0.10f;
    constexpr bool kReserveZero = true;

    struct Entry {
        std::vector<std::uint8_t> v;
        std::vector<float> lastHitH;
        std::vector<float> lastEvalH;
        std::vector<float> blockUntilH;
        std::vector<double> blockUntilRtS;

        std::array<float, kComboCount> comboBlockUntilH{};     // TODO: migrar p/ mapa por ComboKey
        std::array<double, kComboCount> comboBlockUntilRtS{};  // TODO: migrar p/ mapa por ComboKey
        std::array<std::uint8_t, kComboCount> inCombo{};       // TODO: migrar p/ mapa por ComboKey
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
        if (!ed) return delta;

        double f = 1.0;
        for (const auto& [flag, mult] : ed->stateMultipliers) {
            if (ElementalStates::Get(a, flag)) {
                f *= mult;
            }
        }

        const double out = std::round(static_cast<double>(delta) * f);
        return static_cast<int>(out < 0.0 ? 0.0 : out);
    }
}

namespace {
    std::array<ElementalGauges::SumComboTrigger, (std::size_t)ElementalGauges::Combo::_COUNT> g_onCombo{};  // NOSONAR

    inline double NowRealSeconds() {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        return std::chrono::duration<double>(clock::now() - t0).count();
    }

    struct FFFIdx {
        std::size_t fire{std::numeric_limits<std::size_t>::max()};
        std::size_t frost{std::numeric_limits<std::size_t>::max()};
        std::size_t shock{std::numeric_limits<std::size_t>::max()};
        bool ok() const {
            return fire != std::numeric_limits<std::size_t>::max() &&
                   frost != std::numeric_limits<std::size_t>::max() && shock != std::numeric_limits<std::size_t>::max();
        }
    };

    inline FFFIdx ResolveFFFIdx() {
        FFFIdx out{};
        const auto& R = ElementRegistry::get();
        if (auto h = R.findByName("Fire")) out.fire = Gauges::idx(*h);
        if (auto h = R.findByName("Frost")) out.frost = Gauges::idx(*h);
        if (auto h = R.findByName("Shock")) out.shock = Gauges::idx(*h);
        return out;
    }

    inline const FFFIdx& FFF() {
        static FFFIdx cache{};
        static std::size_t lastSize = 0;
        const auto cur = ElementRegistry::get().size();
        if (cur != lastSize) {
            cache = ResolveFFFIdx();
            lastSize = cur;
        }
        return cache;
    }

    template <class Fn>
    void ForEachElementInCombo(ElementalGauges::Combo c, Fn&& fn) {
        const auto& f = FFF();
        if (!f.ok()) return;
        using enum ElementalGauges::Combo;
        switch (c) {
            case Fire:
                fn(f.fire);
                break;
            case Frost:
                fn(f.frost);
                break;
            case Shock:
                fn(f.shock);
                break;
            case FireFrost:
                fn(f.fire);
                fn(f.frost);
                break;
            case FrostFire:
                fn(f.frost);
                fn(f.fire);
                break;
            case FireShock:
                fn(f.fire);
                fn(f.shock);
                break;
            case ShockFire:
                fn(f.shock);
                fn(f.fire);
                break;
            case FrostShock:
                fn(f.frost);
                fn(f.shock);
                break;
            case ShockFrost:
                fn(f.shock);
                fn(f.frost);
                break;
            case FireFrostShock:
                fn(f.fire);
                fn(f.frost);
                fn(f.shock);
                break;
            default:
                break;
        }
    }

    inline void ApplyElementLockout(Gauges::Entry& e, ElementalGauges::Combo which,
                                    const ElementalGauges::SumComboTrigger& cfg, float nowH) {
        if (cfg.elementLockoutSeconds <= 0.0f) return;
        const double untilRt = NowRealSeconds() + cfg.elementLockoutSeconds;
        const float untilH = nowH + static_cast<float>(cfg.elementLockoutSeconds / 3600.0);

        ForEachElementInCombo(which, [&](std::size_t idxDyn) {
            if (idxDyn >= e.v.size()) return;
            if (cfg.elementLockoutIsRealTime)
                e.blockUntilRtS[idxDyn] = std::max(e.blockUntilRtS[idxDyn], untilRt);
            else
                e.blockUntilH[idxDyn] = std::max(e.blockUntilH[idxDyn], untilH);
        });
    }

    inline void DispatchCombo(RE::Actor* a, ElementalGauges::Combo which, const ElementalGauges::SumComboTrigger& cfg) {
        if (!cfg.cb || !a) return;
        if (cfg.deferToTask) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                RE::ActorHandle h = a->CreateRefHandle();
                auto cb = cfg.cb;
                void* user = cfg.user;
                tasks->AddTask([h, cb, user, which]() {
                    if (auto actor = h.get().get()) cb(actor, which, user);
                });
                return;
            }
        }
        cfg.cb(a, which, cfg.user);
    }

    inline std::array<size_t, 3> rank3(const std::array<std::uint8_t, 3>& fff) {
        std::array<size_t, 3> idx{0, 1, 2};
        std::ranges::sort(idx, std::greater<>{}, [&](size_t i) { return fff[i]; });
        return idx;
    }

    constexpr ElementalGauges::Combo makeSolo(std::size_t i) noexcept {
        using enum ElementalGauges::Combo;
        switch (i) {
            case 0:
                return Fire;
            case 1:
                return Frost;
            case 2:
                return Shock;
            default:
                return Fire;
        }
    }

    inline ElementalGauges::Combo makePairDirectional(size_t aFFS, size_t bFFS) {
        using enum ElementalGauges::Combo;
        if (aFFS == 0 && bFFS == 1) return FireFrost;
        if (aFFS == 1 && bFFS == 0) return FrostFire;
        if (aFFS == 0 && bFFS == 2) return FireShock;
        if (aFFS == 2 && bFFS == 0) return ShockFire;
        if (aFFS == 1 && bFFS == 2) return FrostShock;
        return ShockFrost;
    }

    inline const ElementalGauges::SumComboTrigger& PickRules() {
        using enum ElementalGauges::Combo;
        const ElementalGauges::SumComboTrigger* sel = nullptr;
        if (g_onCombo[ComboIndex(FireFrostShock)].cb)
            sel = &g_onCombo[ComboIndex(FireFrostShock)];
        else if (g_onCombo[ComboIndex(Fire)].cb)
            sel = &g_onCombo[ComboIndex(Fire)];
        else if (g_onCombo[ComboIndex(Frost)].cb)
            sel = &g_onCombo[ComboIndex(Frost)];
        else if (g_onCombo[ComboIndex(Shock)].cb)
            sel = &g_onCombo[ComboIndex(Shock)];
        static ElementalGauges::SumComboTrigger defaults{};
        return sel ? *sel : defaults;
    }

    inline int SumAll(const Gauges::Entry& e) {
        int s = 0;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) s += e.v[i];
        return s;
    }

    inline std::array<std::uint8_t, 3> TotFFF(const Gauges::Entry& e) {
        const auto& f = FFF();
        std::array<std::uint8_t, 3> out{0, 0, 0};
        if (!f.ok()) return out;
        if (f.fire < e.v.size()) out[0] = e.v[f.fire];
        if (f.frost < e.v.size()) out[1] = e.v[f.frost];
        if (f.shock < e.v.size()) out[2] = e.v[f.shock];
        return out;
    }

    inline std::optional<ElementalGauges::Combo> ChooseCombo(const Gauges::Entry& e) {
        if (const int sumAll = SumAll(e); sumAll < 100) return std::nullopt;

        auto fff = TotFFF(e);
        const int sumFFS = int(fff[0]) + int(fff[1]) + int(fff[2]);

        if (sumFFS <= 0) return std::nullopt;

        const auto& rules = PickRules();
        const float p0 = fff[0] / float(sumFFS);
        const float p1 = fff[1] / float(sumFFS);
        const float p2 = fff[2] / float(sumFFS);

        if (p0 >= rules.majorityPct) return ElementalGauges::Combo::Fire;
        if (p1 >= rules.majorityPct) return ElementalGauges::Combo::Frost;
        if (p2 >= rules.majorityPct) return ElementalGauges::Combo::Shock;

        if (fff[0] > 0 && fff[1] > 0 && fff[2] > 0) {
            const float minPct = std::min({p0, p1, p2});
            if (minPct >= rules.tripleMinPct) return ElementalGauges::Combo::FireFrostShock;
        }

        auto ord = rank3(fff);
        return makePairDirectional(ord[0], ord[1]);
    }

    bool MaybeSumComboReact(RE::Actor* a, Gauges::Entry& e) {
        auto whichOpt = ChooseCombo(e);
        if (!whichOpt) return false;
        const auto which = *whichOpt;

        const auto ci = static_cast<std::size_t>(std::to_underlying(which));
        const auto& cfg = g_onCombo[ci];
        if (!cfg.cb) return false;

        const float nowH = NowHours();
        const double nowRt = NowRealSeconds();

        if (const bool cooldownHit = (!cfg.cooldownIsRealTime && nowH < e.comboBlockUntilH[ci]) ||
                                     (cfg.cooldownIsRealTime && nowRt < e.comboBlockUntilRtS[ci]);
            cooldownHit)
            return false;

        if (!a || a->IsDead()) return false;

        if (cfg.cooldownSeconds > 0.f) {
            if (cfg.cooldownIsRealTime)
                e.comboBlockUntilRtS[ci] = std::max(e.comboBlockUntilRtS[ci], nowRt + cfg.cooldownSeconds);
            else
                e.comboBlockUntilH[ci] = std::max(e.comboBlockUntilH[ci], nowH + (cfg.cooldownSeconds / 3600.f));
        }

        ApplyElementLockout(e, which, cfg, nowH);

        if (cfg.clearAllOnTrigger) {
            for (std::size_t k = Gauges::firstIndex(); k < e.v.size(); ++k) {
                e.v[k] = 0;
                e.lastHitH[k] = nowH;
                e.lastEvalH[k] = nowH;
            }
        }

        DispatchCombo(a, which, cfg);
        return true;
    }

    constexpr std::uint32_t kFire = 0xF04A3A;
    constexpr std::uint32_t kFrost = 0x4FB2FF;
    constexpr std::uint32_t kShock = 0xFFD02A;
    constexpr std::uint32_t kNeutral = 0xFFFFFF;

    constexpr std::uint32_t kFireFrost_RedBias = 0xE65ACF;
    constexpr std::uint32_t kFrostFire_BlueBias = 0x7A73FF;

    constexpr std::uint32_t kFireShock_RedBias = 0xFF8A2A;
    constexpr std::uint32_t kShockFire_YelBias = 0xF6B22E;

    constexpr std::uint32_t kFrostShock_BlueBias = 0x49C9F0;
    constexpr std::uint32_t kShockFrost_YelBias = 0xB8E34D;

    constexpr std::uint32_t kTripleMix = 0xFFF0CC;

    inline std::uint32_t TintForIndex(ElementalGauges::Combo c) {
        using enum ElementalGauges::Combo;
        std::uint32_t tint = kNeutral;
        switch (c) {
            case Fire:
                tint = kFire;
                break;
            case Frost:
                tint = kFrost;
                break;
            case Shock:
                tint = kShock;
                break;

            case FireFrost:
                tint = kFireFrost_RedBias;
                break;
            case FrostFire:
                tint = kFrostFire_BlueBias;
                break;

            case FireShock:
                tint = kFireShock_RedBias;
                break;
            case ShockFire:
                tint = kShockFire_YelBias;
                break;

            case FrostShock:
                tint = kFrostShock_BlueBias;
                break;
            case ShockFrost:
                tint = kShockFrost_YelBias;
                break;

            case FireFrostShock:
                tint = kTripleMix;
                break;

            default:
                tint = kNeutral;
                break;
        }
        return tint;
    }

    inline ElementalGauges::HudIcon IconForCombo(ElementalGauges::Combo c) {
        using enum ElementalGauges::HudIcon;
        using ElementalGauges::Combo;
        ElementalGauges::HudIcon icon = Fire;
        switch (c) {
            case Combo::Fire:
                icon = Fire;
                break;
            case Combo::Frost:
                icon = Frost;
                break;
            case Combo::Shock:
                icon = Shock;
                break;
            case Combo::FireFrost:
            case Combo::FrostFire:
                icon = FireFrost;
                break;
            case Combo::FireShock:
            case Combo::ShockFire:
                icon = FireShock;
                break;
            case Combo::FrostShock:
            case Combo::ShockFrost:
                icon = FrostShock;
                break;
            case Combo::FireFrostShock:
                icon = FireFrostShock;
                break;
            default:
                icon = Fire;
                break;
        }
        return icon;
    }

    inline std::optional<ElementalGauges::Combo> ChooseHudCombo(const std::array<std::uint8_t, 3>& fff) {
        using enum ElementalGauges::Combo;
        const int sumFFS = int(fff[0]) + int(fff[1]) + int(fff[2]);
        if (sumFFS <= 0) return std::nullopt;

        const auto& rules = PickRules();
        const float p0 = fff[0] / float(sumFFS);
        const float p1 = fff[1] / float(sumFFS);
        const float p2 = fff[2] / float(sumFFS);

        if (p0 >= rules.majorityPct) return Fire;
        if (p1 >= rules.majorityPct) return Frost;
        if (p2 >= rules.majorityPct) return Shock;

        if (fff[0] > 0 && fff[1] > 0 && fff[2] > 0) {
            const float minPct = std::min({p0, p1, p2});
            if (minPct >= rules.tripleMinPct) return FireFrostShock;
        }

        auto ord = rank3(fff);
        return makePairDirectional(ord[0], ord[1]);
    }

    static inline std::uint8_t to_u8_100(float x) {
        auto r = (x <= 0.f) ? 0 : (x >= 100.f) ? 100 : static_cast<std::uint8_t>(x + 0.5f);
        return r;
    }
}

namespace {
    bool Save(SKSE::SerializationInterface* ser) {
        const auto& m = Gauges::state();
        auto count = static_cast<std::uint32_t>(m.size());
        if (!ser->WriteRecordData(&count, sizeof(count))) return false;

        for (const auto& [id, e] : m) {
            if (!ser->WriteRecordData(&id, sizeof(id))) return false;

            auto writeVecU8 = [&](const std::vector<std::uint8_t>& v) {
                auto n = static_cast<std::uint32_t>(v.size());
                return ser->WriteRecordData(&n, sizeof(n)) &&
                       (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
            };
            auto writeVecF = [&](const std::vector<float>& v) {
                auto n = static_cast<std::uint32_t>(v.size());
                return ser->WriteRecordData(&n, sizeof(n)) &&
                       (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
            };
            auto writeVecD = [&](const std::vector<double>& v) {
                auto n = static_cast<std::uint32_t>(v.size());
                return ser->WriteRecordData(&n, sizeof(n)) &&
                       (n == 0 || ser->WriteRecordData(v.data(), n * sizeof(v[0])));
            };

            if (!writeVecU8(e.v)) return false;
            if (!writeVecF(e.lastHitH)) return false;
            if (!writeVecF(e.lastEvalH)) return false;
            if (!writeVecF(e.blockUntilH)) return false;
            if (!writeVecD(e.blockUntilRtS)) return false;

            if (!ser->WriteRecordData(e.comboBlockUntilH.data(), sizeof(e.comboBlockUntilH))) return false;
            if (!ser->WriteRecordData(e.comboBlockUntilRtS.data(), sizeof(e.comboBlockUntilRtS))) return false;
            if (!ser->WriteRecordData(e.inCombo.data(), sizeof(e.inCombo))) return false;
        }
        return true;
    }

    bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t /*length*/) {
        if (version != Gauges::kVersion) return false;
        auto& m = Gauges::state();
        m.clear();

        std::uint32_t count{};
        if (!ser->ReadRecordData(&count, sizeof(count))) return false;

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldID{};
            RE::FormID newID{};
            if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;
            if (!ser->ResolveFormID(oldID, newID)) continue;

            Gauges::Entry e{};

            auto readVecU8 = [&](std::vector<std::uint8_t>& v) {
                std::uint32_t n{};
                if (!ser->ReadRecordData(&n, sizeof(n))) return false;
                v.resize(n);
                return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
            };
            auto readVecF = [&](std::vector<float>& v) {
                std::uint32_t n{};
                if (!ser->ReadRecordData(&n, sizeof(n))) return false;
                v.resize(n);
                return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
            };
            auto readVecD = [&](std::vector<double>& v) {
                std::uint32_t n{};
                if (!ser->ReadRecordData(&n, sizeof(n))) return false;
                v.resize(n);
                return n == 0 || ser->ReadRecordData(v.data(), n * sizeof(v[0]));
            };

            if (!readVecU8(e.v)) return false;
            if (!readVecF(e.lastHitH)) return false;
            if (!readVecF(e.lastEvalH)) return false;
            if (!readVecF(e.blockUntilH)) return false;
            if (!readVecD(e.blockUntilRtS)) return false;

            if (!ser->ReadRecordData(e.comboBlockUntilH.data(), sizeof(e.comboBlockUntilH))) return false;
            if (!ser->ReadRecordData(e.comboBlockUntilRtS.data(), sizeof(e.comboBlockUntilRtS))) return false;
            if (!ser->ReadRecordData(e.inCombo.data(), sizeof(e.inCombo))) return false;

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

    Gauges::tickAll(e, nowH);

    if (i < e.blockUntilH.size() && (nowH < e.blockUntilH[i] || NowRealSeconds() < e.blockUntilRtS[i])) {
        return;
    }

    const int before = (i < e.v.size()) ? e.v[i] : 0;
    const int adj = Gauges::AdjustByStates(a, elem, delta);
    const int after_i = std::min(100, std::max(0, before + adj));

    if (i < e.v.size()) {
        const int sumBefore = SumAll(e);
        e.v[i] = static_cast<std::uint8_t>(after_i);
        e.lastHitH[i] = nowH;
        e.lastEvalH[i] = nowH;

        const int sumAfter = SumAll(e);
        if (sumBefore < 100 && sumAfter >= 100 && (MaybeSumComboReact(a, e))) return;
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
    const auto id = a->GetFormID();
    auto& m = Gauges::state();
    m.erase(id);
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

        // locks/combos?
        bool anyElemLock = false;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            if (e.blockUntilH[i] > nowH || e.blockUntilRtS[i] > nowRt) {
                anyElemLock = true;
                break;
            }
        }
        bool anyComboCd = false, anyComboFlag = false;
        for (std::size_t ci = 0; ci < kComboCount; ++ci) {
            anyComboCd |= (e.comboBlockUntilH[ci] > nowH) || (e.comboBlockUntilRtS[ci] > nowRt);
            anyComboFlag |= (e.inCombo[ci] != 0);
        }

        Totals t{};
        if (e.v.size() > Gauges::firstIndex()) t.values.reserve(e.v.size() - Gauges::firstIndex());
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) t.values.push_back(e.v[i]);

        if (anyVal) {
            fn(it->first, t);
            ++it;
        } else if (anyElemLock || anyComboCd || anyComboFlag) {
            // reporta zeros do mesmo tamanho
            std::fill(t.values.begin(), t.values.end(), 0);
            fn(it->first, t);
            ++it;
        } else {
            it = m.erase(it);
        }
    }
}

std::optional<ElementalGauges::Totals> ElementalGauges::GetTotalsDecayed(RE::FormID id) {
    auto& m = Gauges::state();
    const float nowH = NowHours();

    if (auto it = m.find(id); it != m.end()) {
        auto& e = it->second;
        Gauges::tickAll(e, nowH);

        bool anyVal = false;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            if (e.v[i] > 0) {
                anyVal = true;
                break;
            }
        }

        Totals t{};
        if (e.v.size() > Gauges::firstIndex()) t.values.reserve(e.v.size() - Gauges::firstIndex());
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) t.values.push_back(e.v[i]);

        if (anyVal) {
            return t;
        } else {
            std::fill(t.values.begin(), t.values.end(), 0);
            return t;
        }
    }
    return std::nullopt;
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

    for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
        const std::uint8_t v = e.v[i];
        bundle.values.push_back(static_cast<std::uint32_t>(v));
        const ERF_ElementHandle h = static_cast<ERF_ElementHandle>(i);
        if (const ERF_ElementDesc* d = R.get(h)) {
            bundle.colors.push_back(d->colorRGB);
        } else {
            bundle.colors.push_back(0xFFFFFFu);
        }
    }

    bool anyVal = false;
    for (auto v : bundle.values) {
        if (v > 0) {
            anyVal = true;
            break;
        }
    }

    if (!anyVal) {
        const double nowRt = NowRealSeconds();
        bool anyElemLock = false;
        for (std::size_t i = Gauges::firstIndex(); i < e.v.size(); ++i) {
            if (e.blockUntilH[i] > nowH || e.blockUntilRtS[i] > nowRt) {
                anyElemLock = true;
                break;
            }
        }
        bool anyComboCd = false, anyComboFlag = false;
        for (std::size_t ci = 0; ci < kComboCount; ++ci) {
            anyComboCd |= (e.comboBlockUntilH[ci] > nowH) || (e.comboBlockUntilRtS[ci] > nowRt);
            anyComboFlag |= (e.inCombo[ci] != 0);
        }
        if (!anyElemLock && !anyComboCd && !anyComboFlag) {
            m.erase(it);
        }
        return std::nullopt;
    }

    const auto fff = TotFFF(e);
    auto whichOpt = ChooseHudCombo(fff);
    if (!whichOpt) return std::nullopt;

    const Combo which = *whichOpt;
    bundle.iconId = static_cast<int>(std::to_underlying(IconForCombo(which)));
    bundle.iconTint = TintForIndex(which);

    return bundle;
}

void ElementalGauges::SetOnSumCombo(Combo c, const SumComboTrigger& cfg) { g_onCombo[ComboIndex(c)] = cfg; }

std::vector<std::pair<RE::FormID, ElementalGauges::Totals>> ElementalGauges::SnapshotDecayed() {
    std::vector<std::pair<RE::FormID, Totals>> out;
    out.reserve(Gauges::state().size());
    ForEachDecayed([&](RE::FormID id, const Totals& t) { out.emplace_back(id, t); });
    return out;
}

void ElementalGauges::GarbageCollectDecayed() {
    ForEachDecayed([](RE::FormID, const Totals&) {});
}

void ElementalGauges::RegisterStore() { Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Save, &Load, &Revert}); }

std::optional<ElementalGauges::ActiveComboHUD> ElementalGauges::PickActiveComboHUD(RE::FormID id) {
    auto& m = Gauges::state();
    auto it = m.find(id);
    if (it == m.end()) return std::nullopt;

    const auto& e = it->second;
    const double nowRt = NowRealSeconds();

    double bestRem = 0.0;
    std::size_t bestIdx = SIZE_MAX;
    for (std::size_t ci = 0; ci < kComboCount; ++ci) {
        const double endRt = e.comboBlockUntilRtS[ci];
        if (endRt > nowRt) {
            const double rem = endRt - nowRt;
            if (rem > bestRem) {
                bestRem = rem;
                bestIdx = ci;
            }
        }
    }
    if (bestIdx == SIZE_MAX) return std::nullopt;

    const auto which = static_cast<Combo>(bestIdx);
    const auto& cfg = g_onCombo[bestIdx];

    ActiveComboHUD hud;
    hud.which = which;
    hud.remainingRtS = bestRem;
    hud.durationRtS = std::max(0.001, double(cfg.elementLockoutSeconds));
    hud.realTime = true;

    return hud;
}