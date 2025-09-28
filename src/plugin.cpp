#include <atomic>
#include <chrono>
#include <thread>

#include "PCH.h"
#include "TrueHUDAPI.h"
#include "common/Helpers.h"
#include "common/PluginSerialization.h"
#include "elemental_reactions/ElementalGauges.h"
#include "elemental_reactions/ElementalGaugesHook.h"
#include "elemental_reactions/ElementalStates.h"
#include "elemental_reactions/erf_element.h"
#include "elemental_reactions/erf_reaction.h"
#include "hud/InjectHUD.h"
#include "hud/TrueHUDMenuWatcher.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

using namespace SKSE;
using namespace RE;
using RR = ReactionRegistry;

namespace {
    static std::optional<ERF_ElementHandle> E(std::string_view name) { return ElementRegistry::get().findByName(name); }

    static std::uint32_t Col(ERF_ElementHandle h, std::uint32_t defRGB) {
        if (const auto* d = ElementRegistry::get().get(h)) return d->colorRGB;
        return defRGB;
    }

    static constexpr const char* kIconsDDSDir = "img://textures/erf/icons/";

    // Aceita string_view para evitar const char* nulo e facilitar chamada
    static inline std::string DDS(std::string_view fileNoExt) {
        std::string s{kIconsDDSDir};
        s.append(fileNoExt.data(), fileNoExt.size());
        s += ".dds";
        return s;
    }

    static ERF_ReactionHudIcon HudSolo(ERF_ElementHandle e) {
        ERF_ReactionHudIcon h{};
        h.iconPath = DDS("icon_fire");  // será sobrescrito em Reg_Solo conforme o arquivo passado
        if (const auto* d = ElementRegistry::get().get(e))
            h.iconTint = d->colorRGB;
        else
            h.iconTint = 0xFFFFFF;
        return h;
    }

    static ERF_ReactionHudIcon HudPair(ERF_ElementHandle a, ERF_ElementHandle b, std::string_view filenameNoExt,
                                       std::uint32_t tint) {
        ERF_ReactionHudIcon h{};
        h.iconPath = DDS(filenameNoExt);  // ex.: "icon_fire_frost"
        h.iconTint = tint;
        return h;
    }

    static ERF_ReactionHudIcon HudTriple(ERF_ElementHandle f, ERF_ElementHandle i, ERF_ElementHandle s) {
        ERF_ReactionHudIcon h{};
        h.iconPath = DDS("icon_fire_frost_shock");
        h.iconTint = 0xFFF0CC;
        return h;
    }

    static void FillPolicy(ERF_ReactionDesc& d) {
        d.minTotalGauge = 100;
        d.clearAllOnTrigger = true;
        d.elementLockoutSeconds = 10.0f;
        d.elementLockoutIsRealTime = true;
        d.cooldownSeconds = 0.5f;
        d.cooldownIsRealTime = true;
    }

    static void FxSoloFire(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] SOLO Fire");
    }
    static void FxSoloFrost(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] SOLO Frost");
    }
    static void FxSoloShock(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] SOLO Shock");
    }

    static void FxPairFireFrost(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair FireFrost");
    }
    static void FxPairFrostFire(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair FrostFire");
    }
    static void FxPairFireShock(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair FireShock");
    }
    static void FxPairShockFire(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair ShockFire");
    }
    static void FxPairFrostShock(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair FrostShock");
    }
    static void FxPairShockFrost(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] Pair ShockFrost");
    }

    static void FxTriple(RE::Actor* a, void*) {
        if (a) spdlog::info("[ERF] TRIPLE Fire+Frost+Shock");
    }

    // -------- Registro (corrige d.name e usa iconPath) --------

    static ERF_ReactionHandle Reg_Solo(std::string_view name, ERF_ElementHandle e, std::string_view fileNoExt,
                                       ERF_ReactionDesc::Callback cb) {
        ERF_ReactionDesc d{};
        d.name = std::string{name};  // evita dangling de string_view
        d.elements = {e};
        d.ordered = false;
        d.minPctEach = 0.85f;
        d.minSumSelected = 0.0f;
        FillPolicy(d);
        d.cb = cb;

        d.hud = HudSolo(e);
        d.hud.iconPath = DDS(fileNoExt);  // só path e tint
        return ReactionRegistry::get().registerReaction(d);
    }

    static ERF_ReactionHandle Reg_Pair(std::string_view name, ERF_ElementHandle first, ERF_ElementHandle second,
                                       std::string_view fileNoExt, std::uint32_t tint, ERF_ReactionDesc::Callback cb) {
        ERF_ReactionDesc d{};
        d.name = std::string{name};
        d.elements = {first, second};
        d.ordered = true;
        d.minPctEach = 0.35f;
        d.minSumSelected = 0.80f;
        FillPolicy(d);
        d.cb = cb;

        d.hud = HudPair(first, second, fileNoExt, tint);  // já monta iconPath e tint
        return ReactionRegistry::get().registerReaction(d);
    }

    static ERF_ReactionHandle Reg_Triple(std::string_view name, ERF_ElementHandle f, ERF_ElementHandle i,
                                         ERF_ElementHandle s, ERF_ReactionDesc::Callback cb) {
        ERF_ReactionDesc d{};
        d.name = std::string{name};
        d.elements = {f, i, s};
        d.ordered = false;
        d.minPctEach = 0.28f;
        d.minSumSelected = 0.0f;
        FillPolicy(d);
        d.cb = cb;

        d.hud = HudTriple(f, i, s);
        return ReactionRegistry::get().registerReaction(d);
    }

    void RegisterAllReactions_ERF() {
        auto f = ElementRegistry::get().findByName("Fire");
        auto i = ElementRegistry::get().findByName("Frost");
        auto s = ElementRegistry::get().findByName("Shock");
        if (!f || !i || !s) {
            spdlog::error("[ERF] RegisterAllReactions: elementos vanilla ausentes");
            return;
        }

        Reg_Solo("Solo_Fire_85", *f, "icon_fire", &FxSoloFire);
        Reg_Solo("Solo_Frost_85", *i, "icon_frost", &FxSoloFrost);
        Reg_Solo("Solo_Shock_85", *s, "icon_shock", &FxSoloShock);

        Reg_Pair("Pair_FireFrost", *f, *i, "icon_fire_frost", 0xE65ACF, &FxPairFireFrost);
        Reg_Pair("Pair_FrostFire", *i, *f, "icon_fire_frost", 0x7A73FF, &FxPairFrostFire);
        Reg_Pair("Pair_FireShock", *f, *s, "icon_fire_shock", 0xFF8A2A, &FxPairFireShock);
        Reg_Pair("Pair_ShockFire", *s, *f, "icon_fire_shock", 0xF6B22E, &FxPairShockFire);
        Reg_Pair("Pair_FrostShock", *i, *s, "icon_frost_shock", 0x49C9F0, &FxPairFrostShock);
        Reg_Pair("Pair_ShockFrost", *s, *i, "icon_frost_shock", 0xB8E34D, &FxPairShockFrost);

        Reg_Triple("Triple_FireFrostShock_28each", *f, *i, *s, &FxTriple);
        spdlog::info("[ERF] Reactions registered: {}", ReactionRegistry::get().size());
    }

    static RE::BGSKeyword* byID(uint32_t formID) { return RE::TESForm::LookupByID<RE::BGSKeyword>(formID); }

    static void RegisterVanillaElements() {
        auto& R = ElementRegistry::get();
        if (R.findByName("Fire") || R.findByName("Frost") || R.findByName("Shock")) return;

        constexpr uint32_t kMagicDamageFire = 0x0001CEAD;
        constexpr uint32_t kMagicDamageFrost = 0x0001CEAE;
        constexpr uint32_t kMagicDamageShock = 0x0001CEAF;

        {
            ERF_ElementDesc d{};
            d.name = "Fire";
            d.colorRGB = 0xF04A3A;
            d.keyword = byID(kMagicDamageFire);
            d.stateMultipliers = {{ElementalStates::Flag::Wet, 0.10},
                                  {ElementalStates::Flag::Fur, 1.30},
                                  {ElementalStates::Flag::Rubber, 1.30}};
            R.registerElement(d);
        }
        {
            ERF_ElementDesc d{};
            d.name = "Frost";
            d.colorRGB = 0x4FB2FF;
            d.keyword = byID(kMagicDamageFrost);
            d.stateMultipliers = {{ElementalStates::Flag::Fur, 0.10},
                                  {ElementalStates::Flag::Wet, 1.30},
                                  {ElementalStates::Flag::Rubber, 1.30}};
            R.registerElement(d);
        }
        {
            ERF_ElementDesc d{};
            d.name = "Shock";
            d.colorRGB = 0xFFD02A;
            d.keyword = byID(kMagicDamageShock);
            d.stateMultipliers = {{ElementalStates::Flag::Rubber, 0.10},
                                  {ElementalStates::Flag::Wet, 1.30},
                                  {ElementalStates::Flag::Fur, 1.30}};
            R.registerElement(d);
        }
    }
}

namespace {
    void InitializeLogger() {
        if (auto path = log::log_directory()) {
            *path /= "ERFDestruction.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("global", sink);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("Logger iniciado.");
        }
    }

    void GlobalMessageHandler(SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;

        const auto trueHUD =
            static_cast<TRUEHUD_API::IVTrueHUD4*>(TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V4));
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded: {
                RegisterVanillaElements();
                spdlog::info("[ERF] Elementos vanilla registrados (count={}).", ElementRegistry::get().size());
                RegisterAllReactions_ERF();
                spdlog::info("Efeitos elementais registrados.");
                ElementalGaugesHook::InitCarrierRefs();
                ElementalGaugesHook::Install();
                ElementalGaugesHook::RegisterAEEventSink();
                spdlog::info("Hook para gauges instalado.");

                if (!trueHUD) {
                    spdlog::warn("[ERF] TrueHUD não detectado. Widget não será carregado.");
                    return;
                }
                spdlog::info("[ERF] TrueHUD detectado.");
                InjectHUD::g_trueHUD = trueHUD;
                InjectHUD::g_pluginHandle = SKSE::GetPluginHandle();

                if (auto* ui = RE::UI::GetSingleton()) {
                    ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(
                        TrueHUDWatcher::TrueHUDMenuWatcher::GetSingleton());
                    spdlog::info("[ERF] TrueHUD menu watcher registrado.");
                }

                InjectHUD::g_trueHUD->LoadCustomWidgets(
                    InjectHUD::g_pluginHandle, InjectHUD::ERF_SWF_PATH, [](TRUEHUD_API::APIResult result) {
                        spdlog::info("[ERF] Resultado do LoadCustomWidgets: {}", static_cast<int>(result));

                        if (result == TRUEHUD_API::APIResult::OK) {
                            InjectHUD::g_trueHUD->RegisterNewWidgetType(InjectHUD::g_pluginHandle,
                                                                        InjectHUD::ERF_WIDGET_TYPE);
                            spdlog::info("Widget registrado");

                            // Abre o "gate": HUDTick será iniciado pelos hooks (Start/Update) via ALLOW_HUD_TICK
                            ElementalGaugesHook::ALLOW_HUD_TICK.store(true, std::memory_order_release);
                            spdlog::info("ALLOW_HUD_TICK = true (HUDTick iniciará nos hooks).");
                        } else {
                            spdlog::error("[ERF] Falha ao carregar o SWF dos widgets!");
                        }
                    });
                break;
            }

            case SKSE::MessagingInterface::kNewGame:
                [[fallthrough]];
            case SKSE::MessagingInterface::kPostLoadGame: {
                InjectHUD::RemoveAllWidgets();
                HUD::ResetTracking();
                spdlog::info("[ERF] Reset após NewGame/PostLoad.");
                break;
            }

            default:
                break;
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();
    spdlog::info("ERFDestruction carregado.");

    Ser::Install(FOURCC('E', 'L', 'R', 'E'));
    spdlog::info("Serializador registrado.");

    ElementalStates::RegisterStore();
    spdlog::info("Store de estados elementais registrado.");
    ElementalGauges::RegisterStore();
    spdlog::info("Store de medidores elementais registrado.");

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(GlobalMessageHandler);
        spdlog::info("Messaging listener registrado.");
    }

    return true;
}
