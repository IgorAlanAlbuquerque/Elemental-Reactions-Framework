#pragma once

#include "RE/A/Actor.h"
#include "RE/A/ActorValues.h"
#include "RE/V/ValueModifierEffect.h"
#include "REL/Relocation.h"

namespace Hooks {
    class DrainEffectHook {
    public:
        static void Install();

    private:
        using FnModifyAV = void(RE::ValueModifierEffect*, RE::Actor*, float, RE::ActorValue);
        static inline REL::Relocation<FnModifyAV*> _ModifyActorValue;
        static void Hooked_ModifyAV(RE::ValueModifierEffect* eff, RE::Actor* actor, float val, RE::ActorValue av);
    };
    void Install();
}