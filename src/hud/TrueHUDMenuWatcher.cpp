#include "TrueHUDMenuWatcher.h"

#include "InjectHUD.h"

using TrueHUDWatcher::TrueHUDMenuWatcher;

RE::BSEventNotifyControl TrueHUDMenuWatcher::ProcessEvent(const RE::MenuOpenCloseEvent* ev,
                                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (!ev || !ev->menuName.empty()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Nome do menu no TrueHUD
    if (ev->menuName == "TrueHUD") {
        if (!ev->opening) {
            spdlog::info("[ERF] TrueHUD CLOSE");
            InjectHUD::OnTrueHUDClose();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}
