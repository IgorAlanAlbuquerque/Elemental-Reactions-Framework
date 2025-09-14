#include <atomic>
#include <chrono>
#include <thread>

#include "PCH.h"
#include "TrueHUDAPI.h"
#include "common/Helpers.h"
#include "common/PluginSerialization.h"
#include "elemental_reactions/ElementalEffects.h"
#include "elemental_reactions/ElementalGauges.h"
#include "elemental_reactions/ElementalGaugesHook.h"
#include "elemental_reactions/ElementalStates.h"
#include "hud/HUDTick.h"
#include "hud/InjectHUD.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

using namespace SKSE;
using namespace RE;

namespace {
    void InitializeLogger() {
        if (auto path = log::log_directory()) {
            *path /= "SMSODestruction.log";
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
            case SKSE::MessagingInterface::kDataLoaded:
                ElementalGaugesHook::Install();
                ElementalGaugesHook::RegisterAEEventSink();
                spdlog::info("Hook para gauges instalado.");
                ElementalEffects::ConfigurarGatilhos();
                spdlog::info("Efeitos elementais registrados.");

                // Inicializa TrueHUD
                if (!trueHUD) {
                    spdlog::warn("[SMSO] TrueHUD não detectado. Widget não será carregado.");
                    return;
                }
                spdlog::info("[SMSO] TrueHUD detectado.");
                InjectHUD::g_trueHUD = trueHUD;
                InjectHUD::g_pluginHandle = SKSE::GetPluginHandle();

                // Registra o tipo de widget customizado
                InjectHUD::g_trueHUD->LoadCustomWidgets(
                    InjectHUD::g_pluginHandle, InjectHUD::SMSO_SWF_PATH, [](TRUEHUD_API::APIResult result) {
                        spdlog::info("[SMSO] Resultado do LoadCustomWidgets: {}", static_cast<int>(result));

                        if (result == TRUEHUD_API::APIResult::OK) {
                            InjectHUD::g_trueHUD->RegisterNewWidgetType(InjectHUD::g_pluginHandle,
                                                                        InjectHUD::SMSO_WIDGET_TYPE);
                            spdlog::info("Widget registrado");

                            HUD::StartHUDTick();
                            spdlog::info("Loop de HUD iniciado.");
                        } else {
                            spdlog::error("[SMSO] Falha ao carregar o SWF dos widgets!");
                        }
                    });
                break;
            case SKSE::MessagingInterface::kNewGame:
                [[fallthrough]];
            case SKSE::MessagingInterface::kPostLoadGame:
                break;
            default:
                break;
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();
    spdlog::info("SMSODestruction carregado.");

    Ser::Install(FOURCC('S', 'M', 'S', 'O'));
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
