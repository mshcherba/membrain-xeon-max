#include "topology_config.h"
#include <cstdlib>

namespace membrain::topology {

static int g_ddrNode = 0;
static int g_hbmNode = 2;

void initFromEnv() {
    if (const char *envDdr = std::getenv("MEMBRAIN_DDR_NUMA")) {
        g_ddrNode = std::atoi(envDdr);
    }
    if (const char *envHbm = std::getenv("MEMBRAIN_HBM_NUMA")) {
        g_hbmNode = std::atoi(envHbm);
    }
}

int getDdrNode() {
    return g_ddrNode;
}

int getHbmNode() {
    return g_hbmNode;
}

int getNumaNodeForTier(int tierNode) {
    if (tierNode == 2) {
        return g_hbmNode;
    }
    return g_ddrNode;
}

} // namespace membrain::topology
