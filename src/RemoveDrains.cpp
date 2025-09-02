#include "RemoveDrains.h"

namespace RemoveDrains {
    void RemoveDrainFromShockAndFrost() {
        using enum RE::ActorValue;
        std::vector<std::pair<RE::FormID, RE::ActorValue>> effectIDs = {
            {0x00013CAB, kMagicka}, {0x000E4CB6, kMagicka}, {0x0010CBDF, kMagicka}, {0x0001CEA8, kMagicka},
            {0x0010F7EF, kMagicka}, {0x0005DBAE, kMagicka}, {0x0004EFDE, kMagicka}, {0x000D22FA, kMagicka},
            {0x000EA65A, kStamina}, {0x00013CAA, kStamina}, {0x0010CBDE, kStamina}, {0x0001CEA2, kStamina},
            {0x0010F7F0, kStamina}, {0x0001CEA3, kStamina}, {0x000EA076, kStamina}, {0x0007E8E2, kStamina},
            {0x00066335, kStamina}, {0x00066334, kStamina}};

        for (const auto& [formID, expectedAV] : effectIDs) {
            auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(formID);
            if (effect && effect->data.secondaryAV == expectedAV) {
                effect->data.secondaryAV = kNone;
            }
        }
    }
}
