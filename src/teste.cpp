// test.cpp — MOD consumidor da ERF (padrão TrueHUD)
#include <algorithm>  // std::clamp
#include <cmath>
#include <unordered_map>

#include "ElementalReactionsAPI.h"  // ERF_GetAPI(), tipos públicos
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "spdlog/spdlog.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

// === config de teste ===
// Se você estiver testando a ERF sozinho, pode forçar o freeze imediato após EndBatchRegistration.
// DESATIVE em builds “reais” para não atropelar outros consumidores.
#define FORCE_FREEZE_AFTER_END 0

// -------------------- Guarda da API --------------------
static ERF_API_V1* g_erf = nullptr;

static ERF_StateHandle g_stateWet{};

static ERF_API_V1* AcquireERF() {
    if (g_erf) return g_erf;

    g_erf = ERF_GetAPI(ERF_API_VERSION);  // v1 estendida
    if (!g_erf) {
        return nullptr;
    }
    return g_erf;
}

// -------------------- Callback de reação (explosão 100%) --------------------
static void OnReactionFire(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction fire 1 fired on actor {:08X}", ctx.target->GetFormID());
    }
}
static void OnReactionFireS(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction fire 2 fired on actor {:08X}", ctx.target->GetFormID());
    }
}
static void OnReactionFireT(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction fire 3 fired on actor {:08X}", ctx.target->GetFormID());
    }
}
static void OnReactionFrost(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction frost fired on actor {:08X}", ctx.target->GetFormID());
    }
}
static void OnReactionShock(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction shock fired on actor {:08X}", ctx.target->GetFormID());
    }
}

// -------------------- Callback do pré-efeito (Shock Slow contínuo) ---------
namespace {
    static std::unordered_map<RE::FormID, float> g_lastShockSlow;
}

static void ApplyShockSlow(RE::Actor* actor, ERF_ElementHandle /*element*/, std::uint8_t /*gauge*/, float intensity,
                           void* /*user*/) {
    if (!actor) return;

    const float newPenalty = std::clamp(intensity, 0.0f, 1.0f) * 100.0f;

    const RE::FormID id = actor->GetFormID();
    const float prev = (g_lastShockSlow.contains(id) ? g_lastShockSlow[id] : 0.0f);
    const float delta = newPenalty - prev;

    if (std::fabs(delta) > 1e-3f) {
        if (auto* avOwner = actor->AsActorValueOwner()) {
            const float current = avOwner->GetActorValue(RE::ActorValue::kSpeedMult);
            avOwner->SetActorValue(RE::ActorValue::kSpeedMult, current - delta);
            g_lastShockSlow[id] = newPenalty;
        }
    }
    if (newPenalty <= 0.0f) {
        g_lastShockSlow.erase(id);
    }
}

// -------------------- Registro via API --------------------
static void RegisterEverything_Core() {
    auto* api = AcquireERF();
    if (!api) return;

    // 1) Elementos (com keywords vanilla)
    constexpr std::uint32_t kMagicDamageFire = 0x0001CEAD;
    constexpr std::uint32_t kMagicDamageFrost = 0x0001CEAE;
    constexpr std::uint32_t kMagicDamageShock = 0x0001CEAF;

    ERF_ElementHandle fire{}, frost{}, shock{};

    {
        ERF_ElementDesc_Public d{"Fire", 0xF04A3A, kMagicDamageFire, true};
        fire = api->RegisterElement(d);
    }
    {
        ERF_ElementDesc_Public d{"Frost", 0x4FB2FF, kMagicDamageFrost, false};
        frost = api->RegisterElement(d);
    }
    {
        ERF_ElementDesc_Public d{"Shock", 0xFFD02A, kMagicDamageShock, false};
        shock = api->RegisterElement(d);
    }

    // 2) Estados
    ERF_StateHandle wet{};
    {
        ERF_StateDesc_Public s{"Wet", 0};
        wet = api->RegisterState(s);
        g_stateWet = wet;
    }

    // 3) Multiplicadores estado×elemento
    api->SetElementStateMultiplier(fire, wet, 0.10, 0.10);
    api->SetElementStateMultiplier(frost, wet, 1.30, 1.0);
    api->SetElementStateMultiplier(shock, wet, 1.9, 1.30);

    // 4) Pré-efeito: Shock Slow (≥50, 10% em 50, +1%/pt, cap 60%)
    {
        ERF_PreEffectDesc_Public p{};
        p.name = "Shock_Slow_50p";
        p.element = shock;
        p.minGauge = 50;
        p.baseIntensity = 0.10f;
        p.scalePerPoint = 0.01f;
        p.minIntensity = 0.0f;
        p.maxIntensity = 0.60f;
        p.durationSeconds = 0.0f;
        p.durationIsRealTime = true;
        p.cooldownSeconds = 0.0f;
        p.cooldownIsRealTime = true;
        p.cb = &ApplyShockSlow;
        p.user = nullptr;

        api->RegisterPreEffect(p);
    }

    // 5) Reação simples: Solo Fire 85%
    {
        // --- Fire ---
        ERF_ElementHandle elemsFire[] = {fire};
        ERF_ReactionDesc_Public rF{};
        rF.name = "Solo_Fire_85";
        rF.elements = elemsFire;
        rF.elementCount = 1;
        rF.ordered = false;
        rF.minPctEach = 0.85f;
        rF.minSumSelected = 0.0f;
        rF.cooldownSeconds = 0.5f;
        rF.cooldownIsRealTime = true;
        rF.elementLockoutSeconds = 10.0f;
        rF.elementLockoutIsRealTime = true;
        rF.clearAllOnTrigger = true;
        rF.hudTint = 0xF04A3A;
        rF.cb = &OnReactionFire;
        rF.user = nullptr;
        rF.iconName = "ERF_ICON__erf_core__fire";
        api->RegisterReaction(rF);

        ERF_ElementHandle elemsFireS[] = {fire};
        ERF_ReactionDesc_Public rFs{};
        rFs.name = "Solo_Fire_90";
        rFs.elements = elemsFireS;
        rFs.elementCount = 1;
        rFs.ordered = false;
        rFs.minPctEach = 0.9f;
        rFs.minSumSelected = 0.0f;
        rFs.cooldownSeconds = 0.5f;
        rFs.cooldownIsRealTime = true;
        rFs.elementLockoutSeconds = 10.0f;
        rFs.elementLockoutIsRealTime = true;
        rFs.clearAllOnTrigger = true;
        rFs.hudTint = 0xF04A3A;
        rFs.cb = &OnReactionFireS;
        rFs.user = nullptr;
        rFs.iconName = "ERF_ICON__erf_core__fire";
        api->RegisterReaction(rFs);

        ERF_ElementHandle elemsFireT[] = {fire};
        ERF_ReactionDesc_Public rFt{};
        rFt.name = "Solo_Fire_80";
        rFt.elements = elemsFireT;
        rFt.elementCount = 1;
        rFt.ordered = false;
        rFt.minPctEach = 0.80f;
        rFt.minSumSelected = 0.0f;
        rFt.cooldownSeconds = 0.5f;
        rFt.cooldownIsRealTime = true;
        rFt.elementLockoutSeconds = 10.0f;
        rFt.elementLockoutIsRealTime = true;
        rFt.clearAllOnTrigger = true;
        rFt.hudTint = 0xF04A3A;
        rFt.cb = &OnReactionFireT;
        rFt.user = nullptr;
        rFt.iconName = "ERF_ICON__erf_core__fire";
        api->RegisterReaction(rFt);

        // --- Frost ---
        ERF_ElementHandle elemsFrost[] = {frost};
        ERF_ReactionDesc_Public rFr{};
        rFr.name = "Solo_Frost_85";
        rFr.elements = elemsFrost;
        rFr.elementCount = 1;
        rFr.ordered = false;
        rFr.minPctEach = 0.85f;
        rFr.minSumSelected = 0.0f;
        rFr.cooldownSeconds = 0.5f;
        rFr.cooldownIsRealTime = true;
        rFr.elementLockoutSeconds = 10.0f;
        rFr.elementLockoutIsRealTime = true;
        rFr.clearAllOnTrigger = true;
        rFr.hudTint = 0x4FB2FF;
        rFr.cb = &OnReactionFrost;
        rFr.user = nullptr;
        rFr.iconName = "ERF_ICON__erf_core__frost";
        api->RegisterReaction(rFr);

        // --- Shock ---
        ERF_ElementHandle elemsShock[] = {shock};
        ERF_ReactionDesc_Public rS{};
        rS.name = "Solo_Shock_85";
        rS.elements = elemsShock;
        rS.elementCount = 1;
        rS.ordered = false;
        rS.minPctEach = 0.85f;
        rS.minSumSelected = 0.0f;
        rS.cooldownSeconds = 0.5f;
        rS.cooldownIsRealTime = true;
        rS.elementLockoutSeconds = 10.0f;
        rS.elementLockoutIsRealTime = true;
        rS.clearAllOnTrigger = true;
        rS.hudTint = 0xFFD02A;
        rS.cb = &OnReactionShock;
        rS.user = nullptr;
        rS.iconName = "ERF_ICON__erf_core__shock";
        api->RegisterReaction(rS);
    }
}

// Envolve o registro com a JANELA/BARREIRA
static void RegisterEverything_WithWindow() {
    auto* api = AcquireERF();
    if (!api) return;

    // (Opcional) — configure um timeout menor p/ laboratório
    // if (api->SetFreezeTimeoutMs) api->SetFreezeTimeoutMs(1500);

    bool usedBarrier = false;
    if (api->BeginBatchRegistration) {
        usedBarrier = api->BeginBatchRegistration();
    }

    if (!usedBarrier) {
        // Se a janela já fechou, registrar agora vai falhar (provider recusa pós-freeze).
        // Tente pelo menos logar o estado pra diagnosticar.
        api->IsRegistrationOpen ? (api->IsRegistrationOpen() ? 1 : 0) : -1;
        api->IsFrozen ? (api->IsFrozen() ? 1 : 0) : -1;
    }

    RegisterEverything_Core();

    if (usedBarrier && api->EndBatchRegistration) {
        api->EndBatchRegistration();

#if FORCE_FREEZE_AFTER_END
        if (api->FreezeNow) {
            spdlog::warn("[ERF-Test] FORCE_FREEZE_AFTER_END=1 → FreezeNow()");
            api->FreezeNow();
        }
#endif
    }
}

class SneakInputSink : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static SneakInputSink* GetSingleton() {
        static SneakInputSink s;
        return std::addressof(s);
    }

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*, RE::BSTEventSource<RE::InputEvent*>*) override {
        auto* api = g_erf;
        if (!api || g_stateWet == 0) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) {
            return RE::BSEventNotifyControl::kContinue;
        }

        static bool s_wasSneaking = pc->IsSneaking();
        const bool nowSneaking = pc->IsSneaking();

        if (nowSneaking != s_wasSneaking) {
            s_wasSneaking = nowSneaking;

            if (nowSneaking) {
                if (api->ActivateState) {
                    api->ActivateState(pc, g_stateWet);
                    spdlog::info("[ERF-Test] Sneak ON → Wet state ACTIVATED.");
                }
            } else {
                if (api->DeactivateState) {
                    api->DeactivateState(pc, g_stateWet);
                    spdlog::info("[ERF-Test] Sneak OFF → Wet state DEACTIVATED.");
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

// -------------------- Mensageria SKSE (apenas ciclo de vida) --------------------
static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg) {
    if (!msg) return;

    if (msg->type == SKSE::MessagingInterface::kPostLoad) {
        AcquireERF();  // tenta cedo
    }
    if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
        if (!g_erf) AcquireERF();
        RegisterEverything_WithWindow();
    }
    if (msg->type == SKSE::MessagingInterface::kPostLoadGame) {
        auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr) {
            inputMgr->AddEventSink(SneakInputSink::GetSingleton());
        }
    }
}

// -------------------- Boilerplate SKSE --------------------
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    spdlog::set_level(spdlog::level::info);

    if (auto* m = SKSE::GetMessagingInterface()) {
        m->RegisterListener(OnSKSEMessage);  // só eventos SKSE; nada de “canal” da ERF
    }
    return true;
}
