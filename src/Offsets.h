#pragma once
#include "SKSE/Impl/PCH.h"

static const float* const g_deltaTime =
    reinterpret_cast<const float*>(RELOCATION_ID(523660, 410199).address());  // NOSONAR - interop
static const float* const g_deltaTimeRealTime =
    reinterpret_cast<const float*>(RELOCATION_ID(523661, 410200).address());  // NOSONAR - interop
static const float* const g_DurationOfApplicationRunTimeMS =
    reinterpret_cast<const float*>(RELOCATION_ID(523662, 410201).address());  // NOSONAR - interop
static const float* const g_fHUDOpacity =
    reinterpret_cast<const float*>(RELOCATION_ID(510579, 383659).address());  // NOSONAR - interop
static const float* const g_fShoutMeterEndDuration =
    reinterpret_cast<const float*>(RELOCATION_ID(510025, 382842).address());  // NOSONAR - interop
static const uintptr_t g_worldToCamMatrix = RELOCATION_ID(519579, 406126).address();
static const RE::NiRect<float>* const g_viewPort = reinterpret_cast<const RE::NiRect<float>*>(  // NOSONAR - interop
    RELOCATION_ID(519618, 406160).address());
static const float* const g_fNear =
    reinterpret_cast<const float*>(RELOCATION_ID(517032, 403540).address() +  // NOSONAR - interop
                                   0x40);
static const float* const g_fFar =
    reinterpret_cast<const float*>(RELOCATION_ID(517032, 403540).address() +  // NOSONAR - interop
                                   0x44);

typedef uint32_t(_fastcall* tIsSentient)(RE::Actor* a_this);
static const REL::Relocation<tIsSentient> IsSentient{RELOCATION_ID(36889, 37913)};

typedef uint32_t(_fastcall* tGetSoulType)(uint16_t a_actorLevel, uint8_t a_isActorSentient);
static const REL::Relocation<tGetSoulType> GetSoulType{RELOCATION_ID(25933, 26520)};

typedef void(_fastcall* tSetHUDMode)(const char* a_mode, bool a_enable);
static const REL::Relocation<tSetHUDMode> SetHUDMode{RELOCATION_ID(50747, 51642)};

typedef void(_fastcall* tFlashHUDMenuMeter)(RE::ActorValue a_actorValue);
static const REL::Relocation<tFlashHUDMenuMeter> FlashHUDMenuMeter{RELOCATION_ID(51907, 52845)};
