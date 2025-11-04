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
        SI_Error rc = ini.LoadFile(path.string().c_str());
        if (rc < 0) {
            spdlog::info("[ERF] Config::Load: usando defaults (sem ini em {})", path.string());
            return;
        }
        bool en = loadBool(ini, "General", "Enabled", true);
        enabled.store(en, std::memory_order_relaxed);
        spdlog::info("[ERF] Config::Load: Enabled={}", en);
    }

    void Config::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());
        ini.SetBoolValue("General", "Enabled", enabled.load(std::memory_order_relaxed));
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
        spdlog::info("[ERF] Config::Save: wrote {}", path.string());
    }

    Config& GetConfig() {
        static Config g_cfg{};
        return g_cfg;
    }
}
