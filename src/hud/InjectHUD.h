#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace HUD {
    void InitTrueHUDInjection();
    void EnsureGaugeFor(RE::GFxMovieView* view, RE::Actor* a);
    void UpdateGaugeFor(RE::GFxMovieView* view, RE::Actor* a, std::uint8_t fire, std::uint8_t frost, std::uint8_t shock,
                        int iconId, std::uint32_t tintRGB);
    void RemoveGaugeFor(RE::GFxMovieView* view, RE::Actor* a);
}