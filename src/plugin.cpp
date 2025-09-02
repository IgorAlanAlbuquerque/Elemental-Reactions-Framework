#include "PCH.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

using namespace SKSE;
using namespace RE;

namespace {
    void RemoveDrainFromShockAndFrost() {
        std::vector<std::pair<RE::FormID, RE::ActorValue>> effectIDs = {
            {0x00013CAB, RE::ActorValue::kMagicka}, {0x000E4CB6, RE::ActorValue::kMagicka},
            {0x0010CBDF, RE::ActorValue::kMagicka}, {0x0001CEA8, RE::ActorValue::kMagicka},
            {0x0010F7EF, RE::ActorValue::kMagicka}, {0x0005DBAE, RE::ActorValue::kMagicka},
            {0x0004EFDE, RE::ActorValue::kMagicka}, {0x000D22FA, RE::ActorValue::kMagicka},
            {0x000EA65A, RE::ActorValue::kStamina}, {0x00013CAA, RE::ActorValue::kStamina},
            {0x0010CBDE, RE::ActorValue::kStamina}, {0x0001CEA2, RE::ActorValue::kStamina},
            {0x0010F7F0, RE::ActorValue::kStamina}, {0x0001CEA3, RE::ActorValue::kStamina},
            {0x000EA076, RE::ActorValue::kStamina}, {0x0007E8E2, RE::ActorValue::kStamina},
            {0x00066335, RE::ActorValue::kStamina}, {0x00066334, RE::ActorValue::kStamina}};

        for (auto& [formID, expectedAV] : effectIDs) {
            auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(formID);
            if (effect) {
                if (effect->data.secondaryAV == expectedAV) {
                    spdlog::info("Removendo {} de '{}'", expectedAV == RE::ActorValue::kMagicka ? "Magicka" : "Stamina",
                                 effect->GetFormEditorID());
                    effect->data.secondaryAV = RE::ActorValue::kNone;
                } else {
                    spdlog::info("Efeito '{}' já não usa {} como secondaryAV", effect->GetFormEditorID(),
                                 expectedAV == RE::ActorValue::kMagicka ? "Magicka" : "Stamina");
                }
            } else {
                spdlog::warn("Não foi possível encontrar EffectSetting com FormID {:08X}", formID);
            }
        }
    }

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

    void OnSKSEMessage(SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            spdlog::info("kDataLoaded recebido. Iniciando remoção de efeitos.");
            RemoveDrainFromShockAndFrost();
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();
    spdlog::info("SMSODestruction carregado.");

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(OnSKSEMessage);
        spdlog::info("Messaging listener registrado.");
    }

    return true;
}
