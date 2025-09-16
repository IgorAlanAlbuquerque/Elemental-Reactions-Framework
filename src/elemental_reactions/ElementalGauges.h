#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace ElementalGauges {

    enum class Type : std::uint8_t { Fire = 0, Frost = 1, Shock = 2 };

    enum class Combo : std::uint8_t {
        Fire = 0,
        Frost,
        Shock,
        FireFrost,
        FrostFire,
        FireShock,
        ShockFire,
        FrostShock,
        ShockFrost,
        FireFrostShock,
        _COUNT
    };

    enum class HudIcon : std::uint8_t {
        Fire = 0,       // icon_fire
        Frost,          // icon_frost
        Shock,          // icon_shock
        FireFrost,      // icon_fire_frost
        FireShock,      // icon_fire_shock
        FrostShock,     // icon_frost_shock
        FireFrostShock  // icon_fire_frost_shock
    };

    struct HudIconSel {
        int id;                 // cast de HudIcon
        std::uint32_t tintRGB;  // 0xRRGGBB (par direcional: cor do elemento dominante)
        Combo combo;            // qual combinação lógica foi escolhida
    };

    struct Totals {
        std::uint8_t fire{};
        std::uint8_t frost{};
        std::uint8_t shock{};
        bool any() const { return fire | frost | shock; }
        std::uint8_t sum() const { return std::uint8_t(int(fire) + int(frost) + int(shock)); }
    };

    std::optional<HudIconSel> PickHudIcon(const Totals& t);

    // Conveniência: aplica decay/GC e já devolve o ícone para um ator
    std::optional<HudIconSel> PickHudIconDecayed(RE::FormID id);

    struct SumComboTrigger {
        using Callback = void (*)(RE::Actor* actor, Combo which, void* user);
        Callback cb{nullptr};
        void* user{nullptr};

        // Regras
        float majorityPct{0.85f};
        float tripleMinPct{0.28f};

        // Execução / antispam
        float cooldownSeconds{0.5f};
        bool cooldownIsRealTime{true};
        bool deferToTask{true};
        bool clearAllOnTrigger{true};

        // NOVO: lockout para ACUMULAR GAUGE dos elementos envolvidos no combo
        float elementLockoutSeconds{0.0f};    // 0 = sem lockout de acumulo
        bool elementLockoutIsRealTime{true};  // true = segundos reais; false = tempo de jogo
    };

    void SetOnSumCombo(Combo c, const SumComboTrigger& cfg);

    std::uint8_t Get(RE::Actor* a, Type t);
    void Set(RE::Actor* a, Type t, std::uint8_t value);
    void Add(RE::Actor* a, Type t, int delta);
    void Clear(RE::Actor* a);

    void RegisterStore();

    void ForEachDecayed(const std::function<void(RE::FormID, const Totals&)>& fn);

    std::vector<std::pair<RE::FormID, Totals>> SnapshotDecayed();

    std::optional<Totals> GetTotalsDecayed(RE::FormID id);

    void GarbageCollectDecayed();
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
