#include "ElementalGauges.h"

#include <array>
#include <unordered_map>

#include "common/PluginSerialization.h"
#include "common/StateCommon.h"

namespace ElementalGauges {
    namespace Gauges {
        using Trio = std::array<std::uint8_t, 3>;
        using Map = std::unordered_map<RE::FormID, Trio>;

        inline Map& state() noexcept {
            static Map m;  // NOSONAR
            return m;
        }
        [[nodiscard]] constexpr std::size_t idx(Type t) noexcept { return static_cast<std::size_t>(to_underlying(t)); }

        inline constexpr std::uint32_t kRecordID = FOURCC('G', 'A', 'U', 'V');
        inline constexpr std::uint32_t kVersion = 1;

        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = state();
            const auto count = static_cast<std::uint32_t>(m.size());
            ser->WriteRecordData(&count, sizeof(count));
            for (const auto& [id, v] : m) {
                ser->WriteRecordData(&id, sizeof(id));
                ser->WriteRecordData(v.data(), static_cast<std::uint32_t>(v.size()));  // 3 bytes
            }
            return true;
        }

        bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t) {
            if (version != kVersion) return false;
            std::uint32_t count{};
            if (!ser->ReadRecordData(&count, sizeof(count))) return false;

            auto& m = state();
            m.clear();

            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                Trio trio{};
                if ((!ser->ReadRecordData(&oldID, sizeof(oldID))) ||
                    (!ser->ReadRecordData(trio.data(), static_cast<std::uint32_t>(trio.size()))))
                    break;
                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) continue;
                m[newID] = trio;
            }
            return true;
        }

        void Revert() { state().clear(); }
    }

    std::uint8_t Get(const RE::Actor* a, Type t) {
        if (!a) return 0;
        const auto& m = Gauges::state();
        const auto it = m.find(a->GetFormID());
        return (it == m.end()) ? 0 : it->second[Gauges::idx(t)];
    }
    void Set(const RE::Actor* a, Type t, std::uint8_t value) {
        if (!a) return;
        Gauges::state()[a->GetFormID()][Gauges::idx(t)] = clamp100(value);
    }
    void Add(const RE::Actor* a, Type t, int delta) {
        if (!a) return;
        auto& arr = Gauges::state()[a->GetFormID()];
        arr[Gauges::idx(t)] = clamp100(static_cast<int>(arr[Gauges::idx(t)]) + delta);
    }
    void Clear(const RE::Actor* a) {
        if (a) Gauges::state().erase(a->GetFormID());
    }
    void ClearAll() { Gauges::state().clear(); }

    void RegisterStore() {
        Ser::Register({Gauges::kRecordID, Gauges::kVersion, &Gauges::Save, &Gauges::Load, &Gauges::Revert});
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
        const auto f0 = ElementalGauges::Get(pc, Fire);
        const auto r0 = ElementalGauges::Get(pc, Frost);
        const auto s0 = ElementalGauges::Get(pc, Shock);
        spdlog::info("[GAUGES TEST] inicial: Fire={} Frost={} Shock={}", f0, r0, s0);

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
    }
}
