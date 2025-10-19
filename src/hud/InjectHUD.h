#pragma once

#include <array>
#include <string>
#include <unordered_map>

#include "../common/Helpers.h"
#include "../elemental_reactions/erf_reaction.h"
#include "SKSE/SKSE.h"
#include "TrueHUDAPI.h"

namespace InjectHUD {
    struct PendingReaction {
        RE::FormID id{};
        RE::ActorHandle handle{};
        ERF_ReactionHandle reaction{};
        float secs{0.f};
        bool realTime{true};
    };

    struct ActiveReactionHUD {
        ERF_ReactionHandle reaction{};
        bool realTime{true};
        double endRtS{0.0};
        float endH{0.f};
        float durationS{0.f};

        std::string iconPath;
        std::uint32_t tint{0xFFFFFF};
    };

    struct Smooth01 {
        double v{0.0};
        bool init{false};
    };

    class ERFWidget;
    using WidgetPtr = std::shared_ptr<ERFWidget>;
    struct HUDEntry {
        RE::ActorHandle handle{};
        std::vector<WidgetPtr> slots;
    };

    constexpr auto ERF_SWF_PATH = "erfgauge/erfgauge.swf";
    constexpr auto ERF_SYMBOL_NAME = "ERF_Gauge";
    constexpr uint32_t ERF_WIDGET_TYPE = FOURCC('E', 'L', 'R', 'E');

    extern std::unordered_map<RE::FormID, HUDEntry> widgets;
    extern std::unordered_map<RE::FormID, std::vector<ActiveReactionHUD>> combos;
    extern std::deque<PendingReaction> g_comboQueue;
    extern std::mutex g_comboMx;
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
        void SetIconAndGauge(const std::string& iconPath, std::span<const uint32_t> values,
                             std::span<const uint32_t> colors, uint32_t tintRGB);

        void SetCombo(const std::string& iconPath, float remaining01, std::uint32_t tintRGB);

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
    void BeginReaction(RE::Actor* a, ERF_ReactionHandle handle, float seconds, bool realTime);
    bool RemoveFor(RE::FormID id);
    void RemoveAllWidgets();
    void OnTrueHUDClose();
    void OnUIFrameBegin();
    inline double NowRtS();
}
