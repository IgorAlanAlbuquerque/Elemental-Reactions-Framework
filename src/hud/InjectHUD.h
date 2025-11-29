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
        const char* icon{nullptr};
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

    struct HUDFrameSnapshot {
        bool hudEnabled{true};
        bool isSingle{true};
        bool playerHorizontal{true};
        bool npcHorizontal{true};
        float playerSpacing{40.0f};
        float npcSpacing{40.0f};
        double nowRtS{0.0};
        float nowH{0.f};
        float playerX{0.0f};
        float playerY{0.0f};
        float playerScale{1.0f};
        float npcX{0.0f};
        float npcY{0.0f};
        float npcScale{1.0f};
    };

    constexpr auto ERF_SWF_PATH = "erfgauge/ERF_UI.swf";
    constexpr auto ERF_SYMBOL_NAME = "ERF_Gauge";
    constexpr uint32_t ERF_WIDGET_TYPE = FOURCC('E', 'L', 'R', 'E');

    struct GlobalState {
        std::unordered_map<RE::FormID, HUDEntry> widgets;
        std::unordered_map<RE::FormID, std::vector<ActiveReactionHUD>> combos;
        std::deque<PendingReaction> comboQueue;
        std::mutex comboMx;
        TRUEHUD_API::IVTrueHUD4* trueHUD{nullptr};
        SKSE::PluginHandle pluginHandle{static_cast<SKSE::PluginHandle>(-1)};
    };

    GlobalState& Globals() noexcept;

    inline auto& Widgets() noexcept { return Globals().widgets; }
    inline auto& Combos() noexcept { return Globals().combos; }
    inline auto& ComboQueue() noexcept { return Globals().comboQueue; }
    inline auto& ComboMutex() noexcept { return Globals().comboMx; }
    inline auto*& TrueHUD() noexcept { return Globals().trueHUD; }
    inline auto& PluginHandle() noexcept { return Globals().pluginHandle; }

    class ERFWidget : public TRUEHUD_API::WidgetBase {
    public:
        explicit ERFWidget() = default;

        bool _needsSnap = true;

        bool _isPlayerWidget = false;

        double _lastX{std::numeric_limits<double>::quiet_NaN()};
        double _lastY{std::numeric_limits<double>::quiet_NaN()};

        static constexpr float kPlayerMarginLeftPx = 180.0f;
        static constexpr float kPlayerMarginBottomPx = 120.0f;
        static constexpr float kPlayerScale = 1.5f;

        bool _lastVisible{false};
        float _lastScale{std::numeric_limits<float>::quiet_NaN()};

        void Initialize() override;
        void Update(float) override {
            // nothing here
        }
        void Dispose() override {
            // nothing here
        }

        void FollowActorHead(RE::Actor* actor);

        void SetAll(const std::vector<double>& comboRemain01, const std::vector<std::uint32_t>& comboTintsRGB,
                    const std::vector<double>& accumValues, const std::vector<std::uint32_t>& accumColorsRGB,
                    const std::vector<const char*>& iconNames, bool isSingle, bool isHorizontal, float spacingPx,
                    std::uint32_t singlesBefore, std::uint32_t singlesAfter);

        void ResetSmoothing() { _lastX = _lastY = std::numeric_limits<double>::quiet_NaN(); }
        void ClearAndHide(bool isSingle, bool isHorizontal, float spacingPx);

    private:
        bool _arraysInit{false};

        RE::GFxValue _arrComboRemain;
        RE::GFxValue _arrComboTints;
        RE::GFxValue _arrAccumVals;
        RE::GFxValue _arrAccumCols;
        RE::GFxValue _arrIconNames;
        RE::GFxValue _isSingle;
        RE::GFxValue _isHorin;
        RE::GFxValue _spacing;
        RE::GFxValue _singlesBefore;
        RE::GFxValue _singlesAfter;

        std::uint64_t _hComboRemain{0};
        std::uint64_t _hComboTints{0};
        std::uint64_t _hAccumVals{0};
        std::uint64_t _hAccumCols{0};
        std::uint64_t _hIconNames{0};

        bool _lastIsSingle{true};
        bool _lastIsHor{true};
        float _lastSpacing{std::numeric_limits<float>::quiet_NaN()};

        RE::GFxValue _args[10];

        void EnsureArrays();
        bool FillArrayNames(RE::GFxValue& arr, const std::vector<const char*>& names, std::uint64_t& lastHash);
        bool FillArrayDoubles(RE::GFxValue& arr, const std::vector<double>& src, std::uint64_t& lastHash);
        bool FillArrayU32AsNumber(RE::GFxValue& arr, const std::vector<std::uint32_t>& src, std::uint64_t& lastHash);
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
