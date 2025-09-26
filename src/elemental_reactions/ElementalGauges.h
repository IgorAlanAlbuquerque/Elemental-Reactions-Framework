#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "erf_element.h"

namespace ElementalGauges {
    enum class Combo : std::uint8_t {
        Fire = 0,
        Frost = 1,
        Shock = 2,
        FireFrost = 3,
        FrostFire = 4,
        FireShock = 5,
        ShockFire = 6,
        FrostShock = 7,
        ShockFrost = 8,
        FireFrostShock = 9,
        _COUNT
    };

    enum class HudIcon : std::uint8_t {
        Fire = 0,
        Frost = 1,
        Shock = 2,
        FireFrost = 3,
        FireShock = 4,
        FrostShock = 5,
        FireFrostShock = 6
    };

    struct HudGaugeBundle {
        int iconId{0};
        std::uint32_t iconTint{0};
        std::vector<std::uint32_t> values;
        std::vector<std::uint32_t> colors;
    };

    struct Totals {
        std::vector<std::uint8_t> values;
        bool any() const {
            for (auto v : values)
                if (v > 0) return true;
            return false;
        }
        std::uint32_t sum() const {
            std::uint32_t s = 0;
            for (auto v : values) s += v;
            return s;
        }
    };

    struct SumComboTrigger {
        using Callback = void (*)(RE::Actor* actor, Combo which, void* user);
        Callback cb{nullptr};
        void* user{nullptr};
        float majorityPct{0.85f};
        float tripleMinPct{0.28f};
        float cooldownSeconds{0.5f};
        bool cooldownIsRealTime{true};
        bool deferToTask{true};
        bool clearAllOnTrigger{true};
        float elementLockoutSeconds{0.0f};
        bool elementLockoutIsRealTime{true};
    };

    struct ActiveComboHUD {
        Combo which;
        double remainingRtS;
        double durationRtS;
        bool realTime;
    };

    void SetOnSumCombo(Combo c, const SumComboTrigger& cfg);
    std::uint8_t Get(RE::Actor* a, ERF_ElementHandle elem);
    void Set(RE::Actor* a, ERF_ElementHandle elem, std::uint8_t value);
    void Add(RE::Actor* a, ERF_ElementHandle elem, int delta);
    void Clear(RE::Actor* a);
    void RegisterStore();
    void ForEachDecayed(const std::function<void(RE::FormID, const Totals&)>& fn);
    std::vector<std::pair<RE::FormID, Totals>> SnapshotDecayed();
    std::optional<Totals> GetTotalsDecayed(RE::FormID id);
    void GarbageCollectDecayed();
    std::optional<ActiveComboHUD> PickActiveComboHUD(RE::FormID id);
    std::optional<HudGaugeBundle> PickHudIconDecayed(RE::FormID id);
}

namespace ElementalGaugesDecay {
    inline constexpr float kGraceSec = 5.0f;
    inline constexpr float kRealDecayPerSec = 15.0f;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }
    inline float Timescale() {
        float ts = 20.0f;
        if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
            if (auto* s = gs->GetSetting("fTimescale")) {  // NOSONAR: No const assinatura
                ts = s->GetFloat();
            }
        }
        if (ts < 0.001f) ts = 0.001f;
        return ts;
    }
    inline float DecayPerGameHour() { return kRealDecayPerSec * 3600.0f / Timescale(); }
    inline float GraceGameHours() { return (kGraceSec * Timescale()) / 3600.0f; }
}
