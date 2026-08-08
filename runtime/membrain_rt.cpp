#include "membrain_rt.h"
#include <numaif.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
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
static bool g_trace = false;
static std::once_flag g_initFlag;
static std::unordered_map<uint32_t, int> g_siteTierMap; // site_id -> NUMA node (0: DDR5, 2: HBM2e)
static std::ofstream g_traceFile;
static std::mutex g_traceMutex;

void logAllocationTrace(void *ptr, size_t size, uint32_t site_id) {
    if (!g_trace || !ptr) return;
    std::lock_guard<std::mutex> lock(g_traceMutex);
    if (!g_traceFile.is_open()) {
        g_traceFile.open("alloc_trace.txt", std::ios::out | std::ios::app);
    }
    if (g_traceFile.is_open()) {
        uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t end = start + size;
        g_traceFile << site_id << " " << start << " " << end << " " << size << "\n";
        g_traceFile.flush();
    }
}

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
    if (const char *envT = std::getenv("MEMBRAIN_TRACE")) {
        g_trace = (envT[0] == '1' || envT[0] == 'y');
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
    int res = mbind(ptr, size, MPOL_PREFERRED, &nodemask, sizeof(nodemask) * 8, 0);
    if (res != 0 && g_verbose) {
        std::cerr << "[MemBrainRT] Warning: mbind failed for ptr " << ptr
                  << " size " << size << " on node " << node
                  << " (errno: " << errno << ")\n";
    }
}

} // namespace

extern "C" {

void membrain_init(void) {
    std::call_once(g_initFlag, initRuntime);
}

void *membrain_alloc(size_t size, uint32_t site_id) {
    membrain_init();

    if (size == 0) return nullptr;

    static const size_t pageSize = sysconf(_SC_PAGESIZE);
    size_t alignedSize = (size + pageSize - 1) & ~(pageSize - 1);

    void *ptr = nullptr;
    if (posix_memalign(&ptr, pageSize, alignedSize) != 0) {
        return nullptr;
    }

    int targetNode = getTargetNode(site_id);
    bindMemoryToNode(ptr, alignedSize, targetNode);

    if (g_trace) {
        logAllocationTrace(ptr, alignedSize, site_id);
    }

    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
    g_totalBytesAllocated.fetch_add(size, std::memory_order_relaxed);
    if (targetNode == 2) {
        g_hbmAllocations.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_ddrAllocations.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Site ID " << site_id << " -> Allocated " << size
                  << " bytes (aligned: " << alignedSize
                  << " bytes) on NUMA Node " << targetNode << " ("
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
