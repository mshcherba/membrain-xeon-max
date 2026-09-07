#ifndef MEMBRAIN_SITE_POOL_MANAGER_H
#define MEMBRAIN_SITE_POOL_MANAGER_H

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "pebs_sampler.h"

#include <umf/memory_pool.h>

namespace membrain {

class SitePoolManager {
public:
    static SitePoolManager& instance();

    void initFromEnv();

    bool isProfilingMode() const { return m_profilingMode; }

    // Returns dedicated UMF pool tagged with siteId in profiling mode, or nullptr if disabled
    umf_memory_pool_handle_t getOrCreateSitePool(uint32_t siteId);

    // Explicitly triggers a pagemap RSS sample sweep across all coarse VMAs
    void samplePeakRss();

    // Drains in-process PEBS circular buffers and attributes samples to allocation sites in O(1)
    void drainPebsSamples();

    // Exports peak RSS and PEBS profile data to JSON file
    void exportProfileJson(const std::string& profileOut = "profile_data.json",
                           const std::string& sitesJson = "allocation_sites.json",
                           const std::string& rssProfileOut = "site_rss_profile.json");

    // Returns peak RSS bytes for a given siteId
    size_t getPeakRssBytes(uint32_t siteId) const;

    // Returns access count for a given siteId
    uint64_t getAccessCount(uint32_t siteId) const;

    // Resolves allocation siteId for a given virtual address, or returns 0 if not in a site pool
    uint32_t getSiteId(uintptr_t addr) const noexcept;

    // Fast-path heap boundary check to filter out non-heap addresses before umfPoolByPtr
    bool isPossibleHeapAddr(uint64_t addr) const noexcept {
        return (addr >= m_minHeapAddr.load(std::memory_order_relaxed) &&
                addr < m_maxHeapAddr.load(std::memory_order_relaxed));
    }

    void updateHeapBounds(uintptr_t start, uintptr_t end) noexcept;

    ~SitePoolManager();

private:
    SitePoolManager();

    void startSamplerThread();
    void stopSamplerThread();

    bool m_profilingMode{false};
    bool m_pebsEnabled{false};
    std::string m_sitesFile{"allocation_sites.json"};
    std::string m_profileOut{"profile_data.json"};
    mutable std::mutex m_mutex;

    // Direct siteId -> UMF pool map (pools tagged with siteId via umfPoolSetTag)
    std::unordered_map<uint32_t, umf_memory_pool_handle_t> m_sitePools;
    std::unordered_map<uint32_t, size_t> m_peakRssBytes;
    std::unordered_map<uint32_t, uint64_t> m_accessCounts;

    // Monotonic heap boundaries observed from coarse provider extents
    std::atomic<uintptr_t> m_minHeapAddr{UINTPTR_MAX};
    std::atomic<uintptr_t> m_maxHeapAddr{0};

    PebsSampler m_pebsSampler;

    std::atomic<bool> m_stopSampler{false};
    std::condition_variable m_cvSampler;
    std::mutex m_cvMutex;
    std::thread m_samplerThread;
};

} // namespace membrain

#endif // MEMBRAIN_SITE_POOL_MANAGER_H

