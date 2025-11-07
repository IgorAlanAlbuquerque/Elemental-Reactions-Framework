#pragma once

#include <atomic>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"

namespace ElementalGaugesHook {
    extern std::atomic_bool ALLOW_HUD_TICK;
    extern RE::EffectSetting* g_mgefGaugeAcc;
    void StartHUDTick();
    void StopHUDTick();
    void Install();
    void RegisterAEEventSink();
    void InitCarrierRefs();
}
