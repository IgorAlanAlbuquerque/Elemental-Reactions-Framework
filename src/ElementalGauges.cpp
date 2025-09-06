#include "ElementalGauges.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "ElementalStates.h"
#include "common/Helpers.h"
#include "common/PluginSerialization.h"

using namespace ElementalGaugesDecay;

namespace ElementalGauges {
    namespace {
        constexpr std::size_t ComboIndex(ElementalGauges::Combo c) noexcept {
            return static_cast<std::size_t>(std::to_underlying(c));
        }

        constexpr std::size_t kComboCount = ComboIndex(ElementalGauges::Combo::_COUNT);
    }

    // ============================
    // Internal Storage Gauges
    // ============================
    namespace Gauges {
        struct Entry {
            std::array<std::uint8_t, 3> v{0, 0, 0};
            std::array<float, 3> lastHitH{0, 0, 0};
            std::array<float, 3> lastEvalH{0, 0, 0};

            std::array<float, 3> blockUntilH{0, 0, 0};     // horas de jogo
            std::array<double, 3> blockUntilRtS{0, 0, 0};  // segundos reais

            // (resto já existente)
            std::array<float, kComboCount> comboBlockUntilH{};  // cooldown por combo
            std::array<double, kComboCount> comboBlockUntilRtS{};
            std::array<std::uint8_t, kComboCount> inCombo{};
        };

        using Map = std::unordered_map<RE::FormID, Entry>;

        inline Map& state() noexcept {
            static Map m;  // NOSONAR
            return m;
        }

        inline void tickOne(Entry& e, std::size_t i, float nowH) {
            auto& val = e.v[i];
            auto& eval = e.lastEvalH[i];
            const float hit = e.lastHitH[i];

            if (val == 0) {
                eval = nowH;
                return;
            }

            const float tRef = std::max(eval, hit);
            const float untilDecay = tRef + GraceGameHours();
            if (nowH <= untilDecay) return;

            const float elapsedH = nowH - untilDecay;
            const float decF = elapsedH * DecayPerGameHour();

            int next = static_cast<int>(val) - static_cast<int>(decF);
            if (next < 0) next = 0;

            val = static_cast<std::uint8_t>(next);
            eval = nowH;
        }

        inline void tickAll(Entry& e, float nowH) {
            tickOne(e, 0, nowH);
            tickOne(e, 1, nowH);
            tickOne(e, 2, nowH);
        }

        [[nodiscard]] constexpr std::size_t idx(Type t) noexcept {
            return static_cast<std::size_t>(std::to_underlying(t));
        }

        // Serialização
        inline constexpr std::uint32_t kRecordID = FOURCC('G', 'A', 'U', 'V');
        inline constexpr std::uint32_t kVersion = 1;

        // Ajuste por estados
        inline constexpr float increaseMult = 1.30f;  // +30%
        inline constexpr float decreaseMult = 0.10f;  // -90% (forte; ajuste se quiser)
        static int AdjustByStates(RE::Actor* a, Type t, int delta) {
            using enum ElementalGauges::Type;
            if (!a || delta <= 0) return delta;

            double f = 1.0;
            const bool wet = ElementalStates::Get(a, ElementalStates::Flag::Wet);
            const bool rubber = ElementalStates::Get(a, ElementalStates::Flag::Rubber);
            const bool fur = ElementalStates::Get(a, ElementalStates::Flag::Fur);

            switch (t) {
                case Fire: {
                    const bool dec = wet;
                    const bool inc = (fur || rubber);
                    if (dec)
                        f *= decreaseMult;
                    else if (inc)
                        f *= increaseMult;
                    break;
                }
                case Frost: {
                    const bool dec = fur;
                    const bool inc = (wet || rubber);
                    if (dec)
                        f *= decreaseMult;
                    else if (inc)
                        f *= increaseMult;
                    break;
                }
                case Shock: {
                    const bool dec = rubber;
                    const bool inc = (wet || fur);
                    if (dec)
                        f *= decreaseMult;
                    else if (inc)
                        f *= increaseMult;
                    break;
                }
            }
            return static_cast<int>(std::round(static_cast<double>(delta) * f));
        }
    }  // namespace Gauges

    // ============================
    // Camada de COMBOS por soma
    // ============================
    namespace {
        std::array<SumComboTrigger, (std::size_t)Combo::_COUNT> g_onCombo{};  // NOSONAR

        inline double NowRealSeconds() {
            using clock = std::chrono::steady_clock;
            static const auto t0 = clock::now();
            return std::chrono::duration<double>(clock::now() - t0).count();
        }

        void ForEachElementInCombo(ElementalGauges::Combo c, auto&& fn) {
            using enum ElementalGauges::Combo;
            switch (c) {
                case Fire:
                    fn(0);
                    break;
                case Frost:
                    fn(1);
                    break;
                case Shock:
                    fn(2);
                    break;
                case FireFrost:
                    fn(0);
                    fn(1);
                    break;
                case FrostFire:
                    fn(1);
                    fn(0);
                    break;
                case FireShock:
                    fn(0);
                    fn(2);
                    break;
                case ShockFire:
                    fn(2);
                    fn(0);
                    break;
                case FrostShock:
                    fn(1);
                    fn(2);
                    break;
                case ShockFrost:
                    fn(2);
                    fn(1);
                    break;
                case FireFrostShock:
                    fn(0);
                    fn(1);
                    fn(2);
                    break;
                default:
                    break;
            }
        }

        void ApplyElementLockout(Gauges::Entry& e, ElementalGauges::Combo which,
                                 const ElementalGauges::SumComboTrigger& cfg, float nowH) {
            if (cfg.elementLockoutSeconds <= 0.0f) return;

            const double untilRt = NowRealSeconds() + cfg.elementLockoutSeconds;
            const float untilH = nowH + static_cast<float>(cfg.elementLockoutSeconds / 3600.0);

            ForEachElementInCombo(which, [&](std::size_t idx) {
                if (cfg.elementLockoutIsRealTime)
                    e.blockUntilRtS[idx] = std::max(e.blockUntilRtS[idx], untilRt);
                else
                    e.blockUntilH[idx] = std::max(e.blockUntilH[idx], untilH);
            });
        }

        struct [[nodiscard("RAII guard: mantenha este objeto vivo para manter o lock até o fim do escopo")]] TrigGuard {
            std::uint8_t* f;
            explicit TrigGuard(std::uint8_t& x) noexcept : f(&x) { *f = 1; }
            TrigGuard(const TrigGuard&) = delete;
            TrigGuard& operator=(const TrigGuard&) = delete;
            TrigGuard(TrigGuard&&) = delete;
            TrigGuard& operator=(TrigGuard&&) = delete;
            ~TrigGuard() noexcept {
                if (f) *f = 0;
            }
        };

        void DispatchCombo(RE::Actor* a, Combo which, const SumComboTrigger& cfg) {
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

        // Ordena índices [0:FIRE,1:FROST,2:SHOCK] por valor desc
        std::array<std::size_t, 3> rank3(const std::array<std::uint8_t, 3>& v) {
            std::array<std::size_t, 3> idx{0, 1, 2};
            std::ranges::sort(idx, std::greater<>{}, [&v](std::size_t i) { return v[i]; });
            return idx;
        }

        constexpr Combo makeSolo(std::size_t i) noexcept {
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

        Combo makePairDirectional(std::size_t a, std::size_t b) {
            using enum ElementalGauges::Combo;
            // a e b são os dois maiores na ordem (a primeiro)
            if (a == 0 && b == 1) return FireFrost;
            if (a == 1 && b == 0) return FrostFire;
            if (a == 0 && b == 2) return FireShock;
            if (a == 2 && b == 0) return ShockFire;
            if (a == 1 && b == 2) return FrostShock;
            return ShockFrost;
        }

        // Pega um "conjunto de regras" global para maioria/tripleSpread (fallbacks sensatos)
        const SumComboTrigger& PickRules() {
            using enum ElementalGauges::Combo;
            if (g_onCombo[ComboIndex(FireFrostShock)].cb) return g_onCombo[ComboIndex(FireFrostShock)];
            if (g_onCombo[ComboIndex(Fire)].cb) return g_onCombo[ComboIndex(Fire)];
            if (g_onCombo[ComboIndex(Frost)].cb) return g_onCombo[ComboIndex(Frost)];
            if (g_onCombo[ComboIndex(Shock)].cb) return g_onCombo[ComboIndex(Shock)];
            static SumComboTrigger defaults{/*cb*/ nullptr,
                                            /*user*/ nullptr,
                                            /*majorityPct*/ 0.85f,
                                            /*tripleMinPct*/ 0.28f,
                                            /*cooldownSeconds*/ 0.5f,
                                            /*cooldownIsRealTime*/ true,
                                            /*deferToTask*/ true,
                                            /*clearAllOnTrigger*/ true};
            return defaults;
        }

        // Decide o combo dado os totais PLANEJADOS (após somar o delta atual)
        std::optional<Combo> ChooseCombo(const std::array<std::uint8_t, 3>& tot) {
            const int sum = int(tot[0]) + int(tot[1]) + int(tot[2]);
            if (sum < 100) return std::nullopt;

            const auto& rules = PickRules();

            const float p0 = tot[0] / float(sum);
            const float p1 = tot[1] / float(sum);
            const float p2 = tot[2] / float(sum);

            // 1) Solo se ≥ majorityPct (ex.: 85%)
            if (p0 >= rules.majorityPct) return Combo::Fire;
            if (p1 >= rules.majorityPct) return Combo::Frost;
            if (p2 >= rules.majorityPct) return Combo::Shock;

            // 2) Triplo se 3 presentes e minPct ≥ tripleMinPct (ex.: 28%)

            if (const bool allPresent = (tot[0] > 0) && (tot[1] > 0) && (tot[2] > 0); allPresent) {
                const float minPct = std::min({p0, p1, p2});
                if (minPct >= rules.tripleMinPct) return Combo::FireFrostShock;
            }

            // 3) Caso contrário: par direcional (dois maiores, ordenados)
            auto order = rank3(tot);  // order[0] ≥ order[1] ≥ order[2]
            return makePairDirectional(order[0], order[1]);
        }

        // Tenta disparar o combo (limpa gauges, cooldown, callback). Retorna true se disparou.
        bool MaybeSumComboReact(RE::Actor* a, Gauges::Entry& e, const std::array<std::uint8_t, 3>& afterTot) {
            auto whichOpt = ChooseCombo(afterTot);
            if (!whichOpt) return false;

            const auto which = *whichOpt;
            const std::size_t ci = ComboIndex(which);
            const auto& cfg = g_onCombo[ci];
            if (!cfg.cb) return false;  // combo não registrado

            const float nowH = NowHours();

            // cooldown do combo
            if (const bool cooldownHit = (!cfg.cooldownIsRealTime && nowH < e.comboBlockUntilH[ci]) ||
                                         (cfg.cooldownIsRealTime && NowRealSeconds() < e.comboBlockUntilRtS[ci]);
                cooldownHit)
                return false;

            if (e.inCombo[ci]) return false;
            TrigGuard guard{e.inCombo[ci]};

            auto* actor = a;
            if (!actor || actor->IsDead()) return false;

            if (cfg.clearAllOnTrigger) {
                e.v = {0, 0, 0};
                e.lastHitH = {nowH, nowH, nowH};
                e.lastEvalH = {nowH, nowH, nowH};
            }

            ApplyElementLockout(e, which, cfg, nowH);

            DispatchCombo(actor, which, cfg);

            if (cfg.cooldownSeconds > 0.f) {
                if (cfg.cooldownIsRealTime)
                    e.comboBlockUntilRtS[ci] = NowRealSeconds() + cfg.cooldownSeconds;
                else
                    e.comboBlockUntilH[ci] = nowH + (cfg.cooldownSeconds / 3600.0f);
            }
            return true;
        }
    }

    // ============================
    // API pública
    // ============================
    void SetOnSumCombo(Combo c, const SumComboTrigger& cfg) { g_onCombo[ComboIndex(c)] = cfg; }

    std::uint8_t Get(RE::Actor* a, Type t) {
        if (!a) return 0;
        auto& m = Gauges::state();
        const auto it = m.find(a->GetFormID());
        if (it == m.end()) return 0;
        auto& e = const_cast<Gauges::Entry&>(it->second);
        Gauges::tickOne(e, Gauges::idx(t), NowHours());
        return e.v[Gauges::idx(t)];
    }

    void Set(RE::Actor* a, Type t, std::uint8_t value) {
        if (!a) return;
        auto& e = Gauges::state()[a->GetFormID()];
        const float nowH = NowHours();
        Gauges::tickOne(e, Gauges::idx(t), nowH);
        e.v[Gauges::idx(t)] = clamp100(value);
        e.lastEvalH[Gauges::idx(t)] = nowH;
    }

    void Add(RE::Actor* a, Type t, int delta) {
        if (!a) return;
        auto& e = Gauges::state()[a->GetFormID()];
        const auto i = Gauges::idx(t);
        const float nowH = NowHours();

        Gauges::tickAll(e, nowH);

        if (nowH < e.blockUntilH[i] || NowRealSeconds() < e.blockUntilRtS[i]) {
            return;
        }

        const int before = e.v[i];
        const int adj = Gauges::AdjustByStates(a, t, delta);

        // boost de debug
        int debugBoost = 10;

        // Totais planejados APÓS o delta atual
        std::array<std::uint8_t, 3> afterTot = e.v;
        afterTot[i] = clamp100(before + adj + debugBoost);

        // Cruza <100 → ≥100? tenta combo ANTES de gravar
        const int sumBefore = int(e.v[0]) + int(e.v[1]) + int(e.v[2]);
        if (const int sumAfter = int(afterTot[0]) + int(afterTot[1]) + int(afterTot[2]);
            sumBefore < 100 && sumAfter >= 100 && MaybeSumComboReact(a, e, afterTot)) {
            return;
        }

        // Sem combo: grava valor planejado
        e.v[i] = afterTot[i];
        e.lastHitH[i] = nowH;
        e.lastEvalH[i] = nowH;
    }

    void Clear(RE::Actor* a) {
        if (!a) return;
        Gauges::state().erase(a->GetFormID());
    }

    // ============================
    // Serialização
    // ============================
    namespace {
        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = Gauges::state();

            if (const auto count = static_cast<std::uint32_t>(m.size()); !ser->WriteRecordData(&count, sizeof(count)))
                return false;

            const bool ok = std::ranges::all_of(m, [ser](const auto& kv) {
                const auto& id = kv.first;
                const auto& e = kv.second;
                const auto bytes = static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0]));
                return ser->WriteRecordData(&id, sizeof(id)) && ser->WriteRecordData(e.v.data(), bytes);
            });

            return ok;
        }

        bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t /*length*/) {
            if (version != Gauges::kVersion) return false;
            auto& m = Gauges::state();
            m.clear();

            std::uint32_t count{};
            if (!ser->ReadRecordData(&count, sizeof(count))) return false;

            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                Gauges::Entry e{};
                if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;
                if (const auto bytes = static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0]));
                    !ser->ReadRecordData(e.v.data(), bytes))
                    return false;

                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) continue;

                const float nowH = NowHours();
                e.lastHitH = {nowH, nowH, nowH};
                e.lastEvalH = {nowH, nowH, nowH};
                m[newID] = e;
            }
            return true;
        }

        void Revert() { Gauges::state().clear(); }
    }

    void RegisterStore() { Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Save, &Load, &Revert}); }
}
