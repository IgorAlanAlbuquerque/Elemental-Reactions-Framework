#include "Overrides.h"

#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/S/SpellItem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

#include "RE/T/TESForm.h"

namespace {
    using json = nlohmann::json;
    RE::SpellItem* findByPluginAndBaseID(std::string_view plugin, std::uint32_t baseID) {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return nullptr;
        RE::SpellItem* best = nullptr;
        int bestLO = -1;

        auto getLO = [&](const RE::TESFile* f) -> int {
            if (!f) return -1;
            int i = 0;
            for (auto* file : dh->files) {
                if (file == f) return i;
                ++i;
            }
            return -1;
        };

        for (auto* sp : dh->GetFormArray<RE::SpellItem>()) {
            if (!sp) continue;
            const auto* owner = sp->GetDescriptionOwnerFile();
            if (!owner) continue;

            if (_stricmp(std::string(owner->GetFilename()).c_str(), std::string(plugin).c_str()) != 0) continue;

            if ((sp->formID & 0x00FFFFFF) != (baseID & 0x00FFFFFF)) continue;

            const int lo = getLO(owner);
            if (lo > bestLO) {
                bestLO = lo;
                best = sp;
            }
        }
        return best;
    }

    bool parseBaseID(const json& j, std::uint32_t& out) {
        if (j.is_string()) {
            std::string s = j.get<std::string>();
            if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s.erase(0, 2);
            if (s.size() > 8) return false;
            out = static_cast<std::uint32_t>(std::stoul(s, nullptr, 16));
            return true;
        } else if (j.is_number_unsigned()) {
            out = j.get<std::uint32_t>();
            return true;
        }
        return false;
    }
}

bool SpellKey::operator==(const SpellKey& o) const noexcept {
    return formID == o.formID && _stricmp(plugin.c_str(), o.plugin.c_str()) == 0;
}
size_t SpellKeyHash::operator()(const SpellKey& k) const noexcept {
    std::string p = k.plugin;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    return std::hash<uint32_t>{}(k.formID) ^ (std::hash<std::string>{}(p) << 1);
}

const std::filesystem::path& ERF_GetThisDllDir();

static RE::EffectSetting* s_mgefGauge = nullptr;
static std::unordered_set<RE::BGSKeyword*> s_erfKeywords;

bool ERF::Overrides::InitResources() { return (s_mgefGauge != nullptr); }

void ERF::Overrides::SetGaugeEffect(RE::EffectSetting* mgef) { s_mgefGauge = mgef; }

bool ERF::Overrides::HasERFKeyword(const RE::EffectSetting* mgef) {
    if (!mgef) return false;
    for (auto* kw : mgef->GetKeywords()) {
        if (!kw) continue;
        if (ElementRegistry::get().findByKeyword(kw).has_value()) return true;
    }
    return false;
}

RE::Effect* ERF::Overrides::FindGaugeEffect(RE::SpellItem* sp) {
    if (!sp) return nullptr;

    for (RE::Effect* ei : sp->effects) {
        if (ei && ei->baseEffect == s_mgefGauge) {
            return ei;
        }
    }
    return nullptr;
}

void ERF::Overrides::EnsureGaugeEffect(RE::SpellItem* sp, float defaultMag) {
    if (!sp || !s_mgefGauge) return;
    if (FindGaugeEffect(sp)) return;

    RE::Effect* eff = new RE::Effect();
    eff->baseEffect = s_mgefGauge;
    eff->effectItem.magnitude = defaultMag;
    eff->effectItem.area = 0;
    eff->effectItem.duration = 0;
    eff->cost = 0;

    sp->effects.push_back(eff);
}

void ERF::Overrides::SetGaugeMagnitude(RE::SpellItem* sp, float mag) {
    if (RE::Effect* ei = FindGaugeEffect(sp)) {
        if (std::fabs(ei->effectItem.magnitude - mag) > 1e-4f) {
            ei->effectItem.magnitude = mag;
        }
    }
}

std::vector<RE::SpellItem*> ERF::Overrides::ScanAllSpellsWithKeyword() {
    std::vector<RE::SpellItem*> out;

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto& arr = dh->GetFormArray<RE::SpellItem>();

    auto getLoadOrderIndex = [&](const RE::TESFile* f) -> int {
        if (!f) return -1;
        int idx = 0;
        for (auto* file : dh->files) {
            if (file == f) return idx;
            ++idx;
        }
        return -1;
    };

    std::unordered_map<std::uint32_t, RE::SpellItem*> best;
    best.reserve(arr.size() / 3 + 16);

    auto keep = [&](RE::SpellItem* sp) {
        std::uint32_t key = sp->formID & 0x00FFFFFF;
        const int lo = getLoadOrderIndex(sp->GetDescriptionOwnerFile());

        auto it = best.find(key);
        if (it == best.end()) {
            best.emplace(key, sp);
        } else {
            RE::SpellItem* cur = it->second;
            const int loCur = getLoadOrderIndex(cur->GetDescriptionOwnerFile());
            if (lo > loCur) {
                it->second = sp;
            }
        }
    };

    for (auto* sp : arr) {
        if (!sp || sp->effects.empty()) continue;

        bool eligible = false;
        for (auto& ei : sp->effects) {
            if (ei && HasERFKeyword(ei->baseEffect)) {
                eligible = true;
                break;
            }
        }

        if (eligible) {
            keep(sp);
        }
    }

    out.reserve(best.size());
    for (auto& [key, sp] : best) {
        out.push_back(sp);
    }
    return out;
}

std::string ERF::Overrides::GetEditorID(const RE::TESForm* f) {
    if (!f) return {};
    if (const char* ed = f->GetFormEditorID(); ed && ed[0]) return ed;
    return {};
}

std::string ERF::Overrides::GetDisplayName(const RE::TESForm* f) {
    if (!f) return {};

    const char* name = f->GetName();
    return name ? std::string(name) : std::string();
}

std::string ERF::Overrides::FormIDHex(std::uint32_t id) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", id);
    return buf;
}

static std::filesystem::path ComputeOverridesPath() {
    const auto& dllDir = ERF_GetThisDllDir();
    if (!dllDir.empty()) {
        return dllDir / "ERF" / "spell_overrides.json";
    }

    if (auto logDirOpt = SKSE::log::log_directory()) {
        return *logDirOpt / "ERF" / "spell_overrides.json";
    }

    return std::filesystem::path("Data") / "SKSE" / "Plugins" / "ERF" / "spell_overrides.json";
}

const std::filesystem::path& ERF::Overrides::OverridesPath() {
    static const std::filesystem::path kCached = ComputeOverridesPath();
    return kCached;
}

bool ERF::Overrides::EnsureOverridesFolder() {
    std::error_code ec;
    const auto dir = OverridesPath().parent_path();
    if (dir.empty()) return false;
    return std::filesystem::exists(dir) || std::filesystem::create_directories(dir, ec);
}

std::string_view ERF::Overrides::OwningPlugin(const RE::TESForm* f) {
    if (!f) return {};
    if (auto* file = f->GetDescriptionOwnerFile()) {
        if (std::string_view name = file->GetFilename(); !name.empty()) return name;
    }
    return {};
}

std::uint32_t ERF::Overrides::RawFormID(const RE::TESForm* f) {
    if (!f) return 0;
    return f->formID;
}

std::size_t ERF::Overrides::ApplyOverridesFromJSON() {
    const auto& path = OverridesPath();
    std::ifstream in(path);
    if (!in.good()) {
        spdlog::info("[ERF][Overrides] JSON não encontrado: {}", path.string());
        return 0;
    }

    json root;
    try {
        root = json::parse(in);
    } catch (const std::exception& e) {
        spdlog::error("[ERF][Overrides] Parse falhou em {}: {}", path.string(), e.what());
        return 0;
    }

    auto applyArray = [&](const json& arr) -> std::size_t {
        if (!arr.is_array()) return 0;
        std::size_t applied = 0;
        for (const auto& e : arr) {
            try {
                const auto& jpl = e.at("plugin");
                const auto& jid = e.at("formID");
                const auto& jmag = e.at("magnitude");

                std::string plugin = jpl.get<std::string>();
                std::uint32_t baseID{};
                if (!parseBaseID(jid, baseID)) {
                    spdlog::warn("[ERF][Overrides] formID inválido ({}).", jid.dump());
                    continue;
                }
                float mag = jmag.get<float>();
                if (mag < 0.f) mag = 0.f;

                if (auto* sp = findByPluginAndBaseID(plugin, baseID)) {
                    EnsureGaugeEffect(sp, mag);
                    SetGaugeMagnitude(sp, mag);
                    ++applied;
                } else {
                    spdlog::warn("[ERF][Overrides] Spell não encontrada: {} {:06X}", plugin, baseID & 0x00FFFFFF);
                }
            } catch (const std::exception& ex) {
                spdlog::warn("[ERF][Overrides] Entrada inválida: {}", ex.what());
            }
        }
        return applied;
    };

    std::size_t total = 0;
    if (root.is_object() && root.contains("spells")) {
        total += applyArray(root["spells"]);
    } else if (root.is_array()) {
        total += applyArray(root);
    } else {
        spdlog::warn("[ERF][Overrides] Formato inesperado (esperado array ou objeto com 'spells').");
    }
    return total;
}