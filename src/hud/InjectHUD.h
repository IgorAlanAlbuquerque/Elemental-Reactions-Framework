#pragma once

#include <array>
#include <unordered_map>

#include "SKSE/SKSE.h"
#include "TrueHUDAPI.h"

namespace InjectHUD {
    constexpr auto SMSO_SWF_PATH = "smsogauge.swf";
    constexpr auto SMSO_SYMBOL_NAME = "SMSO_Gauge";
    constexpr uint32_t SMSO_WIDGET_TYPE = 'SMSO';  // 0x534D534F
    extern TRUEHUD_API::IVTrueHUD4* g_trueHUD;
    extern SKSE::PluginHandle g_pluginHandle;

    class SMSOWidget : public TRUEHUD_API::WidgetBase {
    public:
        SMSOWidget() = default;

        void Initialize() override {
            spdlog::info("[SMSO] SMSOWidget::Initialize()");
            if (!_view) return;

            RE::GFxValue x, y, s, vis, alpha;
            x.SetNumber(0.0);
            _object.SetMember("_x", x);
            y.SetNumber(-80.0);
            _object.SetMember("_y", y);  // mais alto que -22
            s.SetNumber(140.0);
            _object.SetMember("_xscale", s);
            _object.SetMember("_yscale", s);
            alpha.SetNumber(100.0);
            _object.SetMember("_alpha", alpha);

            vis.SetBoolean(true);  // deixe visível desde o init para testar
            _object.SetMember("_visible", vis);

            spdlog::info("initialize finalizado");
        }

        void Update(float) override {}
        void Dispose() override {}

        void SetIconAndGauge(uint32_t iconId, uint32_t fire, uint32_t frost, uint32_t shock, uint32_t tintRGB) {
            spdlog::info("[SMSO] SetIconAndGauge(icon={}, F={}, R={}, S={})", iconId, fire, frost, shock);
            if (!_view) return;
            spdlog::info("view instanciado");

            // Aguarda a timeline do símbolo estar pronta (children resolvidos)
            RE::GFxValue ready;
            if (!_object.Invoke("isReady", &ready, nullptr, 0)) {
                spdlog::info("[SMSO] Falha ao invocar isReady()");
                return;
            }
            if (!ready.IsBool()) {
                spdlog::info("[SMSO] isReady() não retornou booleano");
                return;
            }
            if (!ready.GetBool()) {
                spdlog::info("[SMSO] isReady() retornou false");
                return;
            }
            spdlog::info("Icone pronto");

            const bool any = (fire + frost + shock) > 0;

            RE::GFxValue vis;
            vis.SetBoolean(any);
            _object.SetMember("_visible", vis);
            spdlog::info("gauge maior que zero: {}", any);

            // === NEW: logar presença dos métodos e retorno ===
            RE::GFxValue acc[3];
            acc[0].SetNumber(static_cast<double>(fire));
            acc[1].SetNumber(static_cast<double>(frost));
            acc[2].SetNumber(static_cast<double>(shock));
            bool ok1 = _object.Invoke("setAccumulators", nullptr, acc, 3);
            spdlog::info("[SMSO] setAccumulators() -> {}", ok1);

            if (!any) return;

            spdlog::info("ativar o icone");
            RE::GFxValue args[2];
            args[0].SetNumber(iconId);
            args[1].SetNumber(tintRGB);
            bool ok2 = _object.Invoke("setIcon", nullptr, args, 2);
            spdlog::info("[SMSO] setIcon() -> {}", ok2);

            // (opcional) confirme a visibilidade após as chamadas
            RE::GFxValue curVis;
            if (_object.GetMember("_visible", &curVis) && curVis.IsBool()) {
                spdlog::info("[SMSO] _visible agora = {}", curVis.GetBool());
            }
        }
    };

    void AddFor(RE::Actor* actor);
    void UpdateFor(RE::Actor* actor);
    void RemoveFor(RE::FormID id);
}
