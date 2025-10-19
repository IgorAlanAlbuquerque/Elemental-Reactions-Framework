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
    // estado do loop
    static std::atomic_bool g_run{false};
    static std::condition_variable g_cv;
    static std::mutex g_cv_m;

    // período-alvo (ms)
    static std::atomic<int> g_fastMs{16};      // 60 FPS
    static std::atomic<int> g_slowMs{50};      // idle backoff
    static std::atomic<int> g_periodMs{16};    // começa rápido
    static std::atomic<int> g_idleFrames{0};   // contagem de frames sem trabalho
    static constexpr int kIdleThreshold = 30;  // N frames ociosos antes de ir p/ slow

    // flag para acordar imediatamente (via StartHUDTick)
    static std::atomic_bool g_wake{false};

    // telemetria de trabalho do último frame de UI (setado em UpdateAllOnUIThread)
    static std::atomic_bool g_lastHadWork{false};
    std::unordered_map<RE::FormID, float> lastSeen;

    std::unordered_map<RE::FormID, float> lastAddedAt;
    constexpr float INIT_GRACE_SECONDS = 0.05f;

    void UpdateAllOnUIThread() {
        // INÍCIO DO FRAME DE UI

        // 1) Drena combos pendentes (garante mutações em 'combos' só na UI)
        InjectHUD::OnUIFrameBegin();

        // 2) Carimbo de tempo (horas de jogo -> segundos)
        const float now = RE::Calendar::GetSingleton()->GetCurrentGameTime() * 3600.0f;

        constexpr float ZERO_GRACE_SECONDS = 0.1f;

        std::unordered_set<RE::FormID> alive;
        alive.reserve(64);

        // coleta para remoção em lote (fora dos loops)
        std::vector<RE::FormID> toRemove;
        toRemove.reserve(16);

        // 3) Varre todos os alvos com gauges vivos/decay e processa
        ElementalGauges::ForEachDecayed([&](RE::FormID id, ElementalGauges::TotalsView) {
            alive.insert(id);

            RE::Actor* a = nullptr;
            if (auto itW = InjectHUD::widgets.find(id); itW != InjectHUD::widgets.end()) {
                a = (itW->second.handle) ? itW->second.handle.get().get() : nullptr;
            }
            if (!a) {
                a = RE::TESForm::LookupByID<RE::Actor>(id);
                if (a) {
                    auto& entry = InjectHUD::widgets[id];
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

            // Garante slot 0 apenas quando temos 'a' por handle/bootstrap
            InjectHUD::AddFor(a);

            // Janela de graça para evitar piscadas no 1º frame
            auto [itAdd, inserted] = lastAddedAt.try_emplace(id, now);
            const bool inGrace = inserted || ((now - itAdd->second) < INIT_GRACE_SECONDS);

            if (!inGrace) {
                InjectHUD::UpdateFor(a);
            } else {
            }

            lastSeen[id] = now;
        });

        // 4) Varre o mapa de widgets e marca para remover quem não apareceu neste frame
        for (auto it = InjectHUD::widgets.begin(); it != InjectHUD::widgets.end(); ++it) {
            const RE::FormID id = it->first;
            const bool isAlive = (alive.find(id) != alive.end());

            if (!isAlive) {
                const float seen = lastSeen.contains(id) ? lastSeen[id] : 0.0f;
                const float age = now - seen;

                // pequena graça p/ evitar piscadas entre frames
                if (age >= ZERO_GRACE_SECONDS) {
                    toRemove.push_back(id);
                    lastAddedAt.erase(id);
                    lastSeen.erase(id);
                }
            }
        }

        // 4b) Remoção em lote (fora de qualquer iteração)
        if (!toRemove.empty()) {
            for (auto id : toRemove) {
                InjectHUD::RemoveFor(id);
            }
        }

        // 5) Limpa entradas antigas em lastSeen que já não têm widgets
        for (auto it = lastSeen.begin(); it != lastSeen.end();) {
            const RE::FormID id = it->first;
            const bool hasWidgets = (InjectHUD::widgets.find(id) != InjectHUD::widgets.end());
            const bool aliveNow = (alive.find(id) != alive.end());
            const float age = now - it->second;

            if (!aliveNow && !hasWidgets && age >= ZERO_GRACE_SECONDS) {
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
            // 1) espera até próximo período OU um notify
            int waitMs = g_periodMs.load(std::memory_order_relaxed);
            {
                std::unique_lock lk(g_cv_m);
                g_cv.wait_for(lk, milliseconds(waitMs), [] { return !g_run.load() || g_wake.load(); });
                g_wake.store(false, std::memory_order_relaxed);
            }
            if (!g_run.load(std::memory_order_relaxed)) break;

            // 2) agenda a tarefa de UI
            if (auto* ti = SKSE::GetTaskInterface()) {
                ti->AddUITask([] { UpdateAllOnUIThread(); });
            }

            // 3) decide o próximo período (usa resultado do frame anterior)
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
