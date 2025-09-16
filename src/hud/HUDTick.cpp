#include "HUDTick.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "../elemental_reactions/ElementalGauges.h"
#include "InjectHUD.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    std::atomic_bool g_run{false};
    std::unordered_map<RE::FormID, float> lastSeen;

    std::unordered_map<RE::FormID, float> lastAddedAt;
    constexpr float INIT_GRACE_SECONDS = 0.05f;

    void UpdateAllOnUIThread() {
        const float now = RE::Calendar::GetSingleton()->GetCurrentGameTime() * 3600.0f;

        constexpr float ZERO_GRACE_SECONDS = 0.1f;

        std::unordered_set<RE::FormID> alive;
        alive.reserve(64);

        ElementalGauges::ForEachDecayed([&](RE::FormID id, const ElementalGauges::Totals& t) {
            alive.insert(id);

            RE::Actor* a = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!a || a->IsDead()) {
                InjectHUD::RemoveFor(id);
                lastAddedAt.erase(id);
                lastSeen.erase(id);
                return;
            }

            InjectHUD::AddFor(a);

            auto [itAdd, inserted] = lastAddedAt.try_emplace(id, now);
            const bool inGrace = inserted || ((now - itAdd->second) < INIT_GRACE_SECONDS);

            if (!inGrace) {
                InjectHUD::UpdateFor(a);
            }

            lastSeen[id] = now;
        });

        for (auto it = InjectHUD::widgets.begin(); it != InjectHUD::widgets.end();) {
            const RE::FormID id = it->first;

            // se não apareceu na passada de cima, está “inativo”
            const bool isAlive = (alive.find(id) != alive.end());

            if (!isAlive) {
                const float seen = lastSeen.contains(id) ? lastSeen[id] : 0.0f;
                // pequena graça p/ evitar piscadas entre frames
                if ((now - seen) >= ZERO_GRACE_SECONDS) {
                    ++it;  // avance ANTES de remover (RemoveFor já dá erase no map)
                    InjectHUD::RemoveFor(id);
                    lastAddedAt.erase(id);
                    lastSeen.erase(id);
                    continue;
                }
            }

            ++it;
        }

        for (auto it = lastSeen.begin(); it != lastSeen.end();) {
            const RE::FormID id = it->first;

            if (alive.find(id) == alive.end() && InjectHUD::widgets.find(id) == InjectHUD::widgets.end() &&
                (now - it->second) >= ZERO_GRACE_SECONDS) {
                lastAddedAt.erase(id);
                it = lastSeen.erase(it);
            } else {
                ++it;
            }
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
                next += milliseconds(16);

                if (auto* ti = SKSE::GetTaskInterface()) {
                    ti->AddTask([]() { UpdateAllOnUIThread(); });
                }

                std::this_thread::sleep_until(next);
            }
        }).detach();
    }

    void StopHUDTick() { g_run = false; }

    void ResetTracking() {
        lastSeen.clear();
        lastAddedAt.clear();
    }
}
