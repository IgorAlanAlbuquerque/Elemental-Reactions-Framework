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

    bool hud = ERF::GetConfig().hudEnabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Show HUD", &hud)) {
        ERF::GetConfig().hudEnabled.store(hud, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
}

void ERF_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Elemental Reactions");

    SKSEMenuFramework::AddSectionItem("General", ERF_UI::DrawGeneral);
}
