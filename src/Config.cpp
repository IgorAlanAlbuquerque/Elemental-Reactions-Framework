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

        if (SI_Error rc = ini.LoadFile(path.string().c_str()); rc < 0) {
            return;
        }
        bool en = loadBool(ini, "General", "Enabled", true);
        enabled.store(en, std::memory_order_relaxed);
        bool hud = loadBool(ini, "HUD", "Enabled", true);
        hudEnabled.store(hud, std::memory_order_relaxed);
        bool single = loadBool(ini, "Gauges", "Single", true);
        isSingle.store(single, std::memory_order_relaxed);
    }

    void Config::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        ini.SetBoolValue("General", "Enabled", enabled.load(std::memory_order_relaxed));
        ini.SetBoolValue("HUD", "Enabled", hudEnabled.load(std::memory_order_relaxed));
        ini.SetBoolValue("Gauges", "Single", isSingle.load(std::memory_order_relaxed));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    Config& GetConfig() {
        static Config g_cfg{};
        return g_cfg;
    }
}
