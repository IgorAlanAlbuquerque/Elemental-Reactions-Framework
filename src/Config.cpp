#include "Config.h"

namespace ERF {
    Config& GetConfig() {
        static Config g_cfg{};
        return g_cfg;
    }
}
