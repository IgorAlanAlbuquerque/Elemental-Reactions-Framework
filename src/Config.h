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
        std::atomic<int> maxReactionsPerTrigger{1};

        std::atomic<float> playerXPosition{0.0};
        std::atomic<float> playerYPosition{0.0};
        std::atomic<float> npcXPosition{0.0};
        std::atomic<float> npcYPosition{0.0};
        std::atomic<float> playerScale{1.0};
        std::atomic<float> npcScale{1.0};
        std::atomic<bool> playerHorizontal{true};
        std::atomic<bool> npcHorizontal{true};
        std::atomic<float> playerSpacing{40.0};
        std::atomic<float> npcSpacing{40.0};

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    Config& GetConfig();
}
