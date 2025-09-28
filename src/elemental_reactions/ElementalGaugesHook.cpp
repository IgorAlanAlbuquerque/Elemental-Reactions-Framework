#include "ElementalGaugesHook.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <shared_mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "../hud/HUDTick.h"
#include "ElementalGauges.h"
#include "RE/P/PeakValueModifierEffect.h"
#include "erf_element.h"

using namespace std::chrono;

namespace GaugesHook {
    using Elem = ERF_ElementHandle;

    static RE::BGSKeyword* g_kwGaugeAcc = nullptr;
    static RE::EffectSetting* g_mgefGaugeAcc = nullptr;

    static std::unordered_map<std::uint64_t, double> g_lastAccHint;
    static std::shared_mutex g_accHintMx;

    // -------- helpers --------
    static std::uint64_t KeyActorOnly(const RE::Actor* a) { return static_cast<std::uint64_t>(a ? a->GetFormID() : 0); }

    static RE::BGSKeyword* LookupKW(const char* edid, std::uint32_t formID = 0) {
        if (auto* k = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(edid)) return k;
        if (formID) return RE::TESForm::LookupByID<RE::BGSKeyword>(formID);
        return nullptr;
    }
    static RE::EffectSetting* LookupMGEF(const char* edid, std::uint32_t formID = 0) {
        if (auto* m = RE::TESForm::LookupByEditorID<RE::EffectSetting>(edid)) return m;
        if (formID) return RE::TESForm::LookupByID<RE::EffectSetting>(formID);
        return nullptr;
    }

    static bool IsGaugeAccCarrier(const RE::EffectSetting* mgef) {
        if (!mgef) return false;

        if (g_mgefGaugeAcc && mgef == g_mgefGaugeAcc) {
            return true;
        }

        if (g_kwGaugeAcc) {
            const auto kwIDWant = g_kwGaugeAcc->GetFormID();
            std::size_t i = 0;
            for (auto* kw : mgef->GetKeywords()) {
                const auto id = kw ? kw->GetFormID() : 0u;
                if (kw && id == kwIDWant) {
                    return true;
                }
            }
        }
        return false;
    }

    static RE::Actor* AsActor(RE::MagicTarget* mt) {
        if (!mt) return nullptr;
        if (auto a = skyrim_cast<RE::Actor*>(mt)) return a;
        return nullptr;
    }

    static std::optional<Elem> ClassifyElement(const RE::EffectSetting* mgef) {
        if (!mgef) return std::nullopt;
        if (IsGaugeAccCarrier(mgef)) {
            return std::nullopt;
        }

        const auto kws = mgef->GetKeywords();
        std::size_t i = 0;
        for (RE::BGSKeyword* kw : kws) {
            if (!kw) continue;
            if (auto h = ElementRegistry::get().findByKeyword(kw)) {
                return *h;
            }
        }
        return std::nullopt;
    }

    // -------- contexto por ActiveEffect --------
    struct EffCtx {
        RE::ActorHandle target;
        Elem elem;
        std::uint16_t uid;
    };

    static std::unordered_map<const RE::ActiveEffect*, EffCtx> g_ctx;  // NOSONAR
    static std::shared_mutex g_ctxMx;                                  // NOSONAR

    static std::unordered_map<std::uint64_t, double> g_accum;  // NOSONAR
    static std::shared_mutex g_accumMx;                        // NOSONAR
    static std::uint64_t MakeKey(const RE::Actor* a, Elem e) {
        const std::uint64_t hi = static_cast<std::uint64_t>(a ? a->GetFormID() : 0);
        const std::uint64_t lo = static_cast<std::uint64_t>(e);
        return (hi << 32) | lo;
    }

    // -------- EVENTO: limpeza (remove ctx por UID e limpa acumulador do alvo) --------
    class AEApplyRemoveSink final : public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent> {
    public:
        static AEApplyRemoveSink* GetSingleton() {
            static AEApplyRemoveSink s;
            return std::addressof(s);
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* e,
                                              RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override {
            if (!e) return RE::BSEventNotifyControl::kContinue;

            const auto uid = e->activeEffectUniqueID;
            const RE::Actor* tgt = e->target ? e->target->As<RE::Actor>() : nullptr;

            if (!e->isApplied) {
                {
                    std::unique_lock lk(g_ctxMx);
                    std::erase_if(g_ctx, [&](auto& kv) {
                        const auto& ctx = kv.second;
                        if (ctx.uid != uid) return false;
                        if (tgt) {
                            const RE::Actor* a = ctx.target.get().get();
                            if (!a || a != tgt) return false;
                        }
                        return true;
                    });
                }
                if (tgt) {
                    const auto key = KeyActorOnly(tgt);
                    {
                        std::unique_lock lk(g_accHintMx);
                        g_lastAccHint.erase(key);  // << limpa o hint ao remover efeitos
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    inline void RegisterAEEventSinkImpl() {
        if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
            src->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(AEApplyRemoveSink::GetSingleton());
            spdlog::info("[Hook] AEApplyRemoveSink registered.");
        }
    }

    // -------- HOOKS --------
    template <class T>
    struct StartHook {
        static constexpr std::size_t kIndex = 0x14;  // Start
        using Fn = void(T*, RE::MagicTarget*);
        static inline Fn* _orig{};

        static bool IsInstantaneous(const RE::EffectSetting* mgef, const T* self) {
            const bool noDurationFlag =
                (mgef && (mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoDuration)));
            const bool zeroDur = (self && self->duration <= 0.01f);
            return noDurationFlag || zeroDur;
        }

        static void thunk(T* self, RE::MagicTarget* mt) {
            _orig(self, mt);

            // --- pegue os objetos com tipos corretos ---
            const RE::EffectSetting* mgef = self ? self->GetBaseObject() : nullptr;

            RE::Actor* actor = AsActor(self ? self->target : nullptr);
            if (!mgef || !actor) return;

            if (IsGaugeAccCarrier(mgef)) {
                double acc = static_cast<double>(self->magnitude);
                if (acc <= 0.001 && self->effect) {
                    acc = static_cast<double>(self->effect->effectItem.magnitude);
                }
                if (acc <= 0.001) acc = 1.0;
                {
                    std::unique_lock lk(g_accHintMx);
                    g_lastAccHint[KeyActorOnly(actor)] = acc;
                }
                return;
            }

            if (auto elem = ClassifyElement(mgef)) {
                {
                    std::unique_lock lk(g_ctxMx);
                    g_ctx[self] = EffCtx{actor->CreateRefHandle(), *elem, self->usUniqueID};
                }

                if (IsInstantaneous(mgef, self)) {
                    double acc = 0.0;
                    {
                        std::shared_lock lk(g_accHintMx);
                        auto it = g_lastAccHint.find(KeyActorOnly(actor));
                        if (it != g_lastAccHint.end()) acc = it->second;
                    }
                    if (acc > 0.001) {
                        const int inc = std::max(1, (int)std::lround(acc));
                        ElementalGaugesHook::StartHUDTick();
                        ElementalGauges::Add(actor, *elem, inc);
                    }
                }
            }
        }

        static void Install(const char*) {
            REL::Relocation<std::uintptr_t> vtbl{T::VTABLE[0]};
            const auto old = vtbl.write_vfunc(kIndex, &thunk);
            _orig = reinterpret_cast<Fn*>(old);
        }
    };

    template <class T>
    struct UpdateHook {
        static constexpr std::size_t kIndex = 0x04;  // Update
        using Fn = void(T*, float);
        static inline Fn* _orig{};

        static void thunk(T* self, float dt) {
            const auto* mgef = self->GetBaseObject();
            if (mgef && IsGaugeAccCarrier(mgef)) {
                _orig(self, dt);
                return;
            }

            RE::Actor* target = nullptr;
            Elem elem{};
            bool haveCtx = false;

            {
                std::shared_lock lk(g_ctxMx);
                if (auto it = g_ctx.find(self); it != g_ctx.end()) {
                    haveCtx = true;
                    target = it->second.target.get().get();
                    elem = it->second.elem;
                }
            }

            if (!haveCtx) {
                RE::Actor* actor = AsActor(self->target);
                if (mgef && actor) {
                    if (auto e = ClassifyElement(mgef)) {
                        {
                            std::unique_lock lk(g_ctxMx);
                            g_ctx[self] = EffCtx{actor->CreateRefHandle(), *e, self->usUniqueID};
                        }
                        target = actor;
                        elem = *e;
                        haveCtx = true;
                    }
                }
            }

            if (!haveCtx || !target) {
                _orig(self, dt);
                return;
            }

            double accPerSec = 0.0;
            {
                std::shared_lock lk(g_accHintMx);
                auto it = g_lastAccHint.find(KeyActorOnly(target));
                if (it != g_lastAccHint.end()) accPerSec = it->second;
            }
            if (accPerSec <= 0.001) {
                _orig(self, dt);
                return;
            }

            int inc = 0;
            if (dt > 0.0f) {
                const auto key = MakeKey(target, elem);
                std::unique_lock lk(g_accumMx);
                double& acc = g_accum[key];
                const double add = accPerSec * static_cast<double>(dt);
                acc += add;
                inc = static_cast<int>(std::floor(acc));
                if (inc >= 1) acc -= inc;
            }

            if (inc > 0) {
                ElementalGaugesHook::StartHUDTick();
                ElementalGauges::Add(target, elem, inc);
            }

            _orig(self, dt);
        }

        static void Install(const char*) {
            REL::Relocation<std::uintptr_t> vtbl{T::VTABLE[0]};
            const auto old = vtbl.write_vfunc(kIndex, &thunk);
            _orig = reinterpret_cast<Fn*>(old);
        }
    };

    using VME = RE::ValueModifierEffect;
    using DVME = RE::DualValueModifierEffect;
    using PVME = RE::PeakValueModifierEffect;

    static void InstallAll() {
        spdlog::info("[Hook] Installing vfunc hooks...");
        StartHook<VME>::Install("ValueModifierEffect");
        UpdateHook<VME>::Install("ValueModifierEffect");
        StartHook<DVME>::Install("DualValueModifierEffect");
        UpdateHook<DVME>::Install("DualValueModifierEffect");
        StartHook<PVME>::Install("PeakValueModifierEffect");
        UpdateHook<PVME>::Install("PeakValueModifierEffect");
        StartHook<RE::ActiveEffect>::Install("ActiveEffect");
        UpdateHook<RE::ActiveEffect>::Install("ActiveEffect");
        spdlog::info("[Hook] All hooks installed.");
    }
}

namespace HookThread {
    static std::atomic<long long> g_lastHookMs{0};
    static std::atomic_bool g_monitorRunning{false};

    static void EnsureWatchdog() {
        bool expected = false;
        if (!g_monitorRunning.compare_exchange_strong(expected, true)) return;

        std::thread([] {
            const auto timeout = 15s;
            for (;;) {
                std::this_thread::sleep_for(1s);
                const auto last_local = milliseconds(g_lastHookMs.load(std::memory_order_relaxed));
                const auto now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch());
                if (now - last_local > timeout) {
                    HUD::StopHUDTick();
                    g_monitorRunning.store(false, std::memory_order_release);
                    break;
                }
            }
        }).detach();
    }
}

std::atomic_bool ElementalGaugesHook::ALLOW_HUD_TICK{false};

void ElementalGaugesHook::StartHUDTick() {
    if (!ALLOW_HUD_TICK.load(std::memory_order_acquire)) return;
    const auto now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    HookThread::g_lastHookMs.store(now, std::memory_order_relaxed);
    HUD::StartHUDTick();
    HookThread::EnsureWatchdog();
}

void ElementalGaugesHook::StopHUDTick() { HUD::StopHUDTick(); }
void ElementalGaugesHook::Install() {
    spdlog::info("[Hook] Install()");
    GaugesHook::InstallAll();
}
void ElementalGaugesHook::RegisterAEEventSink() {
    spdlog::info("[Hook] RegisterAEEventSink()");
    GaugesHook::RegisterAEEventSinkImpl();
}

void ElementalGaugesHook::InitCarrierRefs() {
    using namespace GaugesHook;
    static constexpr const char* kPlugin = "ERF_Keywords.esp";
    constexpr std::uint32_t kMgefLocalID = 0x059800;
    constexpr std::uint32_t kKwLocalID = 0x000000;

    auto* dh = RE::TESDataHandler::GetSingleton();

    g_mgefGaugeAcc = dh ? dh->LookupForm<RE::EffectSetting>(kMgefLocalID, kPlugin) : nullptr;
    if (!g_mgefGaugeAcc) {
        g_mgefGaugeAcc = LookupMGEF("ERF_GaugeAccEffect");
    }

    if (kKwLocalID) {
        g_kwGaugeAcc = dh ? dh->LookupForm<RE::BGSKeyword>(kKwLocalID, kPlugin) : nullptr;
    }
    if (!g_kwGaugeAcc) {
        g_kwGaugeAcc = LookupKW("ERF_GaugeAccKW");
    }
}