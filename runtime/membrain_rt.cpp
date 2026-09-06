#include "membrain_rt.h"
#include "topology_config.h"
#include "guidance_parser.h"
#include "site_pool_manager.h"

#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
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
static std::unordered_map<uint32_t, int> g_siteTierMap; // site_id -> NUMA node
static std::ofstream g_traceFile;
static std::mutex g_traceMutex;

static umf_memory_pool_handle_t g_hbmPool = nullptr;
static umf_memory_pool_handle_t g_ddrPool = nullptr;

void initUmfPools() {
    int hbmNode = membrain::topology::getHbmNode();
    int ddrNode = membrain::topology::getDdrNode();

    // 1. Initialize HBM OS Memory Provider
    umf_os_memory_provider_params_handle_t hbmParams = nullptr;
    if (umfOsMemoryProviderParamsCreate(&hbmParams) == UMF_RESULT_SUCCESS) {
        unsigned hbmNodes[] = {static_cast<unsigned>(hbmNode)};
        umfOsMemoryProviderParamsSetNumaList(hbmParams, hbmNodes, 1);
        umfOsMemoryProviderParamsSetNumaMode(hbmParams, UMF_NUMA_MODE_PREFERRED);
        umfOsMemoryProviderParamsSetName(hbmParams, "HBM2eProvider");

        umf_memory_provider_handle_t hbmProvider = nullptr;
        if (umfMemoryProviderCreate(umfOsMemoryProviderOps(), hbmParams, &hbmProvider) == UMF_RESULT_SUCCESS) {
            umfPoolCreate(umfScalablePoolOps(), hbmProvider, nullptr, UMF_POOL_CREATE_FLAG_OWN_PROVIDER, &g_hbmPool);
        }
        umfOsMemoryProviderParamsDestroy(hbmParams);
    }

    // 2. Initialize DDR OS Memory Provider
    umf_os_memory_provider_params_handle_t ddrParams = nullptr;
    if (umfOsMemoryProviderParamsCreate(&ddrParams) == UMF_RESULT_SUCCESS) {
        unsigned ddrNodes[] = {static_cast<unsigned>(ddrNode)};
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
        std::cout << "[MemBrainRT] Initialized UMF Scalable Memory Pools (HBM Node " << hbmNode
                  << ": OK, DDR Node " << ddrNode << ": OK)\n";
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

    if (!membrain::guidance::parseFile(path, g_siteTierMap)) {
        if (g_verbose) {
            std::cout << "[MemBrainRT] " << path << " not found or empty. Defaulting all allocations to DDR (NUMA "
                      << membrain::topology::getDdrNode() << ")\n";
        }
        return;
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

    membrain::topology::initFromEnv();
    membrain::SitePoolManager::instance().initFromEnv();

    parseGuidanceFile();
    initUmfPools();

    if (g_verbose) {
        std::cout << "[MemBrainRT] Initialized for Xeon Max (DDR Node: " << membrain::topology::getDdrNode()
                  << ", HBM Node: " << membrain::topology::getHbmNode() << ")\n";
    }
}

int getTargetNode(uint32_t site_id) {
    auto it = g_siteTierMap.find(site_id);
    if (it != g_siteTierMap.end()) {
        return membrain::topology::getNumaNodeForTier(it->second);
    }
    return membrain::topology::getDdrNode();
}

void trackAllocation(void *ptr, size_t size, uint32_t site_id, const char *allocName) {
    if (!ptr) return;
    int targetNode = getTargetNode(site_id);

    if (g_trace) {
        logAllocationTrace(ptr, size, site_id);
    }

    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
    g_totalBytesAllocated.fetch_add(size, std::memory_order_relaxed);
    if (targetNode == membrain::topology::getHbmNode()) {
        g_hbmAllocations.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_ddrAllocations.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_verbose) {
        std::cout << "[MemBrainRT] Site ID " << site_id << " -> " << allocName << " " << size
                  << " bytes on NUMA Node " << targetNode << " ("
                  << (targetNode == membrain::topology::getHbmNode() ? "HBM2e" : "DDR5") << ")\n";
    }
}

} // namespace

static thread_local bool g_inInit = false;

extern "C" {
void __libc_free(void *);
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);
}

static bool is_umf_ptr(const void *ptr) {
    if (!ptr) return false;
    umf_memory_pool_handle_t pool = nullptr;
    return (umfPoolByPtr(ptr, &pool) == UMF_RESULT_SUCCESS && pool != nullptr);
}

extern "C" {

void membrain_init(void) {
    if (g_inInit) return;
    g_inInit = true;
    std::call_once(g_initFlag, initRuntime);
    g_inInit = false;
}

void *membrain_alloc(size_t size, uint32_t site_id) {
    if (g_inInit) {
        return __libc_malloc(size);
    }

    membrain_init();

    if (size == 0) return nullptr;

    void *ptr = nullptr;
    auto& siteMgr = membrain::SitePoolManager::instance();

    if (siteMgr.isProfilingMode()) {
        umf_memory_pool_handle_t sitePool = siteMgr.getOrCreateSitePool(site_id);
        ptr = sitePool ? umfPoolMalloc(sitePool, size) : nullptr;
        if (ptr) {
            siteMgr.registerAllocation(site_id, ptr, size);
        }
    } else {
        int targetNode = getTargetNode(site_id);
        umf_memory_pool_handle_t targetPool = (targetNode == membrain::topology::getHbmNode()) ? g_hbmPool : g_ddrPool;
        ptr = umfPoolMalloc(targetPool, size);
    }

    trackAllocation(ptr, size, site_id, "Allocated");
    return ptr;
}

void membrain_free(void *ptr) {
    if (!ptr) return;
    if (is_umf_ptr(ptr)) {
        auto& siteMgr = membrain::SitePoolManager::instance();
        if (siteMgr.isProfilingMode()) {
            siteMgr.unregisterAllocation(ptr);
        }
        umfFree(ptr);
    } else {
        __libc_free(ptr);
    }
}

void free(void *ptr) noexcept {
    membrain_free(ptr);
}

void cfree(void *ptr) noexcept {
    membrain_free(ptr);
}

void *membrain_calloc(size_t num, size_t size, uint32_t site_id) {
    if (g_inInit) {
        return __libc_calloc(num, size);
    }

    size_t total = num * size;
    void *ptr = membrain_alloc(total, site_id);
    if (ptr && total > 0) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

void *membrain_realloc(void *ptr, size_t size, uint32_t site_id) {
    if (!ptr) return membrain_alloc(size, site_id);
    if (size == 0) {
        membrain_free(ptr);
        return nullptr;
    }

    if (g_inInit) {
        return __libc_realloc(ptr, size);
    }

    membrain_init();

    if (!is_umf_ptr(ptr)) {
        return __libc_realloc(ptr, size);
    }

    auto& siteMgr = membrain::SitePoolManager::instance();
    void *newPtr = nullptr;

    if (siteMgr.isProfilingMode()) {
        umf_memory_pool_handle_t sitePool = siteMgr.getOrCreateSitePool(site_id);
        newPtr = sitePool ? umfPoolRealloc(sitePool, ptr, size) : nullptr;
        if (newPtr) {
            if (newPtr != ptr) {
                siteMgr.unregisterAllocation(ptr);
            }
            siteMgr.registerAllocation(site_id, newPtr, size);
        }
    } else {
        int targetNode = getTargetNode(site_id);
        umf_memory_pool_handle_t targetPool = (targetNode == membrain::topology::getHbmNode()) ? g_hbmPool : g_ddrPool;
        newPtr = umfPoolRealloc(targetPool, ptr, size);
    }

    if (newPtr) {
        trackAllocation(newPtr, size, site_id, "realloc");
    }
    return newPtr;
}

void *membrain_aligned_alloc(size_t alignment, size_t size, uint32_t site_id) {
    void *ptr = nullptr;
    int err = membrain_posix_memalign(&ptr, alignment, size, site_id);
    if (err != 0) return nullptr;
    return ptr;
}

int membrain_posix_memalign(void **memptr, size_t alignment, size_t size, uint32_t site_id) {
    if (g_inInit) {
        void *ptr = __libc_memalign(alignment, size);
        if (!ptr && size > 0) return ENOMEM;
        *memptr = ptr;
        return 0;
    }

    membrain_init();

    if (!memptr) return EINVAL;

    if (size == 0) {
        *memptr = nullptr;
        return 0;
    }

    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }

    void *ptr = nullptr;
    auto& siteMgr = membrain::SitePoolManager::instance();

    if (siteMgr.isProfilingMode()) {
        umf_memory_pool_handle_t sitePool = siteMgr.getOrCreateSitePool(site_id);
        ptr = sitePool ? umfPoolAlignedMalloc(sitePool, size, alignment) : nullptr;
        if (ptr) {
            siteMgr.registerAllocation(site_id, ptr, size);
        }
    } else {
        int targetNode = getTargetNode(site_id);
        umf_memory_pool_handle_t targetPool = (targetNode == membrain::topology::getHbmNode()) ? g_hbmPool : g_ddrPool;
        ptr = umfPoolAlignedMalloc(targetPool, size, alignment);
    }

    if (!ptr) {
        *memptr = nullptr;
        return ENOMEM;
    }

    trackAllocation(ptr, size, site_id, "posix_memalign");
    *memptr = ptr;
    return 0;
}

} // extern "C"
