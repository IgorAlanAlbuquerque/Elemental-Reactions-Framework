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
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Player gauge multiplier", &pm, 0.1f, 1.0f, "%.3f")) {
        if (pm < 0.f) pm = 0.f;
        ERF::GetConfig().playerMult.store(pm, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the player's gauge gain/loss. E.g.: 2.0 = doubles, 0.5 = halves.");

    float nm = ERF::GetConfig().npcMult.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("NPC gauge multiplier", &nm, 0.1f, 1.0f, "%.3f")) {
        if (nm < 0.f) nm = 0.f;
        ERF::GetConfig().npcMult.store(nm, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the NPCs' gauge gain/loss.");
}

void __stdcall ERF_UI::DrawHUD() {
    ImGui::TextUnformatted("HUD Settings");
    ImGui::Separator();

    ImGui::TextUnformatted("Player");
    ImGui::Separator();

    float x = ERF::GetConfig().playerXPosition.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("X Position (px)##player", &x, 1.0f, 10.0f, "%.2f")) {
        ERF::GetConfig().playerXPosition.store(x, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Positive values move the gauge to the right. Negative values move it to the left.");

    float y = ERF::GetConfig().playerYPosition.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Y Position (px)##player", &y, 1.0f, 10.0f, "%.2f")) {
        ERF::GetConfig().playerYPosition.store(y, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Positive values move the gauge up. Negative values move it down.");

    float sc = ERF::GetConfig().playerScale.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Scale##player", &sc, 0.05f, 0.25f, "%.3f")) {
        if (sc < 0.f) sc = 0.f;
        ERF::GetConfig().playerScale.store(sc, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gauge size.");

    bool horiz = ERF::GetConfig().playerHorizontal.load(std::memory_order_relaxed);
    int idx = horiz ? 0 : 1;
    const char* opts[] = {"horizontally", "vertically"};
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Align gauges:##player", &idx, opts, (int)(sizeof(opts) / sizeof(opts[0])))) {
        ERF::GetConfig().playerHorizontal.store(idx == 0, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }

    float psp = ERF::GetConfig().playerSpacing.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Spacing (px)##player", &psp, 1.0f, 5.0f, "%.1f")) {
        if (psp < 0.f) psp = 0.f;
        ERF::GetConfig().playerSpacing.store(psp, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance between the player's gauges in pixels.");
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextUnformatted("NPC");
    ImGui::Separator();

    float nx = ERF::GetConfig().npcXPosition.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("X Position (px)##npc", &nx, 1.0f, 10.0f, "%.2f")) {
        ERF::GetConfig().npcXPosition.store(nx, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Positive values move the gauge to the right. Negative values move it to the left.");

    float ny = ERF::GetConfig().npcYPosition.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Y Position (px)##npc", &ny, 1.0f, 10.0f, "%.2f")) {
        ERF::GetConfig().npcYPosition.store(ny, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Positive values move the gauge up. Negative values move it down.");

    float nsc = ERF::GetConfig().npcScale.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Scale##npc", &nsc, 0.05f, 0.25f, "%.3f")) {
        if (nsc < 0.f) nsc = 0.f;
        ERF::GetConfig().npcScale.store(nsc, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gauge size.");

    bool nhoriz = ERF::GetConfig().npcHorizontal.load(std::memory_order_relaxed);
    int nidx = nhoriz ? 0 : 1;
    const char* nopts[] = {"horizontally", "vertically"};
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Align gauges:##npc", &nidx, nopts, (int)(sizeof(nopts) / sizeof(nopts[0])))) {
        ERF::GetConfig().npcHorizontal.store(nidx == 0, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }

    float nsp = ERF::GetConfig().npcSpacing.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputFloat("Spacing (px)##npc", &nsp, 1.0f, 5.0f, "%.1f")) {
        if (nsp < 0.f) nsp = 0.f;
        ERF::GetConfig().npcSpacing.store(nsp, std::memory_order_relaxed);
        ERF::GetConfig().Save();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance between the npc's gauges in pixels.");
    }
}

void ERF_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Elemental Reactions");

    SKSEMenuFramework::AddSectionItem("General", ERF_UI::DrawGeneral);
    SKSEMenuFramework::AddSectionItem("HUD", ERF_UI::DrawHUD);
}
