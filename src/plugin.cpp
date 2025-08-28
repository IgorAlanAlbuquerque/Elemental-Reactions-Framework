#include "hooks.h"
#include "logger.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    Logger::InitializeLogging();
    Hooks::Install();
    return true;
}
