#pragma once

#include <array>
#include <unordered_map>

#include "SKSE/SKSE.h"
#include "TrueHUDAPI.h"

namespace InjectHUD {
    struct ComboHUD {
        ElementalGauges::Combo which{};
        double endRtS{};  // quando expira (tempo real)
        float endH{};     // quando expira (horas de jogo)
        bool realTime{};  // true = usa endRtS; false = endH
        float durationS{};
        int iconId{};          // 0..6
        std::uint32_t tint{};  // 0xRRGGBB
    };

    constexpr auto SMSO_SWF_PATH = "smsogauge.swf";
    constexpr auto SMSO_SYMBOL_NAME = "SMSO_Gauge";
    constexpr uint32_t SMSO_WIDGET_TYPE = 'SMSO';
    extern TRUEHUD_API::IVTrueHUD4* g_trueHUD;
    extern SKSE::PluginHandle g_pluginHandle;

    extern std::unordered_map<RE::FormID, std::vector<ComboHUD>> combos;

    struct Smooth01 {
        double v{0.0};
        bool init{false};
    };

    class SMSOWidget : public TRUEHUD_API::WidgetBase {
    public:
        SMSOWidget() = default;
        SMSOWidget(int slot = 0) : _slot(slot) {}

        int _slot{0};
        float _slotSpacingPx{24};

        bool _hadContent{false};
        double _lastGaugeRtS{std::numeric_limits<double>::quiet_NaN()};

        void Initialize() override {
            if (!_view) return;

            RE::GFxValue vis;
            vis.SetBoolean(false);
            _object.SetMember("_visible", vis);

            RE::GFxValue alpha;
            alpha.SetNumber(100.0);
            _object.SetMember("_alpha", alpha);
        }

        void Update(float) override {}
        void Dispose() override {}

        void FollowActorHead(RE::Actor* actor);

        void SetIconAndGauge(uint32_t iconId, uint32_t fire, uint32_t frost, uint32_t shock, uint32_t tintRGB);

        void SetCombo(int iconId, float remaining01, std::uint32_t tintRGB);

        double _lastX{std::numeric_limits<double>::quiet_NaN()};
        double _lastY{std::numeric_limits<double>::quiet_NaN()};

        Smooth01 _fireDisp{}, _frostDisp{}, _shockDisp{};
        Smooth01 _comboDisp{};

        double _risePerSec = 90.0;
        double _fallPerSec = 600.0;
        double _comboPerSec = 2.5;
        void ResetSmoothing() { _lastX = _lastY = std::numeric_limits<double>::quiet_NaN(); }
    };

    extern std::unordered_map<RE::FormID, std::vector<std::shared_ptr<SMSOWidget>>> widgets;

    void BeginCombo(RE::Actor* a, ElementalGauges::Combo which, float seconds, bool realTime);
    void AddFor(RE::Actor* actor);
    void UpdateFor(RE::Actor* actor);
    bool RemoveFor(RE::FormID id);

    void RemoveAllWidgets();
    void OnTrueHUDClose();
}
