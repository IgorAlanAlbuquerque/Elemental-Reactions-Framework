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

    extern std::unordered_map<RE::FormID, ComboHUD> combos;

    class SMSOWidget : public TRUEHUD_API::WidgetBase {
    public:
        SMSOWidget() = default;

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
    };

    extern std::unordered_map<RE::FormID, std::shared_ptr<SMSOWidget>> widgets;

    void BeginCombo(RE::Actor* a, ElementalGauges::Combo which, float seconds, bool realTime);
    void AddFor(RE::Actor* actor);
    void UpdateFor(RE::Actor* actor);
    bool RemoveFor(RE::FormID id);

    void RemoveAllWidgets();
    void OnTrueHUDOpen();
    void OnTrueHUDClose();
}
