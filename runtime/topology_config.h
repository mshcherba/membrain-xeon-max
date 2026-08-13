#ifndef MEMBRAIN_TOPOLOGY_CONFIG_H
#define MEMBRAIN_TOPOLOGY_CONFIG_H

namespace membrain::topology {

// Initializes memory topology settings from environment variables
void initFromEnv();

// Returns configured DDR NUMA node ID (default 0)
int getDdrNode();

// Returns configured HBM NUMA node ID (default 2)
int getHbmNode();

// Resolves target NUMA node for a given memory tier
int getNumaNodeForTier(int tierNode);

} // namespace membrain::topology

#endif // MEMBRAIN_TOPOLOGY_CONFIG_H
