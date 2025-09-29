#pragma once
#include "RE/Skyrim.h"

using namespace std::literals;

namespace TrueHUDWatcher {
    class TrueHUDMenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static TrueHUDMenuWatcher* GetSingleton() {
            static TrueHUDMenuWatcher inst;
            return std::addressof(inst);
        }

        virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* ev,
                                                      RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
    };
}