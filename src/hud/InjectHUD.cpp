#include "InjectHUD.h"

#include <memory>

#include "../elemental_reactions/ElementalGauges.h"
#include "SKSE/SKSE.h"
#include "TrueHUDAPI.h"

namespace InjectHUD {
    TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
    SKSE::PluginHandle g_pluginHandle = static_cast<SKSE::PluginHandle>(-1);
    std::unordered_map<RE::FormID, std::shared_ptr<SMSOWidget>> widgets;
}

void InjectHUD::AddFor(RE::Actor* actor) {
    spdlog::info("entoru no add");
    if (!g_trueHUD || !actor) return;

    auto formId = actor->GetFormID();
    if (auto it = widgets.find(formId); it == widgets.end()) {
        auto widget = std::make_shared<SMSOWidget>();
        g_trueHUD->AddWidget(g_pluginHandle, SMSO_WIDGET_TYPE, formId, SMSO_SYMBOL_NAME, widget);
        spdlog::info("[SMSO] AddWidget: actor ID {:08X}", formId);

        widgets[formId] = widget;
    }
    return;
}

void InjectHUD::UpdateFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) return;
    auto formId = actor->GetFormID();

    auto it = widgets.find(formId);
    if (it == widgets.end() || !it->second) {
        spdlog::info("[SMSO] Widget {:08X} ainda não existe; aguardando...", formId);
        return;
    }
    if (!it->second->_view) {
        spdlog::info("[SMSO] Widget {:08X} ainda sem view; aguardando...", formId);
        return;
    }

    auto iconOpt = ElementalGauges::PickHudIconDecayed(formId);
    auto totalsOpt = ElementalGauges::GetTotalsDecayed(formId);
    if (!iconOpt || !totalsOpt) return;

    const auto& icon = *iconOpt;
    const auto& totals = *totalsOpt;
    it->second->SetIconAndGauge(icon.id, totals.fire, totals.frost, totals.shock, icon.tintRGB);
}

void InjectHUD::RemoveFor(RE::FormID id) {
    spdlog::info("remove icon");
    if (!g_trueHUD || !id) return;

    auto it = widgets.find(id);
    if (it != widgets.end()) {
        g_trueHUD->RemoveWidget(g_pluginHandle, SMSO_WIDGET_TYPE, id, TRUEHUD_API::WidgetRemovalMode::Immediate);
        widgets.erase(it);
    }
}
