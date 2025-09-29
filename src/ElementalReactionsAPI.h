#pragma once
#include <cstdint>

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>  // HMODULE, GetModuleHandleA, GetProcAddress

namespace RE {
    class Actor;
}

// =====================
// Versão da interface
// =====================
inline constexpr std::uint32_t ERF_API_VERSION = 1;

// =====================
// Forward declarations
// =====================
struct ERF_API_V1;  // garante que o tipo existe antes do helper

using ERF_RequestPluginAPI_Fn = void* (*)(std::uint32_t /*requestedVersion*/);

// =====================
// Helper (padrão TrueHUD)
// -> use o nome EXATO da sua DLL final
// =====================
[[nodiscard]] inline ERF_API_V1* ERF_GetAPI(std::uint32_t v = ERF_API_VERSION) {
    HMODULE h = ::GetModuleHandleA("ElementalReactionsFramework.dll");  // ajuste se o nome mudar
    if (!h) return nullptr;

    auto fn = reinterpret_cast<ERF_RequestPluginAPI_Fn>(::GetProcAddress(h, "RequestPluginAPI"));
    if (!fn) return nullptr;

    return static_cast<ERF_API_V1*>(fn(v));
}

// (Opcional) Messaging legado — mantenha apenas se quiser fallback
enum : std::uint32_t { ERF_MSG_GET_API = 'ERFA' };  // 0x41524645
struct ERF_API_Request {
    std::uint32_t requestedVersion;  // ERF_API_VERSION
    void* outInterface;              // (saída) ERF_API_V1*
};

// =====================
// Handles públicos
// =====================
using ERF_ElementHandle = std::uint16_t;
using ERF_StateHandle = std::uint16_t;
using ERF_ReactionHandle = std::uint16_t;
using ERF_PreEffectHandle = std::uint16_t;

// =====================
// Descritores públicos
// =====================
// 1) Elemento
struct ERF_ElementDesc_Public {
    const char* name;         // "Fire", "Frost", "Shock", ...
    std::uint32_t colorRGB;   // 0xRRGGBB
    std::uint32_t keywordID;  // FormID da keyword (0 se não usar)
};

struct ERF_ReactionContext {
    RE::Actor* target;
};

// Callback que o MOD consumidor fornece
using ERF_ReactionCallback = void (*)(const ERF_ReactionContext& ctx, void* user);
using ERF_PreEffectCallback = void (*)(RE::Actor* actor, ERF_ElementHandle element, std::uint8_t gauge, float intensity,
                                       void* user);

// 2) Reação (disparo em 100% - “explosão”)
struct ERF_ReactionDesc_Public {
    const char* name;

    const ERF_ElementHandle* elements;
    std::uint32_t elementCount;

    bool ordered;
    std::uint32_t minTotalGauge;  // tipicamente 100
    float minPctEach;             // 0..1
    float minSumSelected;         // 0..1

    float cooldownSeconds;
    bool cooldownIsRealTime;
    float elementLockoutSeconds;
    bool elementLockoutIsRealTime;
    bool clearAllOnTrigger;

    const char* hudIconPath;    // opcional
    std::uint32_t hudIconTint;  // opcional

    // NOVO: callback + cookie do consumidor
    ERF_ReactionCallback cb;  // pode ser nullptr (reação “sem efeito” só HUD)
    void* user;               // cookie repassado no disparo
};

// 3) Pré-efeito (contínuo por elemento, baseado no gauge desse elemento)
struct ERF_PreEffectDesc_Public {
    const char* name;
    ERF_ElementHandle element;
    std::uint8_t minGauge;

    float baseIntensity;
    float scalePerPoint;
    float minIntensity;
    float maxIntensity;

    float durationSeconds;  // 0 = contínuo (sem expiração gerida aqui)
    bool durationIsRealTime;

    float cooldownSeconds;  // 0 = sem throttling
    bool cooldownIsRealTime;

    // NOVO: callback do consumidor + cookie
    ERF_PreEffectCallback cb;  // pode ser nullptr (sem efeito; HUD/telemetria etc.)
    void* user;
};

// 4) Estado elemental (dinâmico)
struct ERF_StateDesc_Public {
    const char* name;         // "Wet", "Rubber", ...
    std::uint32_t keywordID;  // FormID da keyword (0 se não usar)
};

// =====================
// Interface V1 (C-friendly)
// =====================
struct ERF_API_V1 {
    std::uint32_t version;  // ERF_API_VERSION

    // 1) Registrar elemento
    ERF_ElementHandle (*RegisterElement)(const ERF_ElementDesc_Public& desc);

    // 2) Registrar reação (explosão em 100)
    ERF_ReactionHandle (*RegisterReaction)(const ERF_ReactionDesc_Public& desc);

    // 3) Registrar pré-efeito (contínuo por elemento)
    ERF_PreEffectHandle (*RegisterPreEffect)(const ERF_PreEffectDesc_Public& desc);

    // 4) Registrar estado elemental
    ERF_StateHandle (*RegisterState)(const ERF_StateDesc_Public& desc);

    // 5) Setar multiplicador de um estado X para um elemento Y
    void (*SetElementStateMultiplier)(ERF_ElementHandle element, ERF_StateHandle state, double multiplier);

    // 6) Ativar estado X para ator Y
    bool (*ActivateState)(RE::Actor* actor, ERF_StateHandle state);

    // 7) Desativar estado X para ator Y
    bool (*DeactivateState)(RE::Actor* actor, ERF_StateHandle state);
};
