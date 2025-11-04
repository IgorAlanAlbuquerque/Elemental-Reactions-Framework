#include "ERF_UI.h"

void __stdcall ERF_UI::DrawGeneral() {
    ImGui::TextUnformatted("Elemental Reactions Framework");
    ImGui::Separator();

    bool enabled = ERF::GetConfig().enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Enable framework", &enabled)) {
        ERF::GetConfig().enabled.store(enabled, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }

    ImGui::SameLine();
    ImGui::TextDisabled(enabled ? "(enabled)" : "(disabled)");

    ImGui::Separator();

    if (bool hud = ERF::GetConfig().hudEnabled.load(std::memory_order_relaxed); ImGui::Checkbox("Show HUD", &hud)) {
        ERF::GetConfig().hudEnabled.store(hud, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }

    int modeIndex = ERF::GetConfig().isSingle.load(std::memory_order_relaxed) ? 0 : 1;
    const char* kModes[] = {"Single", "Mixed"};
    auto count = (int)(sizeof(kModes) / sizeof(kModes[0]));
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Gauge mode", &modeIndex, kModes, count)) {
        ERF::GetConfig().isSingle.store(modeIndex == 0, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Single: An accumulator for each element.\n"
            "Mixed: Combines different elements into the same accumulator.");
    }
}

void ERF_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Elemental Reactions");

    SKSEMenuFramework::AddSectionItem("General", ERF_UI::DrawGeneral);
}
