#include "ElementalStates.h"

#if !defined(__cpp_lib_to_underlying) || (__cpp_lib_to_underlying < 202102L)
template <class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}
#else
using std::to_underlying;
#endif

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

        inline constexpr std::uint32_t kUniqueID = FOURCC('E', 'L', 'M', 'S');
        inline constexpr std::uint32_t kRecordID = FOURCC('F', 'L', 'G', 'S');
        inline constexpr std::uint32_t kVersion = 1;
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
        const auto& map = Flags::state();
        const auto it = map.find(a->GetFormID());
        if (it == map.end()) return false;
        return (it->second & Flags::bit(f)) != std::byte{0};
    }

    void Clear(const RE::Actor* a) {
        if (!a) return;
        Flags::state().erase(a->GetFormID());
    }

    void ClearAll() { Flags::state().clear(); }

    // ----- Callbacks SKSE -----
    static void OnSave(SKSE::SerializationInterface* ser) {
        if (!ser) return;
        if (!ser->OpenRecord(Flags::kRecordID, Flags::kVersion)) return;

        const auto& map = Flags::state();
        const auto count = static_cast<std::uint32_t>(map.size());
        ser->WriteRecordData(&count, sizeof(count));

        for (const auto& [formID, bits] : map) {
            std::uint8_t raw = std::to_integer<std::uint8_t>(bits);
            ser->WriteRecordData(&formID, sizeof(formID));
            ser->WriteRecordData(&raw, sizeof(raw));
        }
    }

    static void OnLoad(SKSE::SerializationInterface* ser) {
        if (!ser) return;

        std::uint32_t type{};
        std::uint32_t version{};
        std::uint32_t length{};
        while (ser->GetNextRecordInfo(type, version, length)) {
            if (type != Flags::kRecordID || version != Flags::kVersion) continue;

            std::uint32_t count = 0;
            if (!ser->ReadRecordData(&count, sizeof(count))) continue;

            auto& map = Flags::state();
            map.clear();

            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                std::uint8_t raw{};
                if (!ser->ReadRecordData(&oldID, sizeof(oldID)) || !ser->ReadRecordData(&raw, sizeof(raw))) break;

                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) continue;

                map[newID] = std::byte{raw};
            }
        }
    }

    static void OnRevert(SKSE::SerializationInterface*) { Flags::state().clear(); }

    void RegisterSerialization() {
        auto* ser = SKSE::GetSerializationInterface();
        ser->SetUniqueID(Flags::kUniqueID);
        ser->SetSaveCallback(OnSave);
        ser->SetLoadCallback(OnLoad);
        ser->SetRevertCallback(OnRevert);
    }
}

namespace ElementalStatesTest {
    using enum ElementalStates::Flag;

    void RunOnce() {
        const RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) {
            spdlog::error("[TEST] PlayerCharacter não encontrado");
            return;
        }

        // snapshot inicial
        auto wet = ElementalStates::Get(pc, Wet);
        auto rubber = ElementalStates::Get(pc, Rubber);
        auto fur = ElementalStates::Get(pc, Fur);
        auto fat = ElementalStates::Get(pc, Fat);
        spdlog::info("[TEST] inicial: Wet={} Rubber={} Fur={} Fat={}", wet, rubber, fur, fat);

        // liga duas flags
        ElementalStates::Set(pc, Wet, true);
        ElementalStates::Set(pc, Rubber, true);

        spdlog::info("[TEST] depois de set: Wet={} Rubber={} Fur={} Fat={}", ElementalStates::Get(pc, Wet),
                     ElementalStates::Get(pc, Rubber), ElementalStates::Get(pc, Fur), ElementalStates::Get(pc, Fat));

        // alterna
        ElementalStates::Set(pc, Wet, false);
        ElementalStates::Set(pc, Rubber, false);
        ElementalStates::Set(pc, Fur, true);
        ElementalStates::Set(pc, Fat, true);

        spdlog::info("[TEST] depois do toggle: Wet={} Rubber={} Fur={} Fat={}", ElementalStates::Get(pc, Wet),
                     ElementalStates::Get(pc, Rubber), ElementalStates::Get(pc, Fur), ElementalStates::Get(pc, Fat));
        ElementalStates::Set(pc, Wet, wet);
        ElementalStates::Set(pc, Rubber, rubber);
        ElementalStates::Set(pc, Fur, fur);
        ElementalStates::Set(pc, Fat, fat);
    }
}