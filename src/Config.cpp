#include "Config.h"

#include <SKSE/SKSE.h>
#include <SimpleIni.h>

#include <cctype>
#include <string>

namespace ERF {
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

    std::filesystem::path Config::IniPath() {
        auto dirOpt = SKSE::log::log_directory();
        if (!dirOpt) {
            return std::filesystem::path("ElementalReactionsFramework.ini");
        }
        auto p = *dirOpt / "ElementalReactionsFramework.ini";
        return p;
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

        enabled.store(en, std::memory_order_relaxed);
        hudEnabled.store(hud, std::memory_order_relaxed);
        isSingle.store(sgl, std::memory_order_relaxed);
        playerMult.store(static_cast<float>(pm < 0 ? 0 : pm), std::memory_order_relaxed);
        npcMult.store(static_cast<float>(nm < 0 ? 0 : nm), std::memory_order_relaxed);
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

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    Config& GetConfig() {
        static Config g_cfg{};
        return g_cfg;
    }
}
