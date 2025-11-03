#include "ERF_UI.h"

void __stdcall ERF_UI::DrawGeneral() {
    ImGui::TextUnformatted("Elemental Reactions Framework");
    ImGui::Separator();
    ImGui::TextUnformatted("Hello from ERF! (menu de teste)");
}

void ERF_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Elemental Reactions");

    SKSEMenuFramework::AddSectionItem("General", ERF_UI::DrawGeneral);
}
