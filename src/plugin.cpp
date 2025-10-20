#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

#include "ElementalReactionsAPI.h"
#include "ModAPI.h"
#include "PCH.h"
#include "TrueHUDAPI.h"
#include "common/Helpers.h"
#include "common/PluginSerialization.h"
#include "elemental_reactions/ElementalGauges.h"
#include "elemental_reactions/ElementalGaugesHook.h"
#include "elemental_reactions/ElementalStates.h"
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

        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded: {
                ERF::API::OpenRegistrationWindowAndScheduleFreeze();
                ElementalGaugesHook::InitCarrierRefs();
                ElementalGaugesHook::Install();
                ElementalGaugesHook::RegisterAEEventSink();
                spdlog::info("Hook para gauges instalado.");

                const auto trueHUD = static_cast<TRUEHUD_API::IVTrueHUD4*>(
                    TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V4));
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
                        spdlog::info("[ERF] Resultado do LoadCustomWidgets: {}",
                                     static_cast<int>(std::to_underlying(result)));

                        if (result == TRUEHUD_API::APIResult::OK) {
                            InjectHUD::g_trueHUD->RegisterNewWidgetType(InjectHUD::g_pluginHandle,
                                                                        InjectHUD::ERF_WIDGET_TYPE);
                            spdlog::info("Widget registrado");

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

extern "C" DLLEXPORT void* SKSEAPI RequestPluginAPI(std::uint32_t requestedVersion) {
    if (requestedVersion == ERF_API_VERSION) {
        return static_cast<void*>(ERF::API::Get());
    }
    return nullptr;
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
        spdlog::info("Messaging listener (ciclo de vida) registrado.");
    }

    return true;
}