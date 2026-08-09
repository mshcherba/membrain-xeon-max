#include "membrain_rt.h"
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

#include <umf/memory_pool.h>
#include <umf/memory_provider.h>
#include <umf/pools/pool_scalable.h>
#include <umf/providers/provider_os_memory.h>

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

static umf_memory_pool_handle_t g_hbmPool = nullptr;
static umf_memory_pool_handle_t g_ddrPool = nullptr;

void initUmfPools() {
    // 1. Initialize HBM2e OS Memory Provider (NUMA Node 2)
    umf_os_memory_provider_params_handle_t hbmParams = nullptr;
    if (umfOsMemoryProviderParamsCreate(&hbmParams) == UMF_RESULT_SUCCESS) {
        unsigned hbmNodes[] = {2};
        umfOsMemoryProviderParamsSetNumaList(hbmParams, hbmNodes, 1);
        umfOsMemoryProviderParamsSetNumaMode(hbmParams, UMF_NUMA_MODE_PREFERRED);
        umfOsMemoryProviderParamsSetName(hbmParams, "HBM2eProvider");

        umf_memory_provider_handle_t hbmProvider = nullptr;
        if (umfMemoryProviderCreate(umfOsMemoryProviderOps(), hbmParams, &hbmProvider) == UMF_RESULT_SUCCESS) {
            umfPoolCreate(umfScalablePoolOps(), hbmProvider, nullptr, UMF_POOL_CREATE_FLAG_OWN_PROVIDER, &g_hbmPool);
        }
        umfOsMemoryProviderParamsDestroy(hbmParams);
    }

    // 2. Initialize DDR5 OS Memory Provider (NUMA Node 0)
    umf_os_memory_provider_params_handle_t ddrParams = nullptr;
    if (umfOsMemoryProviderParamsCreate(&ddrParams) == UMF_RESULT_SUCCESS) {
        unsigned ddrNodes[] = {0};
        umfOsMemoryProviderParamsSetNumaList(ddrParams, ddrNodes, 1);
        umfOsMemoryProviderParamsSetNumaMode(ddrParams, UMF_NUMA_MODE_PREFERRED);
        umfOsMemoryProviderParamsSetName(ddrParams, "DDR5Provider");

        umf_memory_provider_handle_t ddrProvider = nullptr;
        if (umfMemoryProviderCreate(umfOsMemoryProviderOps(), ddrParams, &ddrProvider) == UMF_RESULT_SUCCESS) {
            umfPoolCreate(umfScalablePoolOps(), ddrProvider, nullptr, UMF_POOL_CREATE_FLAG_OWN_PROVIDER, &g_ddrPool);
        }
        umfOsMemoryProviderParamsDestroy(ddrParams);
    }

    if (!g_hbmPool || !g_ddrPool) {
        std::cerr << "[MemBrainRT] FATAL: Failed to initialize UMF Scalable Memory Pools!\n";
        std::abort();
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Initialized UMF Scalable Memory Pools (HBM2e Pool: OK, DDR5 Pool: OK)\n";
    }
}

__attribute__((destructor)) static void cleanupUmfPools() {
    if (g_hbmPool) {
        umfPoolDestroy(g_hbmPool);
        g_hbmPool = nullptr;
    }
    if (g_ddrPool) {
        umfPoolDestroy(g_ddrPool);
        g_ddrPool = nullptr;
    }
}

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
    const char *envPath = std::getenv("MEMBRAIN_GUIDANCE_PATH");
    std::string path = envPath ? envPath : "site_tier_guidance.json";
    std::ifstream file(path);
    if (!file.is_open()) {
        if (g_verbose) {
            std::cout << "[MemBrainRT] " << path << " not found. Defaulting all allocations to DDR5 (NUMA 0)\n";
        }
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while ((pos = content.find("\"site_id\"", pos)) != std::string::npos) {
        size_t idPos = content.find(":", pos);
        if (idPos == std::string::npos) break;
        uint32_t siteId = std::stoul(content.substr(idPos + 1));
        
        size_t tierKeyPos = content.find("\"tier\"", idPos);
        if (tierKeyPos == std::string::npos) break;
        size_t tierPos = content.find(":", tierKeyPos);
        if (tierPos == std::string::npos) break;
        int tierNode = std::stoi(content.substr(tierPos + 1));
        
        g_siteTierMap[siteId] = tierNode;
        pos = tierPos + 1;
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Loaded guidance for " << g_siteTierMap.size() << " allocation sites from " << path << ".\n";
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
    initUmfPools();

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

void trackAllocation(void *ptr, size_t size, uint32_t site_id, const char *allocName) {
    if (!ptr) return;
    int targetNode = getTargetNode(site_id);

    if (g_trace) {
        logAllocationTrace(ptr, size, site_id);
    }

    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
    g_totalBytesAllocated.fetch_add(size, std::memory_order_relaxed);
    if (targetNode == 2) {
        g_hbmAllocations.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_ddrAllocations.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Site ID " << site_id << " -> " << allocName << " " << size
                  << " bytes on NUMA Node " << targetNode << " ("
                  << (targetNode == 2 ? "HBM2e" : "DDR5") << ")\n";
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

    int targetNode = getTargetNode(site_id);
    umf_memory_pool_handle_t targetPool = (targetNode == 2) ? g_hbmPool : g_ddrPool;

    void *ptr = umfPoolMalloc(targetPool, size);
    trackAllocation(ptr, size, site_id, "Allocated");
    return ptr;
}

void membrain_free(void *ptr) {
    if (ptr) {
        umfFree(ptr);
    }
}

int membrain_posix_memalign(void **memptr, size_t alignment, size_t size, uint32_t site_id) {
    membrain_init();

    if (!memptr) return EINVAL;

    if (size == 0) {
        *memptr = nullptr;
        return 0;
    }

    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }

    int targetNode = getTargetNode(site_id);
    umf_memory_pool_handle_t targetPool = (targetNode == 2) ? g_hbmPool : g_ddrPool;

    void *ptr = umfPoolAlignedMalloc(targetPool, size, alignment);
    if (!ptr) {
        *memptr = nullptr;
        return ENOMEM;
    }

    trackAllocation(ptr, size, site_id, "posix_memalign");
    *memptr = ptr;
    return 0;
}

} // extern "C"
