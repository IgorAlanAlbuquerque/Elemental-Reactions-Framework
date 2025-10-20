#include "ElementalStates.h"

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "../common/Helpers.h"
#include "../common/PluginSerialization.h"
#include "ElementalGauges.h"
#include "erf_state.h"

namespace {
    struct PerActorStates {
        std::unordered_set<ERF_StateHandle> active;
    };

    std::unordered_map<RE::FormID, PerActorStates> g_store;

    inline RE::FormID IdOf(RE::Actor* a) { return a ? a->GetFormID() : 0; }

    namespace StatesSer {
        constexpr std::uint32_t kRecordID = 'ESTS';
        constexpr std::uint32_t kVersion = 2;
    }

    bool Save(SKSE::SerializationInterface* ser, bool dryRun) {
        if (dryRun) {
            return !g_store.empty();
        }
        const auto countActors = static_cast<std::uint32_t>(g_store.size());
        if (!ser->WriteRecordData(&countActors, sizeof(countActors))) return false;

        for (const auto& [id, st] : g_store) {
            if (!ser->WriteRecordData(&id, sizeof(id))) return false;

            std::uint16_t n = static_cast<std::uint16_t>(
                std::min<std::size_t>(st.active.size(), std::numeric_limits<std::uint16_t>::max()));
            if (!ser->WriteRecordData(&n, sizeof(n))) return false;

            if (n > 0) {
                std::vector<ERF_StateHandle> tmp;
                tmp.reserve(n);
                for (auto sh : st.active) tmp.push_back(sh);
                std::sort(tmp.begin(), tmp.end());

                for (std::uint16_t i = 0; i < n; ++i) {
                    if (!ser->WriteRecordData(&tmp[i], sizeof(ERF_StateHandle))) return false;
                }
            }
        }
        return true;
    }

    bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t) {
        g_store.clear();

        if (version == StatesSer::kVersion) {
            std::uint32_t countActors{};
            if (!ser->ReadRecordData(&countActors, sizeof(countActors))) return false;

            for (std::uint32_t i = 0; i < countActors; ++i) {
                RE::FormID oldID{};
                if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;

                RE::FormID newID{};
                if (!ser->ResolveFormID(oldID, newID)) {
                    std::uint16_t n{};
                    if (!ser->ReadRecordData(&n, sizeof(n))) return false;
                    for (std::uint16_t k = 0; k < n; ++k) {
                        ERF_StateHandle dummy{};
                        if (!ser->ReadRecordData(&dummy, sizeof(dummy))) return false;
                    }
                    continue;
                }

                std::uint16_t n{};
                if (!ser->ReadRecordData(&n, sizeof(n))) return false;

                auto& setRef = g_store[newID].active;
                for (std::uint16_t k = 0; k < n; ++k) {
                    ERF_StateHandle sh{};
                    if (!ser->ReadRecordData(&sh, sizeof(sh))) return false;
                    if (sh != 0) setRef.insert(sh);
                }
            }
            return true;
        }

        g_store.clear();
        return false;
    }

    void Revert() { g_store.clear(); }
}

void ElementalStates::RegisterStore() {
    Ser::Register({StatesSer::kRecordID, StatesSer::kVersion, &Save, &Load, &Revert});
}

bool ElementalStates::SetActive(RE::Actor* a, ERF_StateHandle sh, bool value) {
    if (!a || sh == 0) return false;
    const auto id = IdOf(a);
    if (id == 0) return false;
    auto& st = g_store[id].active;
    if (value) {
        st.insert(sh);
    } else {
        st.erase(sh);
    }
    return true;
}

bool ElementalStates::IsActive(RE::Actor* a, ERF_StateHandle sh) {
    if (!a || sh == 0) return false;
    const auto id = IdOf(a);
    auto it = g_store.find(id);
    if (it == g_store.end()) return false;
    return it->second.active.count(sh) > 0;
}

void ElementalStates::Activate(RE::Actor* a, ERF_StateHandle sh) {
    (void)SetActive(a, sh, true);
    ElementalGauges::InvalidateStateMultipliers(a);
}

void ElementalStates::Deactivate(RE::Actor* a, ERF_StateHandle sh) {
    (void)SetActive(a, sh, false);
    ElementalGauges::InvalidateStateMultipliers(a);
}

void ElementalStates::Clear(RE::Actor* a) {
    if (!a) return;
    const auto id = IdOf(a);
    g_store.erase(id);
}

void ElementalStates::ClearAll() { g_store.clear(); }

std::vector<ERF_StateHandle> ElementalStates::GetActive(RE::Actor* a) {
    std::vector<ERF_StateHandle> out;
    if (!a) return out;
    const auto id = IdOf(a);
    auto it = g_store.find(id);
    if (it == g_store.end()) return out;
    out.reserve(it->second.active.size());
    for (auto sh : it->second.active) out.push_back(sh);
    return out;
}