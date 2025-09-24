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

#include "../common/Helpers.h"
#include "../common/PluginSerialization.h"
#include "ElementalStates.h"

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

            const float graceEnd = hit + GraceGameHours();
            if (nowH <= graceEnd) {
                return;
            }

            const float startH = std::max(eval, graceEnd);
            const float elapsedH = nowH - startH;
            const float rate = DecayPerGameHour();
            const float decF = elapsedH * rate;
            const auto decI = static_cast<int>(decF);

            if (decI <= 0) {
                return;
            }

            int next = static_cast<int>(val) - decI;
            if (next < 0) next = 0;
            val = static_cast<std::uint8_t>(next);

            const float rem = decF - decI;
            eval = nowH - (rem / rate);
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

            const double nowRt = NowRealSeconds();
            const double lockS = std::max(0.0, double(cfg.elementLockoutSeconds));
            e.inCombo[ci] = 1;

            if (cfg.elementLockoutIsRealTime)
                e.comboBlockUntilRtS[ci] = nowRt + lockS;
            else
                e.comboBlockUntilH[ci] = nowH + float(lockS / 3600.0);

            // Timers dos ELEMENTOS (o que realmente bloqueia Add()):
            if (cfg.elementLockoutIsRealTime) {
                ForEachElementInCombo(which, [&](std::size_t idx) {
                    e.blockUntilRtS[idx] = std::max(e.blockUntilRtS[idx], nowRt + lockS);
                });
            } else {
                ForEachElementInCombo(which, [&](std::size_t idx) {
                    e.blockUntilH[idx] = std::max(e.blockUntilH[idx], nowH + float(lockS / 3600.0));
                });
            }

            if (cfg.clearAllOnTrigger) {
                e.v = {0, 0, 0};
                e.lastHitH = {nowH, nowH, nowH};
                e.lastEvalH = {nowH, nowH, nowH};
            }

            ApplyElementLockout(e, which, cfg, nowH);

            DispatchCombo(actor, which, cfg);

            if (cfg.cooldownSeconds > 0.f) {
                if (cfg.cooldownIsRealTime)
                    e.comboBlockUntilRtS[ci] =
                        std::max(e.comboBlockUntilRtS[ci], NowRealSeconds() + cfg.cooldownSeconds);
                else
                    e.comboBlockUntilH[ci] = std::max(e.comboBlockUntilH[ci], nowH + (cfg.cooldownSeconds / 3600.0f));
            }
            return true;
        }

        constexpr std::uint32_t kFire = 0xF04A3A;
        constexpr std::uint32_t kFrost = 0x4FB2FF;
        constexpr std::uint32_t kShock = 0xFFD02A;
        constexpr std::uint32_t kNeutral = 0xFFFFFF;

        // Duplas – vermelho/azul/roxo bias
        constexpr std::uint32_t kFireFrost_RedBias = 0xE65ACF;
        constexpr std::uint32_t kFrostFire_BlueBias = 0x7A73FF;

        constexpr std::uint32_t kFireShock_RedBias = 0xFF8A2A;
        constexpr std::uint32_t kShockFire_YelBias = 0xF6B22E;

        constexpr std::uint32_t kFrostShock_BlueBias = 0x49C9F0;
        constexpr std::uint32_t kShockFrost_YelBias = 0xB8E34D;

        // Triplo
        constexpr std::uint32_t kTripleMix = 0xFFF0CC;

        inline std::uint32_t TintForIndex(ElementalGauges::Combo c) {
            using C = ElementalGauges::Combo;
            switch (c) {
                case C::Fire:
                    return kFire;
                case C::Frost:
                    return kFrost;
                case C::Shock:
                    return kShock;

                case C::FireFrost:
                    return kFireFrost_RedBias;  // dominante = Fire
                case C::FrostFire:
                    return kFrostFire_BlueBias;  // dominante = Frost

                case C::FireShock:
                    return kFireShock_RedBias;  // dominante = Fire
                case C::ShockFire:
                    return kShockFire_YelBias;  // dominante = Shock

                case C::FrostShock:
                    return kFrostShock_BlueBias;  // dominante = Frost
                case C::ShockFrost:
                    return kShockFrost_YelBias;  // dominante = Shock

                case C::FireFrostShock:
                    return kTripleMix;

                default:
                    return kNeutral;
            }
        }

        // Mapeia Combo -> arquivo de ícone (pares direcionais compartilham)
        inline ElementalGauges::HudIcon IconForCombo(Combo c) {
            using enum ElementalGauges::HudIcon;
            using ElementalGauges::Combo;
            switch (c) {
                case Combo::Fire:
                    return Fire;
                case Combo::Frost:
                    return Frost;
                case Combo::Shock:
                    return Shock;
                case Combo::FireFrost:
                case Combo::FrostFire:
                    return FireFrost;
                case Combo::FireShock:
                case Combo::ShockFire:
                    return FireShock;
                case Combo::FrostShock:
                case Combo::ShockFrost:
                    return FrostShock;
                case Combo::FireFrostShock:
                    return FireFrostShock;
                default:
                    return Fire;
            }
        }

        // Versão para HUD: MESMAS regras do PickRules (85% / 28%), mas sem exigir soma >= 100
        std::optional<Combo> ChooseHudCombo(const std::array<std::uint8_t, 3>& tot) {
            const int sum = int(tot[0]) + int(tot[1]) + int(tot[2]);
            if (sum <= 0) return std::nullopt;

            const auto& rules = PickRules();
            const float p0 = tot[0] / float(sum);
            const float p1 = tot[1] / float(sum);
            const float p2 = tot[2] / float(sum);

            // 1) Solo se ≥ majorityPct (ex.: 85%)
            if (p0 >= rules.majorityPct) return Combo::Fire;
            if (p1 >= rules.majorityPct) return Combo::Frost;
            if (p2 >= rules.majorityPct) return Combo::Shock;

            // 2) Triplo se os 3 presentes e minPct ≥ tripleMinPct (ex.: 28%)
            if ((tot[0] > 0) && (tot[1] > 0) && (tot[2] > 0)) {
                const float minPct = std::min({p0, p1, p2});
                if (minPct >= rules.tripleMinPct) return Combo::FireFrostShock;
            }

            // 3) Caso contrário, par direcional entre os dois maiores
            auto order = rank3(tot);  // [maior, segundo, menor]
            return makePairDirectional(order[0], order[1]);
        }

        static inline std::uint8_t to_u8_100(float x) {
            if (x <= 0.f) return 0;
            if (x >= 100.f) return 100;
            return static_cast<std::uint8_t>(x + 0.5f);
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
        int debugBoost = 25;

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

    void ForEachDecayed(const std::function<void(RE::FormID, const Totals&)>& fn) {
        auto& m = Gauges::state();
        const float nowH = NowHours();
        const double nowRt = NowRealSeconds();
        for (auto it = m.begin(); it != m.end();) {
            auto& e = it->second;
            Gauges::tickAll(e, nowH);

            const bool anyVal = (e.v[0] | e.v[1] | e.v[2]) != 0;

            const bool anyElemLock = e.blockUntilH[0] > nowH || e.blockUntilH[1] > nowH || e.blockUntilH[2] > nowH ||
                                     e.blockUntilRtS[0] > nowRt || e.blockUntilRtS[1] > nowRt ||
                                     e.blockUntilRtS[2] > nowRt;

            bool anyComboCd = false, anyComboFlag = false;
            for (size_t ci = 0; ci < kComboCount; ++ci) {
                anyComboCd |= (e.comboBlockUntilH[ci] > nowH) || (e.comboBlockUntilRtS[ci] > nowRt);
                anyComboFlag |= (e.inCombo[ci] != 0);
            }

            if (anyVal) {
                Totals t{e.v[0], e.v[1], e.v[2]};
                fn(it->first, t);
                ++it;
            } else if (anyElemLock || anyComboCd || anyComboFlag) {
                Totals t{0, 0, 0};
                fn(it->first, t);
                ++it;
            } else {
                it = m.erase(it);  // GC real
            }
        }
    }

    std::vector<std::pair<RE::FormID, Totals>> SnapshotDecayed() {
        std::vector<std::pair<RE::FormID, Totals>> out;
        // opcional: out.reserve(Gauges::state().size());
        ForEachDecayed([&](RE::FormID id, const Totals& t) { out.emplace_back(id, t); });
        return out;
    }

    std::optional<Totals> GetTotalsDecayed(RE::FormID id) {
        auto& m = Gauges::state();
        const float nowH = NowHours();

        if (auto it = m.find(id); it != m.end()) {
            auto& e = it->second;
            Gauges::tickAll(e, nowH);

            const float eps = 1e-4f;
            const bool anyVal = (std::fabs(e.v[0]) > eps) || (std::fabs(e.v[1]) > eps) || (std::fabs(e.v[2]) > eps);

            if (anyVal) {
                return Totals{to_u8_100(e.v[0]), to_u8_100(e.v[1]), to_u8_100(e.v[2])};
            } else {
                return Totals{};
            }
        }
        return std::nullopt;
    }

    void GarbageCollectDecayed() {
        // Reaproveita a lógica
        ForEachDecayed([](RE::FormID, const Totals&) {});
    }

    std::optional<HudIconSel> PickHudIcon(const Totals& t) {
        const std::array<std::uint8_t, 3> v{t.fire, t.frost, t.shock};
        auto whichOpt = ChooseHudCombo(v);
        if (!whichOpt) return std::nullopt;
        const Combo which = *whichOpt;

        // Ícone (pares direcionais compartilham arquivo)
        const HudIcon icon = IconForCombo(which);

        // Tint: neutro para solo/triplo; nos pares, cor do elemento dominante
        const std::uint32_t tint = TintForIndex(which);

        return HudIconSel{static_cast<int>(std::to_underlying(icon)), tint, which};
    }

    std::optional<HudIconSel> PickHudIconDecayed(RE::FormID id) {
        auto t = GetTotalsDecayed(id);
        if (!t) return std::nullopt;

        if (!t->any()) {
            auto& m = Gauges::state();
            if (auto it = m.find(id); it != m.end()) {
                const float nowH = NowHours();
                const double nowRt = NowRealSeconds();
                auto& e = it->second;

                const bool anyElemLock = (e.blockUntilH[0] > nowH) || (e.blockUntilH[1] > nowH) ||
                                         (e.blockUntilH[2] > nowH) || (e.blockUntilRtS[0] > nowRt) ||
                                         (e.blockUntilRtS[1] > nowRt) || (e.blockUntilRtS[2] > nowRt);

                bool anyComboCd = false, anyComboFlag = false;
                for (size_t ci = 0; ci < kComboCount; ++ci) {
                    anyComboCd |= (e.comboBlockUntilH[ci] > nowH) || (e.comboBlockUntilRtS[ci] > nowRt);
                    anyComboFlag |= (e.inCombo[ci] != 0);
                }

                if (!anyElemLock && !anyComboCd && !anyComboFlag) {
                    m.erase(it);
                }
            }
            return std::nullopt;
        }

        return PickHudIcon(*t);
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

    std::optional<ActiveComboHUD> PickActiveComboHUD(RE::FormID id) {
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
        hud.which = which;  // ← só o enum
        hud.remainingRtS = bestRem;
        hud.durationRtS = std::max(0.001, double(cfg.elementLockoutSeconds));
        hud.realTime = true;
        return hud;
    }
}
