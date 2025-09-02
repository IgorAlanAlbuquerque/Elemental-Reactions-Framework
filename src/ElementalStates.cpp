#include "ElementalStates.h"

#include <unordered_map>

namespace ElementalStates {
    namespace Flags {
        using Bits = std::uint8_t;

        static std::unordered_map<RE::FormID, Bits> gBits;

        constexpr Bits bit(Flag f) { return static_cast<Bits>(1u << static_cast<unsigned>(f)); }

        constexpr std::uint32_t kUniqueID = 'ELMS';
        constexpr std::uint32_t kRecordID = 'FLGS';
        constexpr std::uint32_t kVersion = 1;
    }

    void Set(const RE::Actor* a, Flag f, bool value) {
        if (!a) return;
        auto& b = Flags::gBits[a->GetFormID()];
        const Flags::Bits m = Flags::bit(f);
        if (value)
            b |= m;
        else
            b &= ~m;
    }

    bool Get(const RE::Actor* a, Flag f) {
        if (!a) return false;
        auto it = Flags::gBits.find(a->GetFormID());
        if (it == Flags::gBits.end()) return false;
        return (it->second & Flags::bit(f)) != 0;
    }

    void Clear(const RE::Actor* a) {
        if (!a) return;
        Flags::gBits.erase(a->GetFormID());
    }

    void ClearAll() { Flags::gBits.clear(); }

    // ----- Callbacks SKSE -----
    static void OnSave(SKSE::SerializationInterface* ser) {
        if (!ser) return;
        if (!ser->OpenRecord(Flags::kRecordID, Flags::kVersion)) {
            return;
        }
        const std::uint32_t count = static_cast<std::uint32_t>(Flags::gBits.size());
        ser->WriteRecordData(&count, sizeof(count));
        for (const auto& [formID, bits] : Flags::gBits) {
            ser->WriteRecordData(&formID, sizeof(formID));
            ser->WriteRecordData(&bits, sizeof(bits));
        }
    }

    static void OnLoad(SKSE::SerializationInterface* ser) {
        if (!ser) return;

        std::uint32_t type{}, version{}, length{};
        while (ser->GetNextRecordInfo(type, version, length)) {
            if (type != Flags::kRecordID || version != Flags::kVersion) {
                // Outros records do plugin (se existirem) ou versões antigas
                continue;
            }

            std::uint32_t count = 0;
            if (!ser->ReadRecordData(&count, sizeof(count))) {
                continue;
            }

            Flags::gBits.clear();
            for (std::uint32_t i = 0; i < count; ++i) {
                RE::FormID oldID{};
                Flags::Bits bits{};
                if (!ser->ReadRecordData(&oldID, sizeof(oldID))) break;
                if (!ser->ReadRecordData(&bits, sizeof(bits))) break;

                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) {
                    continue;
                }
                Flags::gBits[newID] = bits;
            }
        }
    }

    static void OnRevert(SKSE::SerializationInterface*) { Flags::gBits.clear(); }

    void RegisterSerialization() {
        auto* ser = SKSE::GetSerializationInterface();
        ser->SetUniqueID(Flags::kUniqueID);
        ser->SetSaveCallback(OnSave);
        ser->SetLoadCallback(OnLoad);
        ser->SetRevertCallback(OnRevert);
    }
}
