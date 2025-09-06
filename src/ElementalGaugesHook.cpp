#include "ElementalGaugesHook.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>

#include "ElementalGauges.h"
#include "RE/P/PeakValueModifierEffect.h"

namespace GaugesHook {
    using Elem = ElementalGauges::Type;

    struct Cfg {
        float pctFire = 0.10f;
        float pctFrost = 0.10f;
        float pctShock = 0.10f;
    };
    static Cfg& cfg() {
        static Cfg c;  // NOSONAR
        return c;
    }

    // -------- helpers --------
    static RE::Actor* AsActor(RE::MagicTarget* mt) {
        if (!mt) return nullptr;
        if (auto a = skyrim_cast<RE::Actor*>(mt)) return a;
        return nullptr;
    }
    static float GetHealth(RE::Actor* a) {
        if (!a) return 0.0f;
        if (auto avo = skyrim_cast<RE::ActorValueOwner*>(a)) return avo->GetActorValue(RE::ActorValue::kHealth);
        return 0.0f;
    }
    static std::optional<Elem> ClassifyElement(const RE::EffectSetting* mgef) {
        if (!mgef) return std::nullopt;
        using enum ElementalGauges::Type;
        using enum RE::ActorValue;
        switch (mgef->data.resistVariable) {
            case kResistFire:
                return Fire;
            case kResistFrost:
                return Frost;
            case kResistShock:
                return Shock;
            default:
                return std::nullopt;
        }
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
        return (static_cast<std::uint64_t>(a->GetFormID()) << 8) | static_cast<std::uint64_t>(std::to_underlying(e));
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
                    const auto base = static_cast<std::uint64_t>(tgt->GetFormID()) << 8;
                    std::unique_lock lk(g_accumMx);
                    for (auto it = g_accum.begin(); it != g_accum.end();)
                        it = ((it->first >> 8) == (base >> 8)) ? g_accum.erase(it) : std::next(it);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    inline void RegisterAEEventSinkImpl() {
        if (auto* src = RE::ScriptEventSourceHolder::GetSingleton())
            src->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(AEApplyRemoveSink::GetSingleton());
    }

    // -------- HOOKS --------
    template <class T>
    struct StartHook {
        static constexpr std::size_t kIndex = 0x14;  // Start
        using Fn = void(T*, RE::MagicTarget*);
        static inline Fn* _orig{};

        static void thunk(T* self, RE::MagicTarget* mt) {
            _orig(self, mt);
            const auto* mgef = self->GetBaseObject();
            RE::Actor* actor = AsActor(self->target);
            if (!mgef || !actor) return;
            if (auto elem = ClassifyElement(mgef)) {
                std::unique_lock lk(g_ctxMx);
                g_ctx[self] = EffCtx{actor->CreateRefHandle(), *elem, self->usUniqueID};
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
                const auto* mgef = self->GetBaseObject();
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

            const float hpBefore = GetHealth(target);
            _orig(self, dt);
            const float hpAfter = GetHealth(target);
            const float dmg = hpBefore - hpAfter;
            if (dmg <= 0.0f) return;

            float pct = 0.0f;
            using enum ElementalGauges::Type;
            switch (elem) {
                case Fire:
                    pct = cfg().pctFire;
                    break;
                case Frost:
                    pct = cfg().pctFrost;
                    break;
                case Shock:
                    pct = cfg().pctShock;
                    break;
                default:
                    break;
            }
            if (pct <= 0.0f) return;

            const double add = static_cast<double>(dmg) * static_cast<double>(pct);
            const auto key = MakeKey(target, elem);
            int inc = 0;
            {
                std::unique_lock lk(g_accumMx);
                double& acc = g_accum[key];
                acc += add;
                inc = static_cast<int>(acc);
                if (inc >= 1) acc -= inc;
            }
            if (inc > 0) ElementalGauges::Add(target, elem, inc);
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
        StartHook<VME>::Install("ValueModifierEffect");
        UpdateHook<VME>::Install("ValueModifierEffect");
        StartHook<DVME>::Install("DualValueModifierEffect");
        UpdateHook<DVME>::Install("DualValueModifierEffect");
        StartHook<PVME>::Install("PeakValueModifierEffect");
        UpdateHook<PVME>::Install("PeakValueModifierEffect");
        StartHook<RE::ActiveEffect>::Install("ActiveEffect");
    }
}

namespace ElementalGaugesHook {
    void Install() { GaugesHook::InstallAll(); }
    void RegisterAEEventSink() { GaugesHook::RegisterAEEventSinkImpl(); }
}