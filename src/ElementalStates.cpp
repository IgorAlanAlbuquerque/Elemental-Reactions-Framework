#include "ElementalStates.h"

#include <cstddef>
#include <unordered_map>

#include "common/PluginSerialization.h"
#include "common/StateCommon.h"

namespace ElementalStates {
    namespace Flags {
        using Bits = std::byte;
        using Map = std::unordered_map<RE::FormID, Bits>;

        static Map& state() noexcept {
            static Map m;  // NOSONAR: estado centralizado com duração estática, serializado via SKSE
            return m;
        }

        [[nodiscard]] constexpr std::byte bit(Flag f) noexcept {
            return std::byte{1} << static_cast<int>(to_underlying(f));
        }

        inline constexpr std::uint32_t kRecordID = FOURCC('F', 'L', 'G', 'S');
        inline constexpr std::uint32_t kVersion = 1;

        bool Save(SKSE::SerializationInterface* ser) {
            const auto& m = state();
            const auto count = static_cast<std::uint32_t>(m.size());
            ser->WriteRecordData(&count, sizeof(count));
            for (const auto& [id, b] : m) {
                std::uint8_t raw = std::to_integer<std::uint8_t>(b);
                ser->WriteRecordData(&id, sizeof(id));
                ser->WriteRecordData(&raw, sizeof(raw));
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
                std::uint8_t raw{};
                if (!(ser->ReadRecordData(&oldID, sizeof(oldID)) && ser->ReadRecordData(&raw, sizeof(raw)))) break;
                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) continue;
                m[newID] = std::byte{raw};
            }
            return true;
        }

        void Revert() { state().clear(); }
    }

    void Set(const RE::Actor* a, Flag f, bool value) {
        if (!a) return;
        auto& b = Flags::state()[a->GetFormID()];
        const auto m = Flags::bit(f);
        if (value)
            b |= m;
        else
            b &= ~m;
    }
    bool Get(const RE::Actor* a, Flag f) {
        if (!a) return false;
        const auto it = Flags::state().find(a->GetFormID());
        if (it == Flags::state().end()) return false;
        return (it->second & Flags::bit(f)) != std::byte{0};
    }
    void Clear(const RE::Actor* a) {
        if (a) Flags::state().erase(a->GetFormID());
    }
    void ClearAll() { Flags::state().clear(); }

    void RegisterStore() {
        Ser::Register({Flags::kRecordID, Flags::kVersion, &Flags::Save, &Flags::Load, &Flags::Revert});
    }
}
