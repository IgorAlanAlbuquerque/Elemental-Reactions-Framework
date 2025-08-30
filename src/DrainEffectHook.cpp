#include "DrainEffectHook.h"

namespace Hooks {
    void DrainEffectHook::Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ValueModifierEffect[0]};
        _ModifyActorValue = vtbl.write_vfunc<FnModifyAV>(0x20, Hooked_ModifyAV);
        spdlog::info("[DrainBlockHook] Hook em ValueModifierEffect");
    }

    void DrainEffectHook::Hooked_ModifyAV(RE::ValueModifierEffect* eff, RE::Actor* actor, float val,
                                          RE::ActorValue av) {
        spdlog::info("[DrainBlockHook] Update chamado: delta={}");
        _ModifyActorValue(eff, actor, val, av);
    }

    void install() { DrainEffectHook::Install(); }
}