#include "PluginSerialization.h"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <unordered_map>

namespace SerFunctions {
    using Handlers = std::vector<Ser::Handler>;
    using IndexMap = std::unordered_map<std::uint32_t, const Ser::Handler*>;

    Handlers& registry() noexcept {
        static Handlers r;  // NOSONAR: estado centralizado, mutável por design
        return r;
    }
    static IndexMap& index() noexcept {
        static IndexMap idx;  // NOSONAR: estado centralizado
        return idx;
    }

    static bool& indexBuilt() noexcept {
        static bool built = false;  // NOSONAR: estado centralizado
        return built;
    }

    static void BuildIndexOnce_() {
        auto& idx = index();
        auto& built = indexBuilt();

        if (built) return;

        idx.clear();
        idx.reserve(registry().size());
        for (const auto& h : registry()) {
            idx.emplace(h.recordID, &h);
        }
        built = true;
    }

    static void InvalidateIndex_() { indexBuilt() = false; }

    void OnSave(SKSE::SerializationInterface* ser) {
        BuildIndexOnce_();
        for (const auto& h : registry()) {
            if (!h.save) continue;
            if (const bool hasData = h.save(ser, true); !hasData) continue;
            if (ser->OpenRecord(h.recordID, h.version)) {
                (void)h.save(ser, false);
            }
        }
    }

    void OnLoad(SKSE::SerializationInterface* ser) {
        std::uint32_t type{};
        std::uint32_t version{};
        std::uint32_t length{};
        while (ser->GetNextRecordInfo(type, version, length)) {
            BuildIndexOnce_();
            auto& idx = index();
            if (auto it = idx.find(type); it != idx.end() && it->second && it->second->load) {
                it->second->load(ser, version, length);
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*) {
        for (const auto& h : registry()) {
            if (h.revert) h.revert();
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
