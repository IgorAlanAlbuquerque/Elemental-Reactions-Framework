#pragma once

#include <atomic>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"

namespace ElementalGaugesHook {
    std::atomic_bool& AllowHudTickFlag() noexcept;
    RE::EffectSetting*& GaugeAccEffect() noexcept;
    void StartHUDTick();
    void StopHUDTick();
    void Install();
    void RegisterAEEventSink();
    void InitCarrierRefs();
}
