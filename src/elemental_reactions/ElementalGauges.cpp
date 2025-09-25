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

        // Pequena ajuda para imprimir arrays u8
        inline std::string V3(const std::array<std::uint8_t, 3>& v) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[%u,%u,%u]", v[0], v[1], v[2]);
            return std::string(buf);
        }
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
            // muito verboso logar em toda chamada; deixe em info:
            spdlog::info("[Gauges] state() size={}", m.size());
            return m;
        }

        inline void tickOne(Entry& e, std::size_t i, float nowH) {
            auto& val = e.v[i];
            auto& eval = e.lastEvalH[i];
            const float hit = e.lastHitH[i];

            spdlog::info("[Gauges] tickOne i={} nowH={:.5f} val={} lastHitH={:.5f} lastEvalH={:.5f}", i, nowH, val, hit,
                         eval);

            if (val == 0) {
                eval = nowH;
                spdlog::info("[Gauges] tickOne i={} -> val==0 set lastEvalH=nowH", i);
                return;
            }

            const float graceEnd = hit + GraceGameHours();
            if (nowH <= graceEnd) {
                spdlog::info("[Gauges] tickOne i={} -> in grace (nowH {:.5f} <= {:.5f})", i, nowH, graceEnd);
                return;
            }

            const float startH = std::max(eval, graceEnd);
            const float elapsedH = nowH - startH;
            const float rate = DecayPerGameHour();
            const float decF = elapsedH * rate;
            const auto decI = static_cast<int>(decF);

            spdlog::info("[Gauges] tickOne i={} startH={:.5f} elapsedH={:.5f} rate={:.5f} decF={:.5f} decI={}", i,
                         startH, elapsedH, rate, decF, decI);

            if (decI <= 0) {
                spdlog::info("[Gauges] tickOne i={} -> decI<=0 (no change)", i);
                return;
            }

            int next = static_cast<int>(val) - decI;
            if (next < 0) next = 0;
            val = static_cast<std::uint8_t>(next);

            const float rem = decF - decI;
            eval = nowH - (rem / rate);

            spdlog::info("[Gauges] tickOne i={} newVal={} newLastEvalH={:.5f}", i, val, eval);
        }

        inline void tickAll(Entry& e, float nowH) {
            spdlog::info("[Gauges] tickAll nowH={:.5f} v={}", nowH, V3(e.v));
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
            if (!a || delta <= 0) {
                spdlog::info("[Gauges] AdjustByStates early: a={} delta={}", (void*)a, delta);
                return delta;
            }

            double f = 1.0;
            const bool wet = ElementalStates::Get(a, ElementalStates::Flag::Wet);
            const bool rubber = ElementalStates::Get(a, ElementalStates::Flag::Rubber);
            const bool fur = ElementalStates::Get(a, ElementalStates::Flag::Fur);

            spdlog::info("[Gauges] AdjustByStates actor={} t={} delta={} flags wet={} rubber={} fur={}", (void*)a,
                         std::to_underlying(t), delta, wet, rubber, fur);

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
            const int out = static_cast<int>(std::round(static_cast<double>(delta) * f));
            spdlog::info("[Gauges] AdjustByStates -> {}", out);
            return out;
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
            spdlog::info("[Gauges] ForEachElementInCombo c={}", std::to_underlying(c));
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
            if (cfg.elementLockoutSeconds <= 0.0f) {
                spdlog::info("[Gauges] ApplyElementLockout skip: seconds<=0");
                return;
            }

            const double untilRt = NowRealSeconds() + cfg.elementLockoutSeconds;
            const float untilH = nowH + static_cast<float>(cfg.elementLockoutSeconds / 3600.0);

            spdlog::info("[Gauges] ApplyElementLockout which={} secs={:.3f} isRT={} -> untilRt={:.3f} untilH={:.5f}",
                         std::to_underlying(which), cfg.elementLockoutSeconds, cfg.elementLockoutIsRealTime, untilRt,
                         untilH);

            ForEachElementInCombo(which, [&](std::size_t idx) {
                if (cfg.elementLockoutIsRealTime) {
                    e.blockUntilRtS[idx] = std::max(e.blockUntilRtS[idx], untilRt);
                    spdlog::info("[Gauges]  lock elem {} RtS={:.3f}", idx, e.blockUntilRtS[idx]);
                } else {
                    e.blockUntilH[idx] = std::max(e.blockUntilH[idx], untilH);
                    spdlog::info("[Gauges]  lock elem {} H={:.5f}", idx, e.blockUntilH[idx]);
                }
            });
        }

        struct [[nodiscard("RAII guard: mantenha este objeto vivo para manter o lock até o fim do escopo")]] TrigGuard {
            std::uint8_t* f;
            explicit TrigGuard(std::uint8_t& x) noexcept : f(&x) {
                *f = 1;
                spdlog::info("[Gauges] TrigGuard set=1");
            }
            TrigGuard(const TrigGuard&) = delete;
            TrigGuard& operator=(const TrigGuard&) = delete;
            TrigGuard(TrigGuard&&) = delete;
            TrigGuard& operator=(TrigGuard&&) = delete;
            ~TrigGuard() noexcept {
                if (f) {
                    *f = 0;
                    spdlog::info("[Gauges] TrigGuard set=0");
                }
            }
        };

        void DispatchCombo(RE::Actor* a, Combo which, const SumComboTrigger& cfg) {
            spdlog::info("[Gauges] DispatchCombo actor={} which={} defer={} hasCb={}", (void*)a,
                         std::to_underlying(which), cfg.deferToTask, (bool)cfg.cb);
            if (!cfg.cb || !a) return;
            if (cfg.deferToTask) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    RE::ActorHandle h = a->CreateRefHandle();
                    auto cb = cfg.cb;
                    void* user = cfg.user;
                    tasks->AddTask([h, cb, user, which]() {
                        if (auto actor = h.get().get()) {
                            spdlog::info("[Gauges] DispatchCombo(Task) actor live -> calling cb");
                            cb(actor, which, user);
                        } else {
                            spdlog::info("[Gauges] DispatchCombo(Task) actor expired");
                        }
                    });
                    return;
                }
                spdlog::warn("[Gauges] DispatchCombo defer requested but TaskInterface==null -> calling sync");
            }
            cfg.cb(a, which, cfg.user);
        }

        // Ordena índices [0:FIRE,1:FROST,2:SHOCK] por valor desc
        std::array<std::size_t, 3> rank3(const std::array<std::uint8_t, 3>& v) {
            auto idx = std::array<std::size_t, 3>{0, 1, 2};
            std::ranges::sort(idx, std::greater<>{}, [&v](std::size_t i) { return v[i]; });
            spdlog::info("[Gauges] rank3 v={} -> order=[{},{},{}]", V3(v), idx[0], idx[1], idx[2]);
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
            Combo out = (a == 0 && b == 1)   ? FireFrost
                        : (a == 1 && b == 0) ? FrostFire
                        : (a == 0 && b == 2) ? FireShock
                        : (a == 2 && b == 0) ? ShockFire
                        : (a == 1 && b == 2) ? FrostShock
                                             : ShockFrost;
            spdlog::info("[Gauges] makePairDirectional a={} b={} -> {}", a, b, std::to_underlying(out));
            return out;
        }

        // Pega um "conjunto de regras" global para maioria/tripleSpread (fallbacks sensatos)
        const SumComboTrigger& PickRules() {
            using enum ElementalGauges::Combo;
            const SumComboTrigger* sel = nullptr;
            if (g_onCombo[ComboIndex(FireFrostShock)].cb)
                sel = &g_onCombo[ComboIndex(FireFrostShock)];
            else if (g_onCombo[ComboIndex(Fire)].cb)
                sel = &g_onCombo[ComboIndex(Fire)];
            else if (g_onCombo[ComboIndex(Frost)].cb)
                sel = &g_onCombo[ComboIndex(Frost)];
            else if (g_onCombo[ComboIndex(Shock)].cb)
                sel = &g_onCombo[ComboIndex(Shock)];
            spdlog::info("[Gauges] PickRules -> {}", sel ? "custom" : "defaults(85%/28%/0.5s rt defer clearAll)");
            static SumComboTrigger defaults{/*cb*/ nullptr,
                                            /*user*/ nullptr,
                                            /*majorityPct*/ 0.85f,
                                            /*tripleMinPct*/ 0.28f,
                                            /*cooldownSeconds*/ 0.5f,
                                            /*cooldownIsRealTime*/ true,
                                            /*deferToTask*/ true,
                                            /*clearAllOnTrigger*/ true};
            return sel ? *sel : defaults;
        }

        // Decide o combo dado os totais PLANEJADOS (após somar o delta atual)
        std::optional<Combo> ChooseCombo(const std::array<std::uint8_t, 3>& tot) {
            const int sum = int(tot[0]) + int(tot[1]) + int(tot[2]);
            spdlog::info("[Gauges] ChooseCombo tot={} sum={}", V3(tot), sum);
            if (sum < 100) return std::nullopt;

            const auto& rules = PickRules();

            const float p0 = tot[0] / float(sum);
            const float p1 = tot[1] / float(sum);
            const float p2 = tot[2] / float(sum);

            if (p0 >= rules.majorityPct) {
                spdlog::info("[Gauges] ChooseCombo -> Solo Fire");
                return Combo::Fire;
            }
            if (p1 >= rules.majorityPct) {
                spdlog::info("[Gauges] ChooseCombo -> Solo Frost");
                return Combo::Frost;
            }
            if (p2 >= rules.majorityPct) {
                spdlog::info("[Gauges] ChooseCombo -> Solo Shock");
                return Combo::Shock;
            }

            if ((tot[0] > 0) && (tot[1] > 0) && (tot[2] > 0)) {
                const float minPct = std::min({p0, p1, p2});
                if (minPct >= rules.tripleMinPct) {
                    spdlog::info("[Gauges] ChooseCombo -> Triple");
                    return Combo::FireFrostShock;
                }
            }

            auto order = rank3(tot);
            auto out = makePairDirectional(order[0], order[1]);
            spdlog::info("[Gauges] ChooseCombo -> Pair {}", std::to_underlying(out));
            return out;
        }

        // Tenta disparar o combo (limpa gauges, cooldown, callback). Retorna true se disparou.
        bool MaybeSumComboReact(RE::Actor* a, Gauges::Entry& e, const std::array<std::uint8_t, 3>& afterTot) {
            spdlog::info("[Gauges] MaybeSumComboReact actor={} afterTot={}", (void*)a, V3(afterTot));
            auto whichOpt = ChooseCombo(afterTot);
            if (!whichOpt) {
                spdlog::info("[Gauges] MaybeSumComboReact -> no combo choice");
                return false;
            }

            const auto which = *whichOpt;
            const std::size_t ci = ComboIndex(which);
            const auto& cfg = g_onCombo[ci];
            if (!cfg.cb) {
                spdlog::info("[Gauges] MaybeSumComboReact -> combo {} not registered", ci);
                return false;
            }

            const float nowH = NowHours();

            const bool cooldownHit = (!cfg.cooldownIsRealTime && nowH < e.comboBlockUntilH[ci]) ||
                                     (cfg.cooldownIsRealTime && NowRealSeconds() < e.comboBlockUntilRtS[ci]);
            if (cooldownHit) {
                spdlog::info("[Gauges] MaybeSumComboReact -> cooldown hit (ci={})", ci);
                return false;
            }

            if (e.inCombo[ci]) {
                spdlog::info("[Gauges] MaybeSumComboReact -> inCombo flag set (ci={})", ci);
                return false;
            }
            TrigGuard guard{e.inCombo[ci]};

            auto* actor = a;
            if (!actor || actor->IsDead()) {
                spdlog::info("[Gauges] MaybeSumComboReact -> actor null/dead");
                return false;
            }

            const double nowRt = NowRealSeconds();
            const double lockS = std::max(0.0, double(cfg.elementLockoutSeconds));
            e.inCombo[ci] = 1;

            if (cfg.elementLockoutIsRealTime)
                e.comboBlockUntilRtS[ci] = nowRt + lockS;
            else
                e.comboBlockUntilH[ci] = nowH + float(lockS / 3600.0);

            spdlog::info("[Gauges] COMBO TRIGGER id={} which={} lockS={:.3f} isRT={} clearAll={}", actor->GetFormID(),
                         std::to_underlying(which), lockS, cfg.elementLockoutIsRealTime, cfg.clearAllOnTrigger);

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
                spdlog::info("[Gauges] MaybeSumComboReact -> gauges cleared");
            }

            ApplyElementLockout(e, which, cfg, nowH);
            DispatchCombo(actor, which, cfg);

            if (cfg.cooldownSeconds > 0.f) {
                if (cfg.cooldownIsRealTime)
                    e.comboBlockUntilRtS[ci] =
                        std::max(e.comboBlockUntilRtS[ci], NowRealSeconds() + cfg.cooldownSeconds);
                else
                    e.comboBlockUntilH[ci] = std::max(e.comboBlockUntilH[ci], nowH + (cfg.cooldownSeconds / 3600.0f));
                spdlog::info("[Gauges] MaybeSumComboReact -> cooldown set ci={} secs={:.3f} isRT={}", ci,
                             cfg.cooldownSeconds, cfg.cooldownIsRealTime);
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
            std::uint32_t tint = kNeutral;
            switch (c) {
                case C::Fire:
                    tint = kFire;
                    break;
                case C::Frost:
                    tint = kFrost;
                    break;
                case C::Shock:
                    tint = kShock;
                    break;

                case C::FireFrost:
                    tint = kFireFrost_RedBias;
                    break;
                case C::FrostFire:
                    tint = kFrostFire_BlueBias;
                    break;

                case C::FireShock:
                    tint = kFireShock_RedBias;
                    break;
                case C::ShockFire:
                    tint = kShockFire_YelBias;
                    break;

                case C::FrostShock:
                    tint = kFrostShock_BlueBias;
                    break;
                case C::ShockFrost:
                    tint = kShockFrost_YelBias;
                    break;

                case C::FireFrostShock:
                    tint = kTripleMix;
                    break;

                default:
                    tint = kNeutral;
                    break;
            }
            spdlog::info("[Gauges] TintForIndex {} -> #{:06X}", std::to_underlying(c), tint);
            return tint;
        }

        // Mapeia Combo -> arquivo de ícone (pares direcionais compartilham)
        inline ElementalGauges::HudIcon IconForCombo(Combo c) {
            using enum ElementalGauges::HudIcon;
            using ElementalGauges::Combo;
            HudIcon icon = Fire;
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
            spdlog::info("[Gauges] IconForCombo {} -> {}", std::to_underlying(c), std::to_underlying(icon));
            return icon;
        }

        // Versão para HUD: MESMAS regras do PickRules (85% / 28%), mas sem exigir soma >= 100
        std::optional<Combo> ChooseHudCombo(const std::array<std::uint8_t, 3>& tot) {
            const int sum = int(tot[0]) + int(tot[1]) + int(tot[2]);
            spdlog::info("[Gauges] ChooseHudCombo tot={} sum={}", V3(tot), sum);
            if (sum <= 0) return std::nullopt;

            const auto& rules = PickRules();
            const float p0 = tot[0] / float(sum);
            const float p1 = tot[1] / float(sum);
            const float p2 = tot[2] / float(sum);

            if (p0 >= rules.majorityPct) return Combo::Fire;
            if (p1 >= rules.majorityPct) return Combo::Frost;
            if (p2 >= rules.majorityPct) return Combo::Shock;

            if ((tot[0] > 0) && (tot[1] > 0) && (tot[2] > 0)) {
                const float minPct = std::min({p0, p1, p2});
                if (minPct >= rules.tripleMinPct) return Combo::FireFrostShock;
            }

            auto order = rank3(tot);
            auto out = makePairDirectional(order[0], order[1]);
            spdlog::info("[Gauges] ChooseHudCombo -> {}", std::to_underlying(out));
            return out;
        }

        static inline std::uint8_t to_u8_100(float x) {
            auto r = (x <= 0.f) ? 0 : (x >= 100.f) ? 100 : static_cast<std::uint8_t>(x + 0.5f);
            spdlog::info("[Gauges] to_u8_100 x={:.3f} -> {}", x, r);
            return r;
        }
    }

    // ============================
    // API pública
    // ============================
    void SetOnSumCombo(Combo c, const SumComboTrigger& cfg) {
        spdlog::info("[Gauges] SetOnSumCombo {} -> cb={} maj={:.2f} tri={:.2f} cd={:.3f} rt={} defer={} clear={}",
                     std::to_underlying(c), (bool)cfg.cb, cfg.majorityPct, cfg.tripleMinPct, cfg.cooldownSeconds,
                     cfg.cooldownIsRealTime, cfg.deferToTask, cfg.clearAllOnTrigger);
        g_onCombo[ComboIndex(c)] = cfg;
    }

    std::uint8_t Get(RE::Actor* a, Type t) {
        if (!a) {
            spdlog::info("[Gauges] Get a=null t={}", std::to_underlying(t));
            return 0;
        }
        auto& m = Gauges::state();
        const auto id = a->GetFormID();
        const auto it = m.find(id);
        if (it == m.end()) {
            spdlog::info("[Gauges] Get id={:08X} t={} -> miss", id, std::to_underlying(t));
            return 0;
        }
        auto& e = const_cast<Gauges::Entry&>(it->second);
        Gauges::tickOne(e, Gauges::idx(t), NowHours());
        spdlog::info("[Gauges] Get id={:08X} t={} -> {}", id, std::to_underlying(t), e.v[Gauges::idx(t)]);
        return e.v[Gauges::idx(t)];
    }

    void Set(RE::Actor* a, Type t, std::uint8_t value) {
        if (!a) {
            spdlog::info("[Gauges] Set a=null");
            return;
        }
        auto& e = Gauges::state()[a->GetFormID()];
        const float nowH = NowHours();
        Gauges::tickOne(e, Gauges::idx(t), nowH);
        e.v[Gauges::idx(t)] = clamp100(value);
        e.lastEvalH[Gauges::idx(t)] = nowH;
        spdlog::info("[Gauges] Set id={:08X} t={} value={} nowH={:.5f}", a->GetFormID(), std::to_underlying(t), value,
                     nowH);
    }

    void Add(RE::Actor* a, Type t, int delta) {
        if (!a) {
            spdlog::info("[Gauges] Add a=null");
            return;
        }
        auto& e = Gauges::state()[a->GetFormID()];
        const auto i = Gauges::idx(t);
        const float nowH = NowHours();

        spdlog::info("[Gauges] Add id={:08X} t={} delta={} v(before)={}", a->GetFormID(), std::to_underlying(t), delta,
                     V3(e.v));
        Gauges::tickAll(e, nowH);

        if (nowH < e.blockUntilH[i] || NowRealSeconds() < e.blockUntilRtS[i]) {
            spdlog::info("[Gauges] Add id={:08X} t={} -> BLOCKED (H {:.5f}<=?{:.5f} or Rt {:.3f}<=?{:.3f})",
                         a->GetFormID(), std::to_underlying(t), nowH, e.blockUntilH[i], NowRealSeconds(),
                         e.blockUntilRtS[i]);
            return;
        }

        const int before = e.v[i];
        const int adj = Gauges::AdjustByStates(a, t, delta);

        // boost de info
        int infoBoost = 15;

        std::array<std::uint8_t, 3> afterTot = e.v;
        afterTot[i] = clamp100(before + adj + infoBoost);

        const int sumBefore = int(e.v[0]) + int(e.v[1]) + int(e.v[2]);
        const int sumAfter = int(afterTot[0]) + int(afterTot[1]) + int(afterTot[2]);

        spdlog::info("[Gauges] Add id={:08X} t={} before={} adj={} dbg+={} afterTot={} sums {} -> {}", a->GetFormID(),
                     std::to_underlying(t), before, adj, infoBoost, V3(afterTot), sumBefore, sumAfter);

        if (sumBefore < 100 && sumAfter >= 100 && MaybeSumComboReact(a, e, afterTot)) {
            spdlog::info("[Gauges] Add id={:08X} -> Combo triggered, not storing element value", a->GetFormID());
            return;
        }

        e.v[i] = afterTot[i];
        e.lastHitH[i] = nowH;
        e.lastEvalH[i] = nowH;
        spdlog::info("[Gauges] Add id={:08X} t={} new v={} hitH={:.5f}", a->GetFormID(), std::to_underlying(t), e.v[i],
                     nowH);
    }

    void Clear(RE::Actor* a) {
        if (!a) {
            spdlog::info("[Gauges] Clear a=null");
            return;
        }
        const auto id = a->GetFormID();
        auto& m = Gauges::state();
        const auto erased = m.erase(id);
        spdlog::info("[Gauges] Clear id={:08X} erased={}", id, erased);
    }

    void ForEachDecayed(const std::function<void(RE::FormID, const Totals&)>& fn) {
        auto& m = Gauges::state();
        const float nowH = NowHours();
        const double nowRt = NowRealSeconds();
        spdlog::info("[Gauges] ForEachDecayed size={} nowH={:.5f} nowRt={:.3f}", m.size(), nowH, nowRt);

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

            spdlog::info(
                "[Gauges] ForEachDecayed id={:08X} v={} anyVal={} anyElemLock={} anyComboCd={} anyComboFlag={}",
                it->first, V3(e.v), anyVal, anyElemLock, anyComboCd, anyComboFlag);

            if (anyVal) {
                Totals t{e.v[0], e.v[1], e.v[2]};
                fn(it->first, t);
                ++it;
            } else if (anyElemLock || anyComboCd || anyComboFlag) {
                Totals t{0, 0, 0};
                fn(it->first, t);
                ++it;
            } else {
                spdlog::info("[Gauges] ForEachDecayed GC erase id={:08X}", it->first);
                it = m.erase(it);  // GC real
            }
        }
    }

    std::vector<std::pair<RE::FormID, Totals>> SnapshotDecayed() {
        spdlog::info("[Gauges] SnapshotDecayed begin");
        std::vector<std::pair<RE::FormID, Totals>> out;
        ForEachDecayed([&](RE::FormID id, const Totals& t) {
            spdlog::info("[Gauges]  snap id={:08X} t=[{},{},{}]", id, t.fire, t.frost, t.shock);
            out.emplace_back(id, t);
        });
        spdlog::info("[Gauges] SnapshotDecayed end size={}", out.size());
        return out;
    }

    std::optional<Totals> GetTotalsDecayed(RE::FormID id) {
        auto& m = Gauges::state();
        const float nowH = NowHours();
        spdlog::info("[Gauges] GetTotalsDecayed id={:08X}", id);

        if (auto it = m.find(id); it != m.end()) {
            auto& e = it->second;
            Gauges::tickAll(e, nowH);

            const float eps = 1e-4f;
            const bool anyVal = (std::fabs(e.v[0]) > eps) || (std::fabs(e.v[1]) > eps) || (std::fabs(e.v[2]) > eps);

            if (anyVal) {
                Totals t{to_u8_100(e.v[0]), to_u8_100(e.v[1]), to_u8_100(e.v[2])};
                spdlog::info("[Gauges] GetTotalsDecayed id={:08X} -> {}", id, V3({t.fire, t.frost, t.shock}));
                return t;
            } else {
                spdlog::info("[Gauges] GetTotalsDecayed id={:08X} -> zeros (kept)", id);
                return Totals{};
            }
        }
        spdlog::info("[Gauges] GetTotalsDecayed id={:08X} -> not found", id);
        return std::nullopt;
    }

    void GarbageCollectDecayed() {
        spdlog::info("[Gauges] GarbageCollectDecayed start");
        // Reaproveita a lógica
        ForEachDecayed([](RE::FormID, const Totals&) {});
        spdlog::info("[Gauges] GarbageCollectDecayed end");
    }

    std::optional<HudIconSel> PickHudIcon(const Totals& t) {
        const std::array<std::uint8_t, 3> v{t.fire, t.frost, t.shock};
        spdlog::info("[Gauges] PickHudIcon v={}", V3(v));
        auto whichOpt = ChooseHudCombo(v);
        if (!whichOpt) {
            spdlog::info("[Gauges] PickHudIcon -> nullopt");
            return std::nullopt;
        }
        const Combo which = *whichOpt;

        const HudIcon icon = IconForCombo(which);
        const std::uint32_t tint = TintForIndex(which);

        spdlog::info("[Gauges] PickHudIcon -> icon={} tint=#{:06X} which={}", std::to_underlying(icon), tint,
                     std::to_underlying(which));
        return HudIconSel{static_cast<int>(std::to_underlying(icon)), tint, which};
    }

    std::optional<HudIconSel> PickHudIconDecayed(RE::FormID id) {
        spdlog::info("[Gauges] PickHudIconDecayed id={:08X}", id);
        auto t = GetTotalsDecayed(id);
        if (!t) {
            spdlog::info("[Gauges] PickHudIconDecayed -> totals=nullopt");
            return std::nullopt;
        }

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
                    spdlog::info("[Gauges] PickHudIconDecayed GC erase id={:08X}", id);
                    m.erase(it);
                } else {
                    spdlog::info("[Gauges] PickHudIconDecayed keep id={:08X} locks/cds present", id);
                }
            }
            return std::nullopt;
        }

        auto r = PickHudIcon(*t);
        spdlog::info("[Gauges] PickHudIconDecayed id={:08X} -> hasIcon={}", id, (bool)r);
        return r;
    }

    // ============================
    // Serialização
    // ============================
    namespace {
        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = Gauges::state();
            spdlog::info("[Gauges] Save count={}", m.size());

            if (const auto count = static_cast<std::uint32_t>(m.size()); !ser->WriteRecordData(&count, sizeof(count)))
                return false;

            const bool ok = std::ranges::all_of(m, [ser](const auto& kv) {
                const auto& id = kv.first;
                const auto& e = kv.second;
                const auto bytes = static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0]));
                spdlog::info("[Gauges]  Save id={:08X} v={}", id, V3(e.v));
                return ser->WriteRecordData(&id, sizeof(id)) && ser->WriteRecordData(e.v.data(), bytes);
            });

            spdlog::info("[Gauges] Save -> {}", ok);
            return ok;
        }

        bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t /*length*/) {
            spdlog::info("[Gauges] Load version={}", version);
            if (version != Gauges::kVersion) return false;
            auto& m = Gauges::state();
            m.clear();

            std::uint32_t count{};
            if (!ser->ReadRecordData(&count, sizeof(count))) return false;
            spdlog::info("[Gauges] Load count={}", count);

            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                Gauges::Entry e{};
                if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;
                if (const auto bytes = static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0]));
                    !ser->ReadRecordData(e.v.data(), bytes))
                    return false;

                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) {
                    spdlog::warn("[Gauges] Load resolve failed old={:08X}", oldID);
                    continue;
                }

                const float nowH = NowHours();
                e.lastHitH = {nowH, nowH, nowH};
                e.lastEvalH = {nowH, nowH, nowH};
                m[newID] = e;
                spdlog::info("[Gauges]  Load id={:08X} v={}", newID, V3(e.v));
            }
            return true;
        }

        void Revert() {
            spdlog::info("[Gauges] Revert");
            Gauges::state().clear();
        }
    }

    void RegisterStore() {
        spdlog::info("[Gauges] RegisterStore recId='GAUV' v={}", Gauges::kVersion);
        Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Save, &Load, &Revert});
    }

    std::optional<ActiveComboHUD> PickActiveComboHUD(RE::FormID id) {
        auto& m = Gauges::state();
        auto it = m.find(id);
        if (it == m.end()) {
            spdlog::info("[Gauges] PickActiveComboHUD id={:08X} -> not found", id);
            return std::nullopt;
        }

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
        if (bestIdx == SIZE_MAX) {
            spdlog::info("[Gauges] PickActiveComboHUD id={:08X} -> none active", id);
            return std::nullopt;
        }

        const auto which = static_cast<Combo>(bestIdx);
        const auto& cfg = g_onCombo[bestIdx];

        ActiveComboHUD hud;
        hud.which = which;  // ← só o enum
        hud.remainingRtS = bestRem;
        hud.durationRtS = std::max(0.001, double(cfg.elementLockoutSeconds));
        hud.realTime = true;

        spdlog::info("[Gauges] PickActiveComboHUD id={:08X} which={} rem={:.3f}s dur={:.3f}s", id,
                     std::to_underlying(which), bestRem, hud.durationRtS);
        return hud;
    }
}