#include "ElementalGauges.h"

#include <array>
#include <chrono>
#include <unordered_map>
#include <utility>

#include "common/Helpers.h"
#include "common/PluginSerialization.h"

using namespace ElementalGaugesDecay;

namespace ElementalGauges {
    namespace Gauges {
        struct Entry {
            std::array<std::uint8_t, 3> v{0, 0, 0};         // Fire, Frost, Shock
            std::array<float, 3> lastHitH{0.f, 0.f, 0.f};   // hora (jogo) do último hit por elemento
            std::array<float, 3> lastEvalH{0.f, 0.f, 0.f};  // última vez que aplicamos decay
        };

        using Map = std::unordered_map<RE::FormID, Entry>;

        inline Map& state() noexcept {
            static Map m;  // NOSONAR
            return m;
        }

        // Aplica decay pendente para 1 elemento de um Entry
        inline void tickOne(Entry& e, std::size_t i, float nowH) {
            auto& val = e.v[i];
            const float hit = e.lastHitH[i];
            auto& eval = e.lastEvalH[i];

            if (val == 0) {
                eval = nowH;
                return;
            }

            // início do decaimento é o maior entre: (última avaliação) e (último hit + graça)
            const float startH = std::max(eval, hit + kGraceHours);
            if (nowH <= startH) return;

            const float elapsedH = nowH - startH;
            const float decF = elapsedH * kDecayPerHour;  // unidades
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
    }

    std::uint8_t Get(const RE::Actor* a, Type t) {
        if (!a) return 0;
        auto& m = Gauges::state();
        const auto it = m.find(a->GetFormID());
        if (it == m.end()) return 0;

        auto& e = const_cast<Gauges::Entry&>(it->second);  // só para tick; não alteramos “valor lógico” além do decay
        Gauges::tickOne(e, Gauges::idx(t), NowHours());
        return e.v[Gauges::idx(t)];
    }

    void Set(const RE::Actor* a, Type t, std::uint8_t value) {
        if (!a) return;
        auto& e = Gauges::state()[a->GetFormID()];
        const float nowH = NowHours();
        Gauges::tickOne(e, Gauges::idx(t), nowH);  // aplica decay pendente antes de setar
        e.v[Gauges::idx(t)] = clamp100(value);
        e.lastEvalH[Gauges::idx(t)] = nowH;  // marcou avaliação
        // não mexemos em lastHitH aqui — set manual não “conta como hit”
    }

    void Add(const RE::Actor* a, Type t, int delta) {
        if (!a) return;
        auto& e = Gauges::state()[a->GetFormID()];
        const float nowH = NowHours();
        Gauges::tickOne(e, Gauges::idx(t), nowH);  // aplica decay antes
        const int next = static_cast<int>(e.v[Gauges::idx(t)]) + delta;
        e.v[Gauges::idx(t)] = clamp100(next);
        e.lastHitH[Gauges::idx(t)] = nowH;   // *este* é um hit — reinicia “graça”
        e.lastEvalH[Gauges::idx(t)] = nowH;  // última avaliação agora
    }

    void Clear(const RE::Actor* a) {
        if (!a) return;
        Gauges::state().erase(a->GetFormID());
    }

    void ClearAll() { Gauges::state().clear(); }

    // --- Serialization
    namespace {
        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = Gauges::state();
            const auto count = static_cast<std::uint32_t>(m.size());
            ser->WriteRecordData(&count, sizeof(count));
            for (const auto& [id, e] : m) {
                ser->WriteRecordData(&id, sizeof(id));
                ser->WriteRecordData(e.v.data(),
                                     static_cast<std::uint32_t>(e.v.size() * sizeof(e.v[0])));  // 3 bytes
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

                // Reinicia relógios no load — começará a decair após nova “graça”
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

namespace {
    using namespace std::chrono;

    void LogOnceAt(std::chrono::steady_clock::time_point target) {
        const auto now = steady_clock::now();
        if (now < target) {
            // re-agenda até bater o horário
            SKSE::GetTaskInterface()->AddTask([target]() { LogOnceAt(target); });
            return;
        }

        if (const auto* pc = RE::PlayerCharacter::GetSingleton()) {
            using enum ElementalGauges::Type;
            const auto fire = ElementalGauges::Get(pc, Fire);  // aplica decay pendente
            const auto frost = ElementalGauges::Get(pc, Frost);
            const auto shock = ElementalGauges::Get(pc, Shock);
            spdlog::info("[GAUGES TEST] após delay: Fire={} Frost={} Shock={}", fire, frost, shock);
        }
    }

    inline void LogAfterDelaySeconds(int seconds) {
        const auto sec = std::max(0, seconds);
        const auto when = std::chrono::steady_clock::now() + std::chrono::seconds(sec);
        SKSE::GetTaskInterface()->AddTask([when]() { LogOnceAt(when); });
        spdlog::info("[GAUGES TEST] agendado um Get() em {}s.", sec);
    }
}

namespace ElementalGaugesTest {
    using enum ElementalGauges::Type;

    void RunOnce() {
        const RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) {
            spdlog::error("[GAUGES TEST] PlayerCharacter não encontrado");
            return;
        }

        // --- snapshot inicial
        spdlog::info("[GAUGES TEST] inicial: Fire={} Frost={} Shock={}", ElementalGauges::Get(pc, Fire),
                     ElementalGauges::Get(pc, Frost), ElementalGauges::Get(pc, Shock));

        // --- SET: define alguns valores base
        ElementalGauges::Set(pc, Fire, 20);
        ElementalGauges::Set(pc, Frost, 40);
        ElementalGauges::Set(pc, Shock, 60);
        spdlog::info("[GAUGES TEST] depois de Set: Fire={} Frost={} Shock={}", ElementalGauges::Get(pc, Fire),
                     ElementalGauges::Get(pc, Frost), ElementalGauges::Get(pc, Shock));

        // --- ADD: incrementos/declínios (testa clamp em 0..100)
        ElementalGauges::Add(pc, Fire, +90);    // 20 + 90 -> 100 (satura)
        ElementalGauges::Add(pc, Frost, -100);  // 40 - 100 -> 0   (satura)
        ElementalGauges::Add(pc, Shock, +25);   // 60 + 25  -> 85
        spdlog::info("[GAUGES TEST] depois de Add: Fire={} Frost={} Shock={}", ElementalGauges::Get(pc, Fire),
                     ElementalGauges::Get(pc, Frost), ElementalGauges::Get(pc, Shock));

        LogAfterDelaySeconds(30);
    }
}
