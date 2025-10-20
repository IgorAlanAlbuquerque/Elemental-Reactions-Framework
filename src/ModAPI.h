#pragma once
#include "ElementalReactionsAPI.h"

namespace ERF::API {
    ERF_API_V1* Get();
    void OpenRegistrationWindowAndScheduleFreeze();

    struct FrozenCaps {
        std::uint16_t numElements{0};
        std::uint16_t numStates{0};
        std::uint16_t numReactions{0};
        bool ready{false};
    };

    const FrozenCaps& Caps();
}