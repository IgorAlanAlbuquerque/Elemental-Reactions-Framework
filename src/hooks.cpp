#include "hooks.h"

namespace Hooks {
    class ActiveEffectHook {
    public:
        static void Install() {
            REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_ActiveEffect[0]};
            original_Update = vtable.write_vfunc(0x8, &Thunk);
            SKSE::log::info("Hooked ActiveEffect::Update");
        }

    private:
        static void Thunk(RE::ActiveEffect* ae, float delta) {
            auto effect = ae ? (ae->effect ? ae->effect->baseEffect : nullptr) : nullptr;
            auto target = ae ? ae->GetTargetActor() : nullptr;

            if (effect && target) {
                static const auto shockKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDamageShock");
                static const auto frostKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDamageFrost");

                bool isShock = shockKW && effect->HasKeyword(shockKW);
                bool isFrost = frostKW && effect->HasKeyword(frostKW);

                if (isShock || isFrost) {
                    if (effect->data.archetype == RE::EffectArchetypes::ArchetypeID::kValueModifier) {
                        if (effect->IsDetrimental()) {
                            auto avEnum = effect->data.primaryAV;
                            if (avEnum == RE::ActorValue::kMagicka || avEnum == RE::ActorValue::kStamina) {
                                float originalMagnitude = ae->magnitude;
                                ae->magnitude = 0.0f;

                                SKSE::log::info("Blocked {} drain effect on {}", effect->GetName(), target->GetName());

                                original_Update(ae, delta);

                                ae->magnitude = originalMagnitude;
                                return;
                            }
                        }
                    }
                }
            }

            original_Update(ae, delta);
        }

        static inline REL::Relocation<decltype(&Thunk)> original_Update;
    };

    void Install() { ActiveEffectHook::Install(); }
}