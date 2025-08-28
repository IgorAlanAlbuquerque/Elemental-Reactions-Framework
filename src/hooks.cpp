#include "hooks.h"

namespace Hooks {
    class ActiveEffectHook {
    public:
        static void Install() {
            REL::Relocation<std::uintptr_t> target{RE::VTABLE_ActiveEffect[0]};
            auto funcAddr = target.write_vfunc(0x8, 0);

            SKSE::Trampoline& trampoline = SKSE::GetTrampoline();
            original_Update = trampoline.write_call<5>(funcAddr, Thunk);
        }

    private:
        static void Thunk(RE::ActiveEffect* ae, float delta) {
            const RE::EffectSetting* effect = nullptr;
            if (ae && ae->effect) {
                effect = ae->effect->baseEffect;
            }

            if (!effect) {
                original_Update(ae, delta);
                return;
            }
            if (const RE::Actor* target = ae ? ae->GetTargetActor() : nullptr) {
                static const auto shockKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDamageShock");
                static const auto frostKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDamageFrost");

                bool isShock = shockKW && effect->HasKeyword(shockKW);
                bool isFrost = frostKW && effect->HasKeyword(frostKW);

                if ((isShock || isFrost) &&
                    effect->data.archetype == RE::EffectArchetypes::ArchetypeID::kValueModifier &&
                    effect->IsDetrimental()) {
                    auto avEnum = effect->data.primaryAV;
                    if (avEnum == RE::ActorValue::kMagicka || avEnum == RE::ActorValue::kStamina) {
                        float originalMagnitude = ae->magnitude;
                        ae->magnitude = 0.0f;
                        original_Update(ae, delta);
                        ae->magnitude = originalMagnitude;
                        return;
                    }
                }
            }

            original_Update(ae, delta);
        }

        static inline REL::Relocation<decltype(&Thunk)> original_Update;
    };

    void Install() { ActiveEffectHook::Install(); }
}