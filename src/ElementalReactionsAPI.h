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

// ===================== API version =====================
// Bumped whenever the public interface changes in a breaking way.
inline constexpr std::uint32_t ERF_API_VERSION = 2;

// ===================== Public handles =====================
// Opaque numeric handles returned by the registration functions.
// They are stable for the lifetime of the game session and should
// be stored by the consumer instead of keeping raw pointers.
using ERF_ElementHandle = std::uint16_t;
using ERF_StateHandle = std::uint16_t;
using ERF_ReactionHandle = std::uint16_t;
using ERF_PreEffectHandle = std::uint16_t;

// ===================== Public helper types =====================

// Context passed to a reaction callback.
// Currently only exposes the target Actor, but can be extended
// in future API versions while remaining binary-compatible.
struct ERF_ReactionContext {
    RE::Actor* target;
};

// Callback type for reactions triggered when gauge conditions are met.
// `user` is the same pointer passed in the reaction descriptor at registration time.
using ERF_ReactionCallback = void (*)(const ERF_ReactionContext& ctx, void* user);

// Callback type for per-element pre-effects that are evaluated continuously
// while the gauge is above a minimum threshold.
// `gauge` is in [0, 100], `intensity` is computed from the descriptor fields.
// `user` is the same pointer passed in the pre-effect descriptor.
using ERF_PreEffectCallback = void (*)(RE::Actor* actor, ERF_ElementHandle element, std::uint8_t gauge, float intensity,
                                       void* user);

// ===================== Public descriptors =====================
//
// These structs describe what you want to register in the framework.
// They are copied by the implementation, so pointers only need to
// remain valid during the registration call.

// 1) Element
// Defines a single elemental type (e.g. "fire", "frost") that can
// accumulate gauge on actors and participate in reactions.
struct ERF_ElementDesc_Public {
    const char* name;         // Unique name for this element (ASCII, null-terminated).
    std::uint32_t colorRGB;   // UI color in 0xRRGGBB format.
    std::uint32_t keywordID;  // FormID of a BGSKeyword used to detect this element on magic effects (0 if unused).
    bool noMixInMixedMode;    // If true, this element is ignored in mixed-mode gauge sums.
};

// 2) Reaction (triggered when gauge reaches configuration thresholds)
// Describes a reaction that fires when the combined gauges of the
// specified elements meet the percentage conditions.
struct ERF_ReactionDesc_Public {
    const char* name;                   // Unique name for this reaction (for debugging/logging).
    const ERF_ElementHandle* elements;  // Array of element handles that must participate.
    std::uint32_t elementCount;         // Number of entries in `elements`.

    bool ordered;          // If true, the first element in `elements` must be the dominant one in the mixture.
    float minPctEach;      // Minimum fraction (0–1) for each participating element relative to the total mix.
    float minSumSelected;  // Minimum total fraction (0–1) of the selected elements relative to overall gauge.

    float cooldownSeconds;        // Reaction-level cooldown duration.
    float elementLockoutSeconds;  // Per-element lockout duration applied when this reaction triggers.

    std::uint32_t hudTint;    // Optional UI tint for the HUD when this reaction triggers (0xRRGGBB).
    ERF_ReactionCallback cb;  // Callback to run when this reaction is triggered.
    void* user;               // User data pointer passed back to `cb` unchanged.
    const char* iconName;     // Name of the HUD icon to use for this reaction (may be nullptr/empty).
};

// 3) Pre-effect (continuous effect per element)
// Describes a continuous effect tied to a single element and actor,
// evaluated as long as the element gauge is above `minGauge`.
struct ERF_PreEffectDesc_Public {
    const char* name;           // Unique name for this pre-effect (for debugging/logging).
    ERF_ElementHandle element;  // Element this pre-effect is bound to.

    std::uint8_t minGauge;  // Minimum gauge [0–100] required for this pre-effect to apply.

    // Intensity curve parameters:
    // intensity = clamp(baseIntensity + scalePerPoint * gauge,
    //                   minIntensity, maxIntensity)
    float baseIntensity;
    float scalePerPoint;
    float minIntensity;
    float maxIntensity;

    // Duration and cooldown settings for the effect instance.
    float durationSeconds;    // How long the effect should last once triggered.
    bool durationIsRealTime;  // If true, duration is in real-time seconds; otherwise game hours.
    float cooldownSeconds;    // Cooldown before this pre-effect can trigger again on the same actor.
    bool cooldownIsRealTime;  // If true, cooldown uses real-time seconds; otherwise game hours.

    ERF_PreEffectCallback cb;  // Callback invoked when the pre-effect is (re)applied.
    void* user;                // User data pointer passed back to `cb` unchanged.
};

// 4) State
// Describes a high-level state (e.g. "wet", "charged") that can
// modify gauge gain and damage taken while active on an actor.
struct ERF_StateDesc_Public {
    const char* name;         // Unique name for this state.
    std::uint32_t keywordID;  // Optional keyword FormID that can be used by other systems (0 if unused).
};

// ===================== Interface V1 =====================
//
// Single flat vtable-style struct containing all public entry points.
// Returned by RequestPluginAPI / ERF_GetAPI.

// Main API structure for version ERF_API_VERSION.
struct ERF_API_V1 {
    std::uint32_t version;  // Must be equal to ERF_API_VERSION.

    // 1) Registration
    // Called during the registration window (before Freeze) to declare
    // elements, reactions, pre-effects and states.
    ERF_ElementHandle (*RegisterElement)(const ERF_ElementDesc_Public&);
    ERF_ReactionHandle (*RegisterReaction)(const ERF_ReactionDesc_Public&);
    ERF_PreEffectHandle (*RegisterPreEffect)(const ERF_PreEffectDesc_Public&);
    ERF_StateHandle (*RegisterState)(const ERF_StateDesc_Public&);

    // 2) Multipliers
    // Configure how each state modifies gauge accumulation and health
    // damage for a given element.
    //  - gaugeMult  > 1 increases gauge gain; < 1 reduces it.
    //  - healthMult > 1 increases damage taken; < 1 reduces it.
    void (*SetElementStateMultiplier)(ERF_ElementHandle elem, ERF_StateHandle state, double gaugeMult,
                                      double healthMult);

    // 3) Dynamic states
    // Turn states on/off for a given actor at runtime.
    bool (*ActivateState)(RE::Actor*, ERF_StateHandle);
    bool (*DeactivateState)(RE::Actor*, ERF_StateHandle);

    // 4) Registration window / freeze control
    // Used to group registration calls and control when the internal
    // data structures become read-only for fast lookup.
    bool (*BeginBatchRegistration)();           // Open registration window (returns false on failure).
    void (*EndBatchRegistration)();             // Close registration window (no more changes allowed in this batch).
    void (*SetFreezeTimeoutMs)(std::uint32_t);  // Safety timeout before auto-freezing the registry.
    bool (*IsRegistrationOpen)();               // True while registration window is open.
    bool (*IsFrozen)();                         // True after registries have been frozen.
    void (*FreezeNow)();                        // Force an immediate freeze of all registries.
};

// ===================== Helper: resolve/cache the API pointer =====================
//
// Helper typedef for the provider's RequestPluginAPI function.
using ERF_RequestPluginAPI_Fn = void* (*)(std::uint32_t);

// Name of the provider DLL exporting RequestPluginAPI.
// Can be overridden at compile time if needed.
#ifndef ERF_PROVIDER_DLL_NAME
    #define ERF_PROVIDER_DLL_NAME "ElementalReactionsFramework.dll"
#endif

// Convenience helper to fetch and cache the API pointer.
// Returns nullptr if the provider DLL or the requested version
// could not be found or does not match ERF_API_VERSION.
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

// ===================== messaging helper =====================
//
// For consumers that prefer the classic SKSE messaging pattern, this
// message ID and struct can be used instead of ERF_GetAPI.
enum : std::uint32_t { ERF_MSG_GET_API = 'ERFA' };

struct ERF_API_Request {
    std::uint32_t requestedVersion;  // Version requested by the consumer (e.g. ERF_API_VERSION).
    void* outInterface;              // Pointer to be filled with ERF_API_V1* on success.
};