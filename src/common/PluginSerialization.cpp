#include "PluginSerialization.h"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <unordered_map>

namespace SerFunctions {
    using Handlers = std::vector<Ser::Handler>;

    Handlers& registry() noexcept {
        static Handlers r;  // NOSONAR: estado centralizado, mutável por design
        return r;
    }

    static std::unordered_map<std::uint32_t, const Ser::Handler*> g_index;
    static bool g_indexBuilt = false;
    static void BuildIndexOnce_() {
        if (g_indexBuilt) return;
        g_index.clear();
        g_index.reserve(registry().size());
        for (const auto& h : registry()) g_index.emplace(h.recordID, &h);
        g_indexBuilt = true;
    }

    static void InvalidateIndex_() { g_indexBuilt = false; }

    void OnSave(SKSE::SerializationInterface* ser) {
        BuildIndexOnce_();
        for (const auto& h : registry()) {
            if (!h.save) continue;
            const bool hasData = h.save(ser, true);
            if (!hasData) continue;
            if (ser->OpenRecord(h.recordID, h.version)) {
                (void)h.save(ser, false);
            }
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
            BuildIndexOnce_();
            if (auto it = g_index.find(type); it != g_index.end() && it->second && it->second->load) {
                it->second->load(ser, version, length);
            }
        }
    }
}

namespace Ser {
    void Register(const Handler& h) {
        SerFunctions::registry().push_back(h);
        SerFunctions::InvalidateIndex_();
    }

    void Install(std::uint32_t uniqueID) {
        if (static std::atomic_bool installed{false}; installed.exchange(true, std::memory_order_acq_rel)) return;
        auto* si = SKSE::GetSerializationInterface();
        si->SetUniqueID(uniqueID);
        si->SetSaveCallback(SerFunctions::OnSave);
        si->SetLoadCallback(SerFunctions::OnLoad);
        si->SetRevertCallback(SerFunctions::OnRevert);
    }
}
