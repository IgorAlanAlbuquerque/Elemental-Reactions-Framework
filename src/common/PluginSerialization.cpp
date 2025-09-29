#include "PluginSerialization.h"

#include <algorithm>
#include <ranges>

namespace SerFunctions {
    using Handlers = std::vector<Ser::Handler>;

    Handlers& registry() noexcept {
        static Handlers r;  // NOSONAR: estado centralizado, mutável por design
        return r;
    }

    void OnSave(SKSE::SerializationInterface* ser) {
        for (const auto& h : registry()) {
            if (!ser->OpenRecord(h.recordID, h.version)) continue;
            if (h.save) h.save(ser);
        }
    }

    void OnRevert(SKSE::SerializationInterface*) {
        for (const auto& h : registry()) {
            if (h.revert) h.revert();
        }
    }

    void OnLoad(SKSE::SerializationInterface* ser) {
        std::uint32_t type{};
        std::uint32_t version{};
        std::uint32_t length{};
        while (ser->GetNextRecordInfo(type, version, length)) {
            auto& reg = registry();

            const auto it = std::ranges::find_if(reg, [&](const Ser::Handler& h) { return h.recordID == type; });

            if (it != std::ranges::end(reg) && it->load) {
                it->load(ser, version, length);
            }
        }
    }
}

namespace Ser {
    void Register(const Handler& h) { SerFunctions::registry().push_back(h); }

    void Install(std::uint32_t uniqueID) {
        auto* si = SKSE::GetSerializationInterface();
        si->SetUniqueID(uniqueID);
        si->SetSaveCallback(SerFunctions::OnSave);
        si->SetLoadCallback(SerFunctions::OnLoad);
        si->SetRevertCallback(SerFunctions::OnRevert);
    }
}
