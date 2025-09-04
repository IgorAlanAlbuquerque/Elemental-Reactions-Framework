#include "ElementalGauges.h"
#include "ElementalStates.h"
#include "MagicGaugesListener.h"
#include "PCH.h"
#include "RemoveDrains.h"
#include "common/Helpers.h"
#include "common/PluginSerialization.h"

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

        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                spdlog::info("Removendo drains.");
                RemoveDrains::RemoveDrainFromShockAndFrost();
                MagicGauges::Install();
                MagicGauges::ResetDebounce();
                spdlog::info("listener de magias instalado.");
                break;
            case SKSE::MessagingInterface::kNewGame:
                [[fallthrough]];
            case SKSE::MessagingInterface::kPostLoadGame:
                MagicGauges::ResetDebounce();
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
