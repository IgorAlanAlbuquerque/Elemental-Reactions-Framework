#include "TrueHUDMenuWatcher.h"

#include "InjectHUD.h"

using TrueHUDWatcher::TrueHUDMenuWatcher;

RE::BSEventNotifyControl TrueHUDMenuWatcher::ProcessEvent(const RE::MenuOpenCloseEvent* ev,
                                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (!ev || ev->menuName.empty()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (ev->menuName == "TrueHUD" && !ev->opening) {
        InjectHUD::OnTrueHUDClose();
    }

    return RE::BSEventNotifyControl::kContinue;
}