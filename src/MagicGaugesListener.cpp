#include "MagicGaugesListener.h"

#include <array>
#include <atomic>
#include <unordered_map>
#include <utility>

#include "ElementalGauges.h"

namespace {
    using Type = ElementalGauges::Type;

    struct Config {
        std::array<int, 3> inc{15, 15, 15};  // Fire, Frost, Shock
    };
    inline Config& cfg() {
        static Config c;  // NOSONAR
        return c;
    }

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    constexpr float kDebounceSec = 0.2f;
    constexpr float kDebounceHours = kDebounceSec / 3600.0f;

    struct DebounceState {
        std::unordered_map<RE::FormID, std::array<float, 3>> lastH;
        void clear() { lastH.clear(); }
    };

    inline DebounceState& debounce() {
        static DebounceState d;  // NOSONAR
        return d;
    }

    inline std::size_t elem_index(Type t) { return static_cast<std::size_t>(std::to_underlying(t)); }

    inline bool AsElement(const RE::EffectSetting* mgef, Type& out) {
        if (!mgef) return false;

        using enum RE::ActorValue;
        using enum ElementalGauges::Type;
        switch (mgef->data.resistVariable) {
            case kResistFire:
                out = Fire;
                return true;
            case kResistFrost:
                out = Frost;
                return true;
            case kResistShock:
                out = Shock;
                return true;
            default:
                return false;
        }
    }

    class MagicApplySink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent> {
    public:
        static MagicApplySink& Instance() {
            static MagicApplySink s;  // NOSONAR
            return s;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* evn,
                                              RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override {
            using enum RE::BSEventNotifyControl;
            if (!evn) return kContinue;

            RE::TESObjectREFR* targetRef = evn->target.get();
            if (!targetRef) {
                return kContinue;
            }

            const auto* target = targetRef->As<RE::Actor>();
            if (!target) return kContinue;

            Type elem{};

            const RE::EffectSetting* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(evn->magicEffect);
            if (!mgef) {
                return kContinue;
            }
            if (!AsElement(mgef, elem)) {
                return kContinue;
            }

            const auto nowH = NowHours();
            const auto idx = elem_index(elem);
            auto& arr = debounce().lastH[target->GetFormID()];  // cria {0,0,0} se não existir

            if (nowH - arr[idx] < kDebounceHours) {
                return kContinue;
            }

            arr[idx] = nowH;

            if (int inc = cfg().inc[elem_index(elem)]; inc != 0) {
                ElementalGauges::Add(target, elem, inc);
            }

            return kContinue;
        }
    };
}

namespace MagicGauges {
    void Install() {
        cfg().inc = {10, 10, 10};

        if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
            src->AddEventSink<RE::TESMagicEffectApplyEvent>(&MagicApplySink::Instance());
        }
    }

    void ResetDebounce() { debounce().clear(); }
}
