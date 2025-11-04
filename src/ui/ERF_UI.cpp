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

    ImGui::Separator();

    float pm = ERF::GetConfig().playerMult.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputFloat("Player gauge multiplier", &pm, 0.1f, 1.0f, "%.3f")) {
        if (pm < 0.f) pm = 0.f;
        ERF::GetConfig().playerMult.store(pm, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the player's gauge gain/loss. E.g.: 2.0 = doubles, 0.5 = halves.");

    float nm = ERF::GetConfig().npcMult.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputFloat("NPC gauge multiplier", &nm, 0.1f, 1.0f, "%.3f")) {
        if (nm < 0.f) nm = 0.f;
        ERF::GetConfig().npcMult.store(nm, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the NPCs' gauge gain/loss.");
}

void ERF_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Elemental Reactions");

    SKSEMenuFramework::AddSectionItem("General", ERF_UI::DrawGeneral);
}
