#pragma once

#include <array>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
        WidgetPtr widget;
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
        explicit ERFWidget() = default;

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

        static constexpr float kPlayerMarginLeftPx = 45.0f;
        static constexpr float kPlayerMarginBottomPx = 160.0f;
        static constexpr float kPlayerScale = 1.5f;

        void Initialize() override;
        void Update(float) override {}
        void Dispose() override {}

        void FollowActorHead(RE::Actor* actor);

        void SetAll(const std::vector<double>& comboRemain01, const std::vector<std::uint32_t>& comboTintsRGB,
                    const std::vector<double>& accumValues, const std::vector<std::uint32_t>& accumColorsRGB);

        void ResetSmoothing() { _lastX = _lastY = std::numeric_limits<double>::quiet_NaN(); }
        void ClearAndHide();

    private:
        bool _arraysInit{false};

        RE::GFxValue _arrComboRemain;
        RE::GFxValue _arrComboTints;
        RE::GFxValue _arrAccumVals;
        RE::GFxValue _arrAccumCols;
        RE::GFxValue _isSingle;

        RE::GFxValue _args[5];

        void EnsureArrays();
        void FillArrayDoubles(RE::GFxValue& arr, const std::vector<double>& src);
        void FillArrayU32AsNumber(RE::GFxValue& arr, const std::vector<std::uint32_t>& src);
    };

    void AddFor(RE::Actor* actor);
    void UpdateFor(RE::Actor* actor, double nowRtS, float nowH);
    void BeginReaction(RE::Actor* a, ERF_ReactionHandle handle, float seconds, bool realTime);

    bool HideFor(RE::FormID id);
    bool RemoveFor(RE::FormID id);
    void RemoveAllWidgets();

    void OnTrueHUDClose();
    void OnUIFrameBegin(double nowRtS, float nowH);

    bool IsOnScreen(RE::Actor* a, float worldOffsetZ = 70.0f) noexcept;

    inline double NowRtS() {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        return std::chrono::duration<double>(clock::now() - t0).count();
    }
}
