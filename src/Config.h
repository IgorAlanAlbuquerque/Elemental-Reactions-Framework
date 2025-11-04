#pragma once
#include <atomic>
#include <filesystem>

namespace ERF {
    struct Config {
        std::atomic<bool> enabled{true};
        std::atomic<bool> hudEnabled{true};
        std::atomic<bool> isSingle{true};
        std::atomic<float> playerMult{1.0};
        std::atomic<float> npcMult{1.0};

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    Config& GetConfig();
}
