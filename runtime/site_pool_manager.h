#ifndef MEMBRAIN_SITE_POOL_MANAGER_H
#define MEMBRAIN_SITE_POOL_MANAGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <umf/memory_pool.h>

namespace membrain {

struct AllocRegion {
    uintptr_t start;
    uintptr_t end;
    size_t size;
};

class SitePoolManager {
public:
    static SitePoolManager& instance();

    void initFromEnv();

    bool isProfilingMode() const { return m_profilingMode; }

    // Returns dedicated UMF pool for siteId in profiling mode, or nullptr if disabled
    umf_memory_pool_handle_t getOrCreateSitePool(uint32_t siteId);

    // Registers allocated memory region for siteId
    void registerAllocation(uint32_t siteId, void* ptr, size_t size);

    // Unregisters freed memory region
    void unregisterAllocation(void* ptr);

    // Explicitly triggers a pagemap RSS sample sweep across all site pools
    void samplePeakRss();

    // Exports peak RSS profile data to JSON file
    void exportProfileJson(const std::string& filepath = "site_rss_profile.json");

    // Returns peak RSS bytes for a given siteId
    size_t getPeakRssBytes(uint32_t siteId) const;

    ~SitePoolManager();

private:
    SitePoolManager();

    void startSamplerThread();
    void stopSamplerThread();

    bool m_profilingMode{false};
    mutable std::mutex m_mutex;

    std::unordered_map<uint32_t, umf_memory_pool_handle_t> m_sitePools;
    std::unordered_map<uint32_t, std::vector<AllocRegion>> m_siteRegions;
    std::unordered_map<void*, std::pair<uint32_t, size_t>> m_ptrSiteMap;
    std::unordered_map<uint32_t, size_t> m_peakRssBytes;

    std::atomic<bool> m_stopSampler{false};
    std::thread m_samplerThread;
};

} // namespace membrain

#endif // MEMBRAIN_SITE_POOL_MANAGER_H
