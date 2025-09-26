#pragma once

#include <array>
#include <unordered_map>

#include "SKSE/SKSE.h"
#include "TrueHUDAPI.h"

namespace InjectHUD {
    struct ComboHUD {
        ElementalGauges::Combo which{};
        double endRtS{};
        float endH{};
        bool realTime{};
        float durationS{};
        int iconId{};
        std::uint32_t tint{};
    };

    struct Smooth01 {
        double v{0.0};
        bool init{false};
    };

    class ERFWidget;
    using WidgetPtr = std::shared_ptr<ERFWidget>;

    constexpr auto ERF_SWF_PATH = "erfgauge.swf";
    constexpr auto ERF_SYMBOL_NAME = "ERF_Gauge";
    constexpr uint32_t ERF_WIDGET_TYPE = 'ELRE';

    extern std::unordered_map<RE::FormID, std::vector<WidgetPtr>> widgets;
    extern std::unordered_map<RE::FormID, std::vector<ComboHUD>> combos;
    extern TRUEHUD_API::IVTrueHUD4* g_trueHUD;
    extern SKSE::PluginHandle g_pluginHandle;

    class ERFWidget : public TRUEHUD_API::WidgetBase {
    public:
        explicit ERFWidget(int slot = 0) : _slot(slot) {}

        int _slot{0};
        int _pos = 0;
        bool _needsSnap = true;

        double _lastX{std::numeric_limits<double>::quiet_NaN()};
        double _lastY{std::numeric_limits<double>::quiet_NaN()};

        bool _hadContent{false};
        double _lastGaugeRtS{std::numeric_limits<double>::quiet_NaN()};

        double _risePerSec = 90.0;
        double _fallPerSec = 600.0;
        double _comboPerSec = 2.5;

        Smooth01 _fireDisp{};
        Smooth01 _frostDisp{};
        Smooth01 _shockDisp{};
        Smooth01 _comboDisp{};

        static constexpr double kSlotSpacingPx = 30.0;
        static constexpr double kTopOffsetPx = 0.0;

        static constexpr float kPlayerMarginLeftPx = 45.0f;
        static constexpr float kPlayerMarginBottomPx = 160.0f;
        static constexpr float kPlayerScale = 1.5f;

        void Initialize() override;
        void Update(float) override {}
        void Dispose() override {}

        void FollowActorHead(RE::Actor* actor);
        void SetIconAndGauge(uint32_t iconId, const std::vector<uint32_t>& values, const std::vector<uint32_t>& colors,
                             uint32_t tintRGB);
        void SetCombo(int iconId, float remaining01, std::uint32_t tintRGB);

        void ResetSmoothing() { _lastX = _lastY = std::numeric_limits<double>::quiet_NaN(); }
        void SetPos(int p) {
            if (p == _pos) return;
            _pos = p;
            _needsSnap = true;
            ResetSmoothing();
        }
    };

    void AddFor(RE::Actor* actor);
    void UpdateFor(RE::Actor* actor);
    void BeginCombo(RE::Actor* a, ElementalGauges::Combo which, float seconds, bool realTime);
    bool RemoveFor(RE::FormID id);
    void RemoveAllWidgets();
    void OnTrueHUDClose();
    void OnUIFrameBegin();
    inline double NowRtS();
    std::uint32_t MakeWidgetID(RE::FormID id, int slot);
}
