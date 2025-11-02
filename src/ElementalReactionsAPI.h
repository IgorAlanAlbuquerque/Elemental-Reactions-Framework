#pragma once
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

namespace RE {
    class Actor;
}

// ===================== Versão da interface =====================
inline constexpr std::uint32_t ERF_API_VERSION = 1;

// ===================== Handles públicos =====================
using ERF_ElementHandle = std::uint16_t;
using ERF_StateHandle = std::uint16_t;
using ERF_ReactionHandle = std::uint16_t;
using ERF_PreEffectHandle = std::uint16_t;

// ===================== Tipos auxiliares públicos =====================
struct ERF_ReactionContext {
    RE::Actor* target;
};

using ERF_ReactionCallback = void (*)(const ERF_ReactionContext& ctx, void* user);
using ERF_PreEffectCallback = void (*)(RE::Actor* actor, ERF_ElementHandle element, std::uint8_t gauge, float intensity,
                                       void* user);

// ===================== Descritores públicos =====================
// 1) Elemento
struct ERF_ElementDesc_Public {
    const char* name;
    std::uint32_t colorRGB;   // 0xRRGGBB
    std::uint32_t keywordID;  // FormID (0 se não usar)
};

// 2) Reação (disparo em 100%)
struct ERF_ReactionDesc_Public {
    const char* name;
    const ERF_ElementHandle* elements;
    std::uint32_t elementCount;
    bool ordered;
    float minPctEach;
    float minSumSelected;
    float cooldownSeconds;
    bool cooldownIsRealTime;
    float elementLockoutSeconds;
    bool elementLockoutIsRealTime;
    bool clearAllOnTrigger;
    std::uint32_t hudTint;
    ERF_ReactionCallback cb;
    void* user;
};

// 3) Pré-efeito (contínuo por elemento)
struct ERF_PreEffectDesc_Public {
    const char* name;
    ERF_ElementHandle element;
    std::uint8_t minGauge;
    float baseIntensity, scalePerPoint, minIntensity, maxIntensity;
    float durationSeconds;
    bool durationIsRealTime;
    float cooldownSeconds;
    bool cooldownIsRealTime;
    ERF_PreEffectCallback cb;
    void* user;
};

// 4) Estado
struct ERF_StateDesc_Public {
    const char* name;
    std::uint32_t keywordID;
};

// ===================== Interface V1 (DEFINIÇÃO ÚNICA!) =====================
struct ERF_API_V1 {
    std::uint32_t version;  // ERF_API_VERSION

    // 1) Registrar
    ERF_ElementHandle (*RegisterElement)(const ERF_ElementDesc_Public&);
    ERF_ReactionHandle (*RegisterReaction)(const ERF_ReactionDesc_Public&);
    ERF_PreEffectHandle (*RegisterPreEffect)(const ERF_PreEffectDesc_Public&);
    ERF_StateHandle (*RegisterState)(const ERF_StateDesc_Public&);

    // 2) Multiplicadores
    void (*SetElementStateMultiplier)(ERF_ElementHandle, ERF_StateHandle, double);

    // 3) Estados dinâmicos
    bool (*ActivateState)(RE::Actor*, ERF_StateHandle);
    bool (*DeactivateState)(RE::Actor*, ERF_StateHandle);

    // 4) Janela / freeze
    bool (*BeginBatchRegistration)();
    void (*EndBatchRegistration)();
    void (*SetFreezeTimeoutMs)(std::uint32_t);
    bool (*IsRegistrationOpen)();
    bool (*IsFrozen)();
    void (*FreezeNow)();
};

// ===================== Helper: resolver/cachesr a API =====================
using ERF_RequestPluginAPI_Fn = void* (*)(std::uint32_t);

#ifndef ERF_PROVIDER_DLL_NAME
    #define ERF_PROVIDER_DLL_NAME "ElementalReactionsFramework.dll"
#endif

[[nodiscard]] inline ERF_API_V1* ERF_GetAPI(std::uint32_t v = ERF_API_VERSION) noexcept {
    static HMODULE s_mod = []() noexcept { return ::GetModuleHandleA(ERF_PROVIDER_DLL_NAME); }();
    if (!s_mod) return nullptr;

    static ERF_RequestPluginAPI_Fn s_req = []() noexcept {
        return reinterpret_cast<ERF_RequestPluginAPI_Fn>(::GetProcAddress(s_mod, "RequestPluginAPI"));
    }();
    if (!s_req) return nullptr;

    auto* p = static_cast<ERF_API_V1*>(s_req(v));
    if (!p) return nullptr;
    return (p->version == v) ? p : nullptr;
}

// (Opcional) messaging legado
enum : std::uint32_t { ERF_MSG_GET_API = 'ERFA' };
struct ERF_API_Request {
    std::uint32_t requestedVersion;
    void* outInterface;
};