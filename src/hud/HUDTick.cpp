#include "HUDTick.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../elemental_reactions/ElementalGauges.h"
#include "InjectHUD.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    static std::atomic_bool g_run{false};
    static std::condition_variable g_cv;
    static std::mutex g_cv_m;

    static std::atomic<int> g_fastMs{16};
    static std::atomic<int> g_slowMs{50};
    static std::atomic<int> g_periodMs{16};
    static std::atomic<int> g_idleFrames{0};
    static constexpr int kIdleThreshold = 30;

    static std::atomic_bool g_wake{false};

    static std::atomic_bool g_lastHadWork{false};
    std::unordered_map<RE::FormID, float> lastSeen;

    std::unordered_map<RE::FormID, float> lastAddedAt;
    constexpr float INIT_GRACE_SECONDS = 0.05f;
    static constexpr float EVICT_SECONDS = 10.0f;

    void UpdateAllOnUIThread() {
        const double nowRt = InjectHUD::NowRtS();
        const float nowH = RE::Calendar::GetSingleton()->GetHoursPassed();
        InjectHUD::OnUIFrameBegin(nowRt, nowH);

        if (auto const* pc = RE::PlayerCharacter::GetSingleton(); pc && pc->IsDead()) {
            InjectHUD::RemoveAllWidgets();
            HUD::ResetTracking();
            return;
        }

        const float now = RE::Calendar::GetSingleton()->GetCurrentGameTime() * 3600.0f;

        constexpr float ZERO_GRACE_SECONDS = 0.1f;

        std::unordered_set<RE::FormID> alive;
        alive.reserve(64);

        std::vector<RE::FormID> toRemove;
        toRemove.reserve(16);

        ElementalGauges::ForEachDecayed([&](RE::FormID id, ElementalGauges::TotalsView) {
            alive.insert(id);

            RE::Actor* a = nullptr;
            if (auto itW = InjectHUD::widgets.find(id); itW != InjectHUD::widgets.end()) {
                a = (itW->second.handle) ? itW->second.handle.get().get() : nullptr;
            }
            if (!a) {
                a = RE::TESForm::LookupByID<RE::Actor>(id);
                if (a) {
                    auto [__itW, __insertedW] = InjectHUD::widgets.try_emplace(id);
                    auto& entry = __itW->second;
                    entry.handle = a->CreateRefHandle();
                }
            }

            if (!a) {
                toRemove.push_back(id);
                lastAddedAt.erase(id);
                lastSeen.erase(id);
                return;
            }

            if (a->IsDead()) {
                toRemove.push_back(id);
                lastAddedAt.erase(id);
                lastSeen.erase(id);
                return;
            }

            InjectHUD::AddFor(a);

            auto [itAdd, inserted] = lastAddedAt.try_emplace(id, now);
            const bool inGrace = inserted || ((now - itAdd->second) < INIT_GRACE_SECONDS);

            if (!InjectHUD::IsOnScreen(a)) {
                InjectHUD::HideFor(id);
            } else if (!inGrace) {
                InjectHUD::UpdateFor(a, nowRt, nowH);
            }

            lastSeen[id] = now;
        });

        for (auto it = InjectHUD::widgets.begin(); it != InjectHUD::widgets.end(); ++it) {
            const RE::FormID id = it->first;
            const bool isAlive = (alive.find(id) != alive.end());

            if (!isAlive) {
                const float seen = lastSeen.contains(id) ? lastSeen[id] : 0.0f;
                const float age = now - seen;

                if (age >= ZERO_GRACE_SECONDS) {
                    InjectHUD::HideFor(id);
                }

                if (age >= EVICT_SECONDS) {
                    toRemove.push_back(id);
                    lastAddedAt.erase(id);
                    lastSeen.erase(id);
                }
            }
        }

        if (!toRemove.empty()) {
            for (auto id : toRemove) {
                InjectHUD::RemoveFor(id);
            }
        }

        for (auto it = lastSeen.begin(); it != lastSeen.end();) {
            const RE::FormID id = it->first;
            const bool hasEntry = (InjectHUD::widgets.find(id) != InjectHUD::widgets.end());
            const bool aliveNow = (alive.find(id) != alive.end());
            const float age = now - it->second;

            if (!aliveNow && !hasEntry && age >= ZERO_GRACE_SECONDS) {
                lastAddedAt.erase(id);
                it = lastSeen.erase(it);
            } else {
                ++it;
            }
        }

        const bool hadWork = !alive.empty() || !InjectHUD::widgets.empty();
        g_lastHadWork.store(hadWork, std::memory_order_relaxed);
    }
}

void HUD::StartHUDTick() {
    if (g_run.exchange(true)) {
        g_wake.store(true, std::memory_order_relaxed);
        g_periodMs.store(g_fastMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
        g_cv.notify_one();
        return;
    }

    std::thread([] {
        using namespace std::chrono;

        for (;;) {
            int waitMs = g_periodMs.load(std::memory_order_relaxed);
            {
                std::unique_lock lk(g_cv_m);
                g_cv.wait_for(lk, milliseconds(waitMs), [] { return !g_run.load() || g_wake.load(); });
                g_wake.store(false, std::memory_order_relaxed);
            }
            if (!g_run.load(std::memory_order_relaxed)) break;

            if (auto* ti = SKSE::GetTaskInterface()) {
                ti->AddUITask([] { UpdateAllOnUIThread(); });
            }

            const bool hadWork = g_lastHadWork.load(std::memory_order_relaxed);
            if (hadWork) {
                g_idleFrames.store(0, std::memory_order_relaxed);
                g_periodMs.store(g_fastMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            } else {
                int idle = g_idleFrames.load(std::memory_order_relaxed) + 1;
                g_idleFrames.store(idle, std::memory_order_relaxed);
                if (idle >= kIdleThreshold) {
                    g_periodMs.store(g_slowMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
                }
            }
        }
    }).detach();
}

void HUD::StopHUDTick() {
    g_run.store(false, std::memory_order_relaxed);
    g_wake.store(true, std::memory_order_relaxed);
    g_cv.notify_one();
}

void HUD::ResetTracking() {
    lastSeen.clear();
    lastAddedAt.clear();
}
