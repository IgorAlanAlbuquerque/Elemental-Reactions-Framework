#include "ElementalGauges.h"

#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "common/Helpers.h"
#include "common/PluginSerialization.h"

using namespace ElementalGaugesDecay;

namespace ElementalGauges {
    namespace Gauges {
        struct Entry {
            std::array<std::uint8_t, 3> v{0, 0, 0};  // Fire/Frost/Shock
            std::array<float, 3> lastHitH{0, 0, 0};
            std::array<float, 3> lastEvalH{0, 0, 0};

            std::array<float, 3> blockUntilH{0, 0, 0};
            std::array<double, 3> blockUntilRtS{0, 0, 0};
            std::array<std::uint8_t, 3> inTrig{0, 0, 0};
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
            const float untilDecay = tRef + ElementalGaugesDecay::GraceGameHours();
            if (nowH <= untilDecay) return;

            const float elapsedH = nowH - untilDecay;
            const float decF = elapsedH * ElementalGaugesDecay::DecayPerGameHour();

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

        inline constexpr std::uint32_t kRecordID = FOURCC('G', 'A', 'U', 'V');
        inline constexpr std::uint32_t kVersion = 1;

        inline constexpr float increaseMult = 1.30f;
        inline constexpr float decreaseMult = 0.10f;

        static int AdjustByStates(RE::Actor* a, Type t, int delta) {
            using enum ElementalGauges::Type;
            if (!a || delta <= 0) return delta;

            double f = 1.0;

            const bool wet = ElementalStates::Get(a, ElementalStates::Flag::Wet);
            const bool rubber = ElementalStates::Get(a, ElementalStates::Flag::Rubber);
            const bool fur = ElementalStates::Get(a, ElementalStates::Flag::Fur);

            switch (t) {
                case Fire:
                    if (wet)
                        f *= decreaseMult;
                    else if (fur || rubber)
                        f *= increaseMult;
                    break;
                case Frost:
                    if (fur)
                        f *= decreaseMult;
                    else if (wet || rubber)
                        f *= increaseMult;
                    break;
                case Shock:
                    if (rubber)
                        f *= decreaseMult;
                    else if (wet || fur)
                        f *= increaseMult;
                    break;
            }

            auto out = static_cast<int>(std::round(static_cast<double>(delta) * f));
            return out;
        }
    }

    namespace {
        std::array<ElementalGauges::FullTrigger, 3> g_onFull{};  // NOSONAR

        double NowRealSeconds() {
            using clock = std::chrono::steady_clock;
            static const auto t0 = clock::now();
            return std::chrono::duration<double>(clock::now() - t0).count();
        }

        struct [[nodiscard(
            "RAII guard: mantenha este objeto vivo em uma variável "
            "para manter o lock ativo até o fim do escopo")]] TrigGuard {
            std::uint8_t* f;

            explicit TrigGuard(std::uint8_t& x) noexcept : f(&x) { *f = 1; }

            TrigGuard(const TrigGuard&) = delete;
            TrigGuard& operator=(const TrigGuard&) = delete;

            TrigGuard(TrigGuard&& other) noexcept : f(other.f) { other.f = nullptr; }
            TrigGuard& operator=(TrigGuard&& other) noexcept {
                if (this != &other) {
                    release();
                    f = other.f;
                    other.f = nullptr;
                }
                return *this;
            }

            ~TrigGuard() noexcept { release(); }

        private:
            void release() noexcept {
                if (f) {
                    *f = 0;
                    f = nullptr;
                }
            }
        };

        void DispatchCallback(RE::Actor* a, ElementalGauges::Type t, const ElementalGauges::FullTrigger& cfg) {
            if (!cfg.cb || !a) return;
            if (cfg.deferToTask) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    RE::ActorHandle h = a->CreateRefHandle();
                    auto cb = cfg.cb;
                    void* user = cfg.user;
                    tasks->AddTask([h, cb, user, t]() {
                        if (auto actor = h.get().get()) cb(actor, t, user);
                    });
                    return;
                }
            }
            cfg.cb(a, t, cfg.user);
        }

        void DoTrigger(RE::Actor* a, Gauges::Entry& e, std::size_t i, ElementalGauges::Type t, float nowH) {
            const auto& cfg = g_onFull[i];
            if (!cfg.cb && cfg.lockoutSeconds <= 0.f && !cfg.clearOnTrigger) return;

            if (e.inTrig[i]) return;
            TrigGuard guard{e.inTrig[i]};

            if (!a || a->IsDead()) return;

            if (cfg.clearOnTrigger) {
                e.v[i] = 0;
                e.lastHitH[i] = nowH;
                e.lastEvalH[i] = nowH;
            }

            DispatchCallback(a, t, cfg);

            if (cfg.lockoutSeconds > 0.f) {
                if (cfg.lockoutIsRealTime) {
                    e.blockUntilRtS[i] = NowRealSeconds() + static_cast<double>(cfg.lockoutSeconds);
                } else {
                    e.blockUntilH[i] = nowH + (cfg.lockoutSeconds / 3600.0f);
                }
            }
        }
    }

    void ElementalGauges::SetOnFull(Type t, const FullTrigger& cfg) {
        g_onFull[static_cast<std::size_t>(std::to_underlying(t))] = cfg;
    }

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

        Gauges::tickOne(e, i, nowH);

        if (nowH < e.blockUntilH[i] || NowRealSeconds() < e.blockUntilRtS[i]) {
            return;
        }

        const int before = e.v[i];
        const int adj = Gauges::AdjustByStates(a, t, delta);
        const auto after = clamp100(static_cast<int>(e.v[i]) + adj);

        e.v[i] = after + 10;
        e.lastHitH[i] = nowH;
        e.lastEvalH[i] = nowH;
        spdlog::info("aumentou o gauge para {}", e.v[i]);

        if (before < 100 && after == 100) {
            spdlog::info("ativou o efeito full");
            DoTrigger(a, e, i, t, nowH);
        }
    }

    void Clear(RE::Actor* a) {
        if (!a) return;
        Gauges::state().erase(a->GetFormID());
    }

    void ClearAll() { Gauges::state().clear(); }
    namespace {
        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = Gauges::state();
            const auto count = static_cast<std::uint32_t>(m.size());
            ser->WriteRecordData(&count, sizeof(count));
            for (const auto& [id, e] : m) {
                ser->WriteRecordData(&id, sizeof(id));
                ser->WriteRecordData(e.v.data(), static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0])));
            }
            return true;
        }

        bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t /*length*/) {
            if (version != Gauges::kVersion) return false;
            auto& m = Gauges::state();
            m.clear();

            auto count = std::uint32_t{};
            if (!ser->ReadRecordData(&count, sizeof(count))) return false;

            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                Gauges::Entry e{};
                if ((!ser->ReadRecordData(&oldID, sizeof(oldID))) ||
                    (!ser->ReadRecordData(e.v.data(), static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0])))))
                    break;

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
