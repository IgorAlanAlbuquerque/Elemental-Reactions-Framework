#include "ElementalStates.h"

#include <algorithm>
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

    using StoreT = std::unordered_map<RE::FormID, PerActorStates>;

    StoreT& GetStore() {
        static StoreT store;  // NOSONAR - intentional function-local static singleton
        return store;
    }

    inline RE::FormID IdOf(RE::Actor* a) { return a ? a->GetFormID() : 0; }

    namespace StatesSer {
        constexpr std::uint32_t kRecordID = FOURCC('E', 'S', 'T', 'S');
        constexpr std::uint32_t kVersion = 2;
    }

    bool SaveStateHandles(SKSE::SerializationInterface* ser, const std::vector<ERF_StateHandle>& handles) {
        return std::ranges::all_of(
            handles, [ser](const auto& handle) { return ser->WriteRecordData(&handle, sizeof(ERF_StateHandle)); });
    }

    bool SaveActorStates(SKSE::SerializationInterface* ser, RE::FormID id, const PerActorStates& st) {
        if (!ser->WriteRecordData(&id, sizeof(id))) return false;

        auto n = static_cast<std::uint16_t>(
            std::min<std::size_t>(st.active.size(), std::numeric_limits<std::uint16_t>::max()));
        if (!ser->WriteRecordData(&n, sizeof(n))) return false;

        if (n > 0) {
            std::vector<ERF_StateHandle> tmp;
            tmp.reserve(st.active.size());
            for (auto sh : st.active) tmp.push_back(sh);
            std::ranges::sort(tmp);

            if (tmp.size() > n) {
                tmp.resize(n);
            }

            if (!SaveStateHandles(ser, tmp)) return false;
        }
        return true;
    }

    bool Save(SKSE::SerializationInterface* ser, bool dryRun) {
        if (dryRun) {
            return !GetStore().empty();
        }

        if (const auto countActors = static_cast<std::uint32_t>(GetStore().size());
            !ser->WriteRecordData(&countActors, sizeof(countActors)))
            return false;

        for (const auto& [id, st] : GetStore()) {
            if (!SaveActorStates(ser, id, st)) return false;
        }

        return true;
    }

    bool LoadStateHandles(SKSE::SerializationInterface* ser, std::unordered_set<ERF_StateHandle>& setRef,
                          std::uint16_t n) {
        for (std::uint16_t k = 0; k < n; ++k) {
            ERF_StateHandle sh{};
            if (!ser->ReadRecordData(&sh, sizeof(sh))) return false;
            if (sh != 0) setRef.insert(sh);
        }
        return true;
    }

    bool SkipStateHandles(SKSE::SerializationInterface* ser, std::uint16_t n) {
        for (std::uint16_t k = 0; k < n; ++k) {
            ERF_StateHandle dummy{};
            if (!ser->ReadRecordData(&dummy, sizeof(dummy))) return false;
        }
        return true;
    }

    bool LoadActorState(SKSE::SerializationInterface* ser, RE::FormID oldID) {
        RE::FormID newID{};
        if (!ser->ResolveFormID(oldID, newID)) {
            std::uint16_t n{};
            if (!ser->ReadRecordData(&n, sizeof(n))) return false;
            return SkipStateHandles(ser, n);
        }

        std::uint16_t n{};
        if (!ser->ReadRecordData(&n, sizeof(n))) return false;

        auto& setRef = GetStore()[newID].active;
        return LoadStateHandles(ser, setRef, n);
    }

    bool Load(SKSE::SerializationInterface* ser, std::uint32_t version, std::uint32_t) {
        GetStore().clear();

        if (version != StatesSer::kVersion) {
            return false;
        }

        std::uint32_t countActors{};
        if (!ser->ReadRecordData(&countActors, sizeof(countActors))) return false;

        for (std::uint32_t i = 0; i < countActors; ++i) {
            RE::FormID oldID{};
            if (!ser->ReadRecordData(&oldID, sizeof(oldID))) return false;

            if (!LoadActorState(ser, oldID)) return false;
        }

        return true;
    }

    void Revert() { GetStore().clear(); }
}

void ElementalStates::RegisterStore() {
    Ser::Register({StatesSer::kRecordID, StatesSer::kVersion, &Save, &Load, &Revert});
}

bool ElementalStates::SetActive(RE::Actor* a, ERF_StateHandle sh, bool value) {
    if (!a || sh == 0) return false;
    const auto id = IdOf(a);
    if (id == 0) return false;
    auto& st = GetStore()[id].active;
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
    auto it = GetStore().find(id);
    if (it == GetStore().end()) return false;
    return it->second.active.contains(sh);
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
    GetStore().erase(id);
}

void ElementalStates::ClearAll() { GetStore().clear(); }

std::vector<ERF_StateHandle> ElementalStates::GetActive(RE::Actor* a) {
    std::vector<ERF_StateHandle> out;
    if (!a) return out;
    const auto id = IdOf(a);
    auto it = GetStore().find(id);
    if (it == GetStore().end()) return out;
    out.reserve(it->second.active.size());
    for (auto sh : it->second.active) out.push_back(sh);
    return out;
}

double ElementalStates::GetGaugeMultiplierFor(RE::Actor* a, ERF_ElementHandle elem) {
    if (!a || elem == 0) {
        return 1.0;
    }

    const auto active = GetActive(a);
    if (active.empty()) {
        return 1.0;
    }

    auto const& SR = StateRegistry::get();
    double mult = 1.0;

    const auto elemHandle = elem;

    for (ERF_StateHandle sh : active) {
        if (sh == 0) {
            continue;
        }
        mult *= SR.getGaugeMultiplier(sh, elemHandle);
    }

    return mult;
}

double ElementalStates::GetHealthMultiplierFor(RE::Actor* a, ERF_ElementHandle elem) {
    if (!a || elem == 0) {
        return 1.0;
    }

    const auto active = GetActive(a);
    if (active.empty()) {
        return 1.0;
    }

    auto const& SR = StateRegistry::get();
    double mult = 1.0;

    const auto elemHandle = elem;

    for (ERF_StateHandle sh : active) {
        if (sh == 0) {
            continue;
        }
        mult *= SR.getHealthMultiplier(sh, elemHandle);
    }

    return mult;
}