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
        // INÍCIO DO FRAME DE UI
        const double tFrameBegin = InjectHUD::NowRtS();
        spdlog::info("[HUDTick] UI task begin");

        // 1) Drena combos pendentes (garante mutações em 'combos' só na UI)
        spdlog::info("[HUDTick] OnUIFrameBegin (drain combo queue)");
        InjectHUD::OnUIFrameBegin();

        // 2) Carimbo de tempo (horas de jogo -> segundos)
        const float now = RE::Calendar::GetSingleton()->GetCurrentGameTime() * 3600.0f;
        spdlog::info("[HUDTick] UpdateAllOnUIThread now={}s (game secs)", now);

        constexpr float ZERO_GRACE_SECONDS = 0.1f;

        std::unordered_set<RE::FormID> alive;
        alive.reserve(64);

        // coleta para remoção em lote (fora dos loops)
        std::vector<RE::FormID> toRemove;
        toRemove.reserve(16);

        // 3) Varre todos os alvos com gauges vivos/decay e processa
        spdlog::info("[HUDTick] ForEachDecayed begin");
        ElementalGauges::ForEachDecayed([&](RE::FormID id, const ElementalGauges::Totals& totals) {
            spdlog::info("[HUDTick] FE id={:08X} totals F/I/S={}/{}/{}", id, totals.fire, totals.frost, totals.shock);
            alive.insert(id);

            RE::Actor* a = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!a) {
                spdlog::info("[HUDTick] FE id={:08X} actor lookup failed -> mark RemoveFor", id);
                toRemove.push_back(id);
                lastAddedAt.erase(id);
                lastSeen.erase(id);
                return;
            }
            if (a->IsDead()) {
                spdlog::info("[HUDTick] FE id={:08X} actor is dead -> mark RemoveFor", id);
                toRemove.push_back(id);
                lastAddedAt.erase(id);
                lastSeen.erase(id);
                return;
            }

            // Garante que existe pelo menos um widget (slot 0) para este ator
            spdlog::info("[HUDTick] FE id={:08X} calling AddFor()", id);
            InjectHUD::AddFor(a);

            // Janela de graça para evitar piscadas no 1º frame
            auto [itAdd, inserted] = lastAddedAt.try_emplace(id, now);
            const bool inGrace = inserted || ((now - itAdd->second) < INIT_GRACE_SECONDS);
            spdlog::info("[HUDTick] FE id={:08X} inGrace={} (inserted?{}; age={}s)", id, inGrace, inserted,
                         now - itAdd->second);

            if (!inGrace) {
                spdlog::info("[HUDTick] FE id={:08X} calling UpdateFor()", id);
                InjectHUD::UpdateFor(a);
            } else {
                spdlog::info("[HUDTick] FE id={:08X} skipping UpdateFor() due to grace window", id);
            }

            lastSeen[id] = now;
        });
        spdlog::info("[HUDTick] ForEachDecayed end alive_count={}", alive.size());

        // 4) Varre o mapa de widgets e marca para remover quem não apareceu neste frame
        spdlog::info("[HUDTick] Sweep inactive begin widgets_map_size={}", InjectHUD::widgets.size());
        for (auto it = InjectHUD::widgets.begin(); it != InjectHUD::widgets.end(); ++it) {
            const RE::FormID id = it->first;
            const bool isAlive = (alive.find(id) != alive.end());

            if (!isAlive) {
                const float seen = lastSeen.contains(id) ? lastSeen[id] : 0.0f;
                const float age = now - seen;
                spdlog::info("[HUDTick] Sweep id={:08X} not alive this frame; lastSeenAge={}s", id, age);

                // pequena graça p/ evitar piscadas entre frames
                if (age >= ZERO_GRACE_SECONDS) {
                    spdlog::info("[HUDTick] Sweep id={:08X} -> mark RemoveFor()", id);
                    toRemove.push_back(id);
                    lastAddedAt.erase(id);
                    lastSeen.erase(id);
                }
            }
        }
        spdlog::info("[HUDTick] Sweep inactive end widgets_map_size={}", InjectHUD::widgets.size());

        // 4b) Remoção em lote (fora de qualquer iteração)
        if (!toRemove.empty()) {
            spdlog::info("[HUDTick] Sweep removing {} actor widgets (batch)", toRemove.size());
            for (auto id : toRemove) {
                InjectHUD::RemoveFor(id);
            }
        }

        // 5) Limpa entradas antigas em lastSeen que já não têm widgets
        spdlog::info("[HUDTick] Cleanup lastSeen begin size={}", lastSeen.size());
        for (auto it = lastSeen.begin(); it != lastSeen.end();) {
            const RE::FormID id = it->first;
            const bool hasWidgets = (InjectHUD::widgets.find(id) != InjectHUD::widgets.end());
            const bool aliveNow = (alive.find(id) != alive.end());
            const float age = now - it->second;

            if (!aliveNow && !hasWidgets && age >= ZERO_GRACE_SECONDS) {
                spdlog::info("[HUDTick] Cleanup id={:08X} drop (age={}s)", id, age);
                lastAddedAt.erase(id);
                it = lastSeen.erase(it);
            } else {
                ++it;
            }
        }
        spdlog::info("[HUDTick] Cleanup lastSeen end size={}", lastSeen.size());

        const double tFrameEnd = InjectHUD::NowRtS();
        spdlog::info("[HUDTick] UI task end (dt={:.3f} ms)", (tFrameEnd - tFrameBegin) * 1000.0);
    }
}

void HUD::StartHUDTick() {
    if (g_run.exchange(true)) return;

    std::thread([] {
        using namespace std::chrono;
        auto next = steady_clock::now();
        while (g_run.load(std::memory_order_relaxed)) {
            next += milliseconds(16);

            if (auto* ti = SKSE::GetTaskInterface()) {
                ti->AddUITask([]() { UpdateAllOnUIThread(); });
            }

            std::this_thread::sleep_until(next);
        }
    }).detach();
}

void HUD::StopHUDTick() { g_run = false; }

void HUD::ResetTracking() {
    lastSeen.clear();
    lastAddedAt.clear();
}
