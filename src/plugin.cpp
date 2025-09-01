#include "PCH.h"

using namespace SKSE;
using namespace RE;

namespace {
    void SetFloatGMST(const char* name, float value) {
        auto* gs = GameSettingCollection::GetSingleton();
        if (!gs) {
            spdlog::info("GameSettingCollection::GetSingleton() retornou null");
            return;
        }
        Setting* s = gs->GetSetting(name);
        if (!s) {
            spdlog::info("GMST '{}' não encontrado", name);
            return;
        }
        if (s->GetType() == Setting::Type::kFloat) {
            s->data.f = value;
            spdlog::info("GMST '{}' := {}", name, value);
        } else {
            spdlog::warn("GMST '{}' não é float", name);
        }
    }

    void InitializeLogger() {
        if (auto path = log::log_directory()) {
            *path /= "SMSODestruction.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("global log", std::move(sink));
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::info("Plugin carregado.");
        }
    }

    void OnSKSEMessage(MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }
        if (msg->type == MessagingInterface::kDataLoaded) {
            spdlog::info("Dados (Data) carregados.");
        }
    }
}

SKSEPluginLoad(const LoadInterface* skse) {
    Init(skse);
    InitializeLogger();

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(OnSKSEMessage);
    } else {
        spdlog::warn("MessagingInterface indisponível.");
    }

    // Zera os multiplicadores de dreno
    SetFloatGMST("fMagicDamageShockMagickaMult", 0.0f);
    SetFloatGMST("fMagicDamageFrostStaminaMult", 0.0f);

    return true;
}
