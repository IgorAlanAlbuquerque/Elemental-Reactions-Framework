#pragma once
#include <atomic>
#include <filesystem>

namespace ERF {
    struct Config {
        std::atomic<bool> enabled{true};

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    Config& GetConfig();
}
