// HUDTick.cpp
#include "HUDTick.h"

#include <chrono>
#include <thread>

#include "../ElementalGauges.h"
#include "InjectHUD.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    std::atomic_bool g_run{false};

    void UpdateAllOnUIThread() {
        RE::UI* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::BSFixedString("TrueHUD"))) return;

        RE::GPtr<RE::IMenu> menu = ui->GetMenu(RE::BSFixedString("TrueHUD"));
        RE::GFxMovieView* view = menu ? menu->uiMovie.get() : nullptr;
        if (!view) return;

        std::vector<RE::FormID> alive;
        alive.reserve(64);

        ElementalGauges::ForEachDecayed([&](RE::FormID id, const ElementalGauges::Totals& t) {
            RE::Actor* a = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!a || a->IsDead()) {
                HUD::RemoveGaugeFor(view, a);
                return;
            }

            int iconId = 0;
            std::uint32_t tint = 0xFFFFFF;
            if (auto sel = ElementalGauges::PickHudIcon(t)) {
                iconId = sel->id;
                tint = sel->tintRGB;
            }

            HUD::EnsureGaugeFor(view, a);
            HUD::UpdateGaugeFor(view, a, t.fire, t.frost, t.shock, iconId, tint);

            alive.push_back(id);
        });
    }
}

namespace HUD {
    void StartHUDTick() {
        if (g_run.exchange(true)) return;

        std::thread([] {
            using namespace std::chrono;
            auto next = steady_clock::now();
            while (g_run.load(std::memory_order_relaxed)) {
                next += milliseconds(200);

                if (auto* ti = SKSE::GetTaskInterface()) {
                    ti->AddTask([]() { UpdateAllOnUIThread(); });
                }

                std::this_thread::sleep_until(next);
            }
        }).detach();
    }

    void StopHUDTick() { g_run = false; }
}
