#include "Config.h"

#include <SKSE/SKSE.h>
#include <SimpleIni.h>

#include <cctype>
#include <string>

namespace {
    static bool loadBool(CSimpleIniA& ini, const char* sec, const char* key, bool defVal) {
        const char* val = ini.GetValue(sec, key, nullptr);
        if (!val) return defVal;
        std::string s = val;
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        if (s == "1" || s == "true" || s == "on") return true;
        if (s == "0" || s == "false" || s == "off") return false;
        return defVal;
    }

    static double loadDouble(CSimpleIniA& ini, const char* sec, const char* key, double defVal) {
        const char* v = ini.GetValue(sec, key, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        double d = std::strtod(v, &end);
        return (end && *end == '\0') ? d : defVal;
    }
}

const std::filesystem::path& ERF_GetThisDllDir();

namespace ERF {

    std::filesystem::path Config::IniPath() {
        const auto& dllDir = ERF_GetThisDllDir();
        if (!dllDir.empty()) {
            return dllDir / "ERF" / "ElementalReactionsFramework.ini";
        }

        if (auto dirOpt = SKSE::log::log_directory()) {
            return *dirOpt / "ERF" / "ElementalReactionsFramework.ini";
        }

        return std::filesystem::path("Data") / "SKSE" / "Plugins" / "ERF" / "ElementalReactionsFramework.ini";
    }

    void Config::Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();

        if (SI_Error rc = ini.LoadFile(path.string().c_str()); rc < 0) return;

        bool en = loadBool(ini, "General", "Enabled", true);
        bool hud = loadBool(ini, "HUD", "Enabled", true);
        bool sgl = loadBool(ini, "Gauges", "Single", true);
        double pm = loadDouble(ini, "Gauges", "PlayerMult", 1.0);
        double nm = loadDouble(ini, "Gauges", "NpcMult", 1.0);
        double px = loadDouble(ini, "HUD", "PlayerXPosition", 0.0);
        double py = loadDouble(ini, "HUD", "PlayerYPosition", 0.0);
        double nx = loadDouble(ini, "HUD", "NpcXPosition", 0.0);
        double ny = loadDouble(ini, "HUD", "NpcYPosition", 0.0);
        double psc = loadDouble(ini, "HUD", "PlayerScale", 1.0);
        double nsc = loadDouble(ini, "HUD", "NpcScale", 1.0);
        bool ph = loadBool(ini, "HUD", "PlayerHorizontal", true);
        bool nh = loadBool(ini, "HUD", "NpcHorizontal", true);
        double psp = loadDouble(ini, "HUD", "PlayerSpacing", 40.0);
        double nsp = loadDouble(ini, "HUD", "NpcSpacing", 40.0);

        enabled.store(en, std::memory_order_relaxed);
        hudEnabled.store(hud, std::memory_order_relaxed);
        isSingle.store(sgl, std::memory_order_relaxed);
        playerMult.store(static_cast<float>(pm < 0 ? 0 : pm), std::memory_order_relaxed);
        npcMult.store(static_cast<float>(nm < 0 ? 0 : nm), std::memory_order_relaxed);
        playerXPosition.store(static_cast<float>(px), std::memory_order_relaxed);
        playerYPosition.store(static_cast<float>(py), std::memory_order_relaxed);
        npcXPosition.store(static_cast<float>(nx), std::memory_order_relaxed);
        npcYPosition.store(static_cast<float>(ny), std::memory_order_relaxed);
        playerScale.store(static_cast<float>(psc > 0 ? psc : 1.0), std::memory_order_relaxed);
        npcScale.store(static_cast<float>(nsc > 0 ? nsc : 1.0), std::memory_order_relaxed);
        playerHorizontal.store(ph, std::memory_order_relaxed);
        npcHorizontal.store(nh, std::memory_order_relaxed);
        playerSpacing.store(static_cast<float>(psp < 0 ? 0 : psp), std::memory_order_relaxed);
        npcSpacing.store(static_cast<float>(nsp < 0 ? 0 : nsp), std::memory_order_relaxed);
    }

    void Config::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        ini.SetBoolValue("General", "Enabled", enabled.load(std::memory_order_relaxed));
        ini.SetBoolValue("HUD", "Enabled", hudEnabled.load(std::memory_order_relaxed));
        ini.SetBoolValue("Gauges", "Single", isSingle.load(std::memory_order_relaxed));
        ini.SetDoubleValue("Gauges", "PlayerMult", playerMult.load(std::memory_order_relaxed));
        ini.SetDoubleValue("Gauges", "NpcMult", npcMult.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "PlayerXPosition", playerXPosition.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "PlayerYPosition", playerYPosition.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "NpcXPosition", npcXPosition.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "NpcYPosition", npcYPosition.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "PlayerScale", playerScale.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "NpcScale", npcScale.load(std::memory_order_relaxed));
        ini.SetBoolValue("HUD", "PlayerHorizontal", playerHorizontal.load(std::memory_order_relaxed));
        ini.SetBoolValue("HUD", "NpcHorizontal", npcHorizontal.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "PlayerSpacing", playerSpacing.load(std::memory_order_relaxed));
        ini.SetDoubleValue("HUD", "NpcSpacing", npcSpacing.load(std::memory_order_relaxed));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    Config& GetConfig() {
        static Config g_cfg{};
        return g_cfg;
    }
}
