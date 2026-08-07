#include "membrain_rt.h"
#include <numaif.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

static std::atomic<uint64_t> g_totalAllocations{0};
static std::atomic<uint64_t> g_totalBytesAllocated{0};
static std::atomic<uint64_t> g_hbmAllocations{0};
static std::atomic<uint64_t> g_ddrAllocations{0};

static bool g_verbose = false;
static std::once_flag g_initFlag;
static std::unordered_map<uint32_t, int> g_siteTierMap; // site_id -> NUMA node (0: DDR5, 2: HBM2e)

void parseGuidanceFile() {
    std::ifstream file("site_tier_guidance.json");
    if (!file.is_open()) {
        if (g_verbose) {
            std::cout << "[MemBrainRT] site_tier_guidance.json not found. Defaulting all allocations to DDR5 (NUMA 0)\n";
        }
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t idPos = line.find("\"site_id\":");
        size_t tierPos = line.find("\"tier\":");
        if (idPos != std::string::npos && tierPos != std::string::npos) {
            uint32_t siteId = std::stoul(line.substr(idPos + 10));
            int tierNode = std::stoi(line.substr(tierPos + 7));
            g_siteTierMap[siteId] = tierNode;
        }
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Loaded guidance for " << g_siteTierMap.size() << " allocation sites.\n";
    }
}

void initRuntime() {
    if (const char *env = std::getenv("MEMBRAIN_VERBOSE")) {
        g_verbose = (env[0] == '1' || env[0] == 'y');
    }

    parseGuidanceFile();

    if (g_verbose) {
        std::cout << "[MemBrainRT] Initialized for Single-Socket Xeon Max (DDR5: NUMA 0, HBM2e: NUMA 2)\n";
    }
}

int getTargetNode(uint32_t site_id) {
    auto it = g_siteTierMap.find(site_id);
    if (it != g_siteTierMap.end()) {
        return it->second;
    }
    return 0; // Default to DDR5 (NUMA 0)
}

void bindMemoryToNode(void *ptr, size_t size, int node) {
    if (!ptr || size == 0) return;
    unsigned long nodemask = (1UL << node);
    // Bind memory range to target NUMA node using MPOL_PREFERRED policy
    mbind(ptr, size, MPOL_PREFERRED, &nodemask, sizeof(nodemask) * 8, 0);
}

} // namespace

extern "C" {

void membrain_init(void) {
    std::call_once(g_initFlag, initRuntime);
}

void *membrain_alloc(size_t size, uint32_t site_id) {
    membrain_init();

    if (size == 0) return nullptr;

    void *ptr = std::malloc(size);
    if (!ptr) return nullptr;

    int targetNode = getTargetNode(site_id);
    bindMemoryToNode(ptr, size, targetNode);

    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
    g_totalBytesAllocated.fetch_add(size, std::memory_order_relaxed);
    if (targetNode == 2) {
        g_hbmAllocations.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_ddrAllocations.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Site ID " << site_id << " -> Allocated " << size
                  << " bytes on NUMA Node " << targetNode << " ("
                  << (targetNode == 2 ? "HBM2e" : "DDR5") << ")\n";
    }

    return ptr;
}

void membrain_free(void *ptr) {
    if (ptr) {
        std::free(ptr);
    }
}

} // extern "C"
