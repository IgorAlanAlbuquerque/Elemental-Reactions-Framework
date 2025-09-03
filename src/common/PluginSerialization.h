#pragma once
#include <cstdint>
#include <vector>

#include "SKSE/SKSE.h"

namespace Ser {

    using SaveFn = bool (*)(SKSE::SerializationInterface*);
    using LoadFn = bool (*)(SKSE::SerializationInterface*, std::uint32_t, std::uint32_t);
    using RevertFn = void (*)();

    struct Handler {
        std::uint32_t recordID;
        std::uint32_t version;
        SaveFn save;
        LoadFn load;
        RevertFn revert;
    };

    void Register(const Handler& h);

    void Install(std::uint32_t uniqueID);

}

namespace SerFunctions {
    void OnSave(SKSE::SerializationInterface* ser);
    void OnLoad(SKSE::SerializationInterface* ser);
    void OnRevert(SKSE::SerializationInterface* ser);
}
