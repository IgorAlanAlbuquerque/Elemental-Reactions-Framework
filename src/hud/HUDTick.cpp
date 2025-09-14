#include "HUDTick.h"

#include <chrono>
#include <thread>
#include <unordered_map>

#include "../elemental_reactions/ElementalGauges.h"
#include "InjectHUD.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    std::atomic_bool g_run{false};
    std::unordered_map<RE::FormID, float> lastSeen;
    constexpr float TIMEOUT_SECONDS = 5.0f;

    std::unordered_map<RE::FormID, float> lastAddedAt;
    constexpr float INIT_GRACE_SECONDS = 0.30f;  // ~300 ms

    void UpdateAllOnUIThread() {
        spdlog::info("[SMSO] Entrou no update");
        float now = RE::Calendar::GetSingleton()->GetCurrentGameTime() * 3600.0f;

        std::vector<RE::FormID> alive;
        alive.reserve(64);

        ElementalGauges::ForEachDecayed([&](RE::FormID id, const ElementalGauges::Totals& t) {
            spdlog::info("[SMSO] HUDTick decayed: id={:08X} F={} R={} S={}", id, t.fire, t.frost, t.shock);
            RE::Actor* a = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!a || a->IsDead()) {
                InjectHUD::RemoveFor(id);
                lastAddedAt.erase(id);
                return;
            }

            InjectHUD::AddFor(a);

            bool justAdded = false;
            if (!lastAddedAt.contains(id)) {
                lastAddedAt[id] = now;
                justAdded = true;
            } else if ((now - lastAddedAt[id]) < INIT_GRACE_SECONDS) {
                justAdded = true;
            }

            if (!justAdded) {
                InjectHUD::UpdateFor(a);
            } else {
                spdlog::info("[SMSO] Aguardando init do widget {:08X} antes de atualizar...", id);
            }

            alive.push_back(id);
            lastSeen[id] = now;
        });

        for (auto it = lastSeen.begin(); it != lastSeen.end();) {
            if (std::find(alive.begin(), alive.end(), it->first) == alive.end() &&
                (now - it->second) >= TIMEOUT_SECONDS) {
                InjectHUD::RemoveFor(it->first);
                lastAddedAt.erase(it->first);
                it = lastSeen.erase(it);
                continue;
            }
            ++it;
        }
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
