#pragma once

#include <atomic>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"

namespace ElementalGaugesHook {
    extern std::atomic_bool ALLOW_HUD_TICK;
    static RE::EffectSetting* g_mgefGaugeAcc = nullptr;
    void StartHUDTick();
    void StopHUDTick();
    void Install();
    void RegisterAEEventSink();
    void InitCarrierRefs();
}
