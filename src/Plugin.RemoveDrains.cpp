#include "PCH.h"
#include "remove_drains/RemoveDrains.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
            if (msg && msg->type == SKSE::MessagingInterface::kDataLoaded) {
                RemoveDrains::RemoveDrainFromShockAndFrost();
            }
        });
    }
    return true;
}
