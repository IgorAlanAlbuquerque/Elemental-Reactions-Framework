#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RE/Skyrim.h"

struct SpellKey {
    std::string plugin;
    std::uint32_t formID{};
    bool operator==(const SpellKey& o) const noexcept;
};
struct SpellKeyHash {
    size_t operator()(const SpellKey& k) const noexcept;
};

namespace ERF::Overrides {

    const std::filesystem::path& OverridesPath();
    bool EnsureOverridesFolder();

    bool InitResources();

    void SetGaugeEffect(RE::EffectSetting* mgef);

    bool HasERFKeyword(const RE::EffectSetting* mgef);
    RE::Effect* FindGaugeEffect(RE::SpellItem* sp);

    void EnsureGaugeEffect(RE::SpellItem* sp, float defaultMag);
    void SetGaugeMagnitude(RE::SpellItem* sp, float mag);

    std::vector<RE::SpellItem*> ScanAllSpellsWithKeyword();

    std::string GetEditorID(const RE::TESForm* f);
    std::string GetDisplayName(const RE::TESForm* f);
    std::string FormIDHex(std::uint32_t id);
    std::string_view OwningPlugin(const RE::TESForm* f);
    std::uint32_t RawFormID(const RE::TESForm* f);
    std::size_t ApplyOverridesFromJSON();
}
