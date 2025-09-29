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

// -------------------- Guarda da API --------------------
static ERF_API_V1* g_erf = nullptr;

static ERF_API_V1* AcquireERF() {
    if (g_erf) return g_erf;

    g_erf = ERF_GetAPI();  // <<< NOVO: padrão TrueHUD (GetProcAddress)
    if (!g_erf) {
        spdlog::warn("[ERF-Test] ERF API v{} ainda não disponível", ERF_API_VERSION);
        return nullptr;
    }

    spdlog::info("[ERF-Test] ERF API v{} adquirida", g_erf->version);
    return g_erf;
}

// -------------------- Callback de reação (explosão 100%) --------------------
static void OnReaction(const ERF_ReactionContext& ctx, void* /*user*/) {
    if (ctx.target) {
        spdlog::info("[ERF-Test] Reaction fired on actor {:08X}", ctx.target->GetFormID());
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
static void RegisterEverything() {
    auto* api = AcquireERF();
    if (!api) return;

    // 1) Elementos (com keywords vanilla)
    constexpr std::uint32_t kMagicDamageFire = 0x0001CEAD;
    constexpr std::uint32_t kMagicDamageFrost = 0x0001CEAE;
    constexpr std::uint32_t kMagicDamageShock = 0x0001CEAF;

    ERF_ElementHandle fire{}, frost{}, shock{};

    {
        ERF_ElementDesc_Public d{"Fire", 0xF04A3A, kMagicDamageFire};
        fire = api->RegisterElement(d);
    }
    {
        ERF_ElementDesc_Public d{"Frost", 0x4FB2FF, kMagicDamageFrost};
        frost = api->RegisterElement(d);
    }
    {
        ERF_ElementDesc_Public d{"Shock", 0xFFD02A, kMagicDamageShock};
        shock = api->RegisterElement(d);
    }

    spdlog::info("[ERF-Test] Elements -> Fire={} Frost={} Shock={}", fire, frost, shock);

    // 2) Estados
    ERF_StateHandle wet{}, rubber{}, fur{};
    {
        ERF_StateDesc_Public s{"Wet", 0};
        wet = api->RegisterState(s);
    }
    {
        ERF_StateDesc_Public s{"Rubber", 0};
        rubber = api->RegisterState(s);
    }
    {
        ERF_StateDesc_Public s{"Fur", 0};
        fur = api->RegisterState(s);
    }

    spdlog::info("[ERF-Test] States -> Wet={} Rubber={} Fur={}", wet, rubber, fur);

    // 3) Multiplicadores estado×elemento
    api->SetElementStateMultiplier(fire, wet, 0.10);
    api->SetElementStateMultiplier(fire, fur, 1.30);
    api->SetElementStateMultiplier(fire, rubber, 1.30);

    api->SetElementStateMultiplier(frost, fur, 0.10);
    api->SetElementStateMultiplier(frost, wet, 1.30);
    api->SetElementStateMultiplier(frost, rubber, 1.30);

    api->SetElementStateMultiplier(shock, rubber, 0.10);
    api->SetElementStateMultiplier(shock, wet, 1.30);
    api->SetElementStateMultiplier(shock, fur, 1.30);

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
        p.durationSeconds = 0.0f;  // contínuo
        p.durationIsRealTime = true;
        p.cooldownSeconds = 0.0f;
        p.cooldownIsRealTime = true;
        p.cb = &ApplyShockSlow;
        p.user = nullptr;

        auto ph = api->RegisterPreEffect(p);
        spdlog::info("[ERF-Test] PreEffect '{}' -> {}", p.name, ph);
    }

    // 5) Reação simples: Solo Fire 85% (callback de log)
    {
        ERF_ElementHandle elems[] = {fire};

        ERF_ReactionDesc_Public r{};
        r.name = "Solo_Fire_85";
        r.elements = elems;
        r.elementCount = 1;
        r.ordered = false;
        r.minTotalGauge = 100;
        r.minPctEach = 0.85f;
        r.minSumSelected = 0.0f;

        r.cooldownSeconds = 0.5f;
        r.cooldownIsRealTime = true;
        r.elementLockoutSeconds = 10.0f;
        r.elementLockoutIsRealTime = true;
        r.clearAllOnTrigger = true;

        r.hudIconPath = "img://textures/erf/icons/icon_fire.dds";
        r.hudIconTint = 0xF04A3A;

        r.cb = &OnReaction;
        r.user = nullptr;

        auto rh = api->RegisterReaction(r);
        spdlog::info("[ERF-Test] Reaction '{}' -> {}", r.name, rh);
    }

    spdlog::info("[ERF-Test] Registro concluído.");
}

// -------------------- Mensageria SKSE (apenas ciclo de vida) --------------------
static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg) {
    if (!msg) return;

    if (msg->type == SKSE::MessagingInterface::kPostLoad) {
        // A DLL provider já está carregada — tentar pegar a API aqui
        AcquireERF();
    }
    if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
        // Se ainda não pegou, tenta de novo e registra
        if (!g_erf) AcquireERF();
        RegisterEverything();
    }
}

// -------------------- Boilerplate SKSE --------------------
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("[ERF-Test] SKSEPlugin_Load");

    if (auto* m = SKSE::GetMessagingInterface()) {
        m->RegisterListener(OnSKSEMessage);  // só eventos SKSE; nada de “canal” da ERF
    }
    return true;
}
