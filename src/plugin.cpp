#include "DrainEffectHook.h"
#include "Logger.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::AllocTrampoline(128);
    Logger::InitializeLogging();
    spdlog::info("log inicializado");
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::Install();  // instala o hook aqui
        }
    });

    return true;
}
