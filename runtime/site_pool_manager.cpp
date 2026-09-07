#include "site_pool_manager.h"
#include "pagemap_util.h"
#include "topology_config.h"
#include "third_party/nlohmann/json.hpp"

#include <umf/memory_provider.h>
#include <umf/pools/pool_scalable.h>
#include <umf/providers/provider_os_memory.h>

#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>

using json = nlohmann::json;

namespace membrain {

SitePoolManager& SitePoolManager::instance() {
    static SitePoolManager manager;
    return manager;
}

SitePoolManager::SitePoolManager() {
    initFromEnv();
}

SitePoolManager::~SitePoolManager() {
    stopSamplerThread();
    if (m_profilingMode) {
        samplePeakRss();
        drainPebsSamples();
        std::cout << "[MemBrainRT] Total PEBS hardware samples captured: " 
                  << m_pebsSampler.getTotalSamples() << "\n";
        exportProfileJson(m_profileOut, m_sitesFile);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_sitePools) {
        if (entry.second) {
            umfPoolDestroy(entry.second);
        }
    }
    m_sitePools.clear();
}

void SitePoolManager::initFromEnv() {
    if (const char *envP = std::getenv("MEMBRAIN_PROFILE")) {
        m_profilingMode = (envP[0] == '1' || envP[0] == 'y');
    }
    if (const char *envT = std::getenv("MEMBRAIN_TRACE")) {
        if (envT[0] == '1' || envT[0] == 'y') {
            m_profilingMode = true;
        }
    }
    if (const char *envSites = std::getenv("MEMBRAIN_SITES_FILE")) {
        m_sitesFile = envSites;
    }
    if (const char *envProfOut = std::getenv("MEMBRAIN_OUTPUT_PROFILE")) {
        m_profileOut = envProfOut;
    }

    if (m_profilingMode) {
        uint64_t samplePeriod = 512;
        if (const char *envPeriod = std::getenv("MEMBRAIN_SAMPLE_PERIOD")) {
            char *end = nullptr;
            unsigned long val = std::strtoul(envPeriod, &end, 10);
            if (val > 0) samplePeriod = val;
        }

        m_pebsEnabled = m_pebsSampler.init(samplePeriod);
        startSamplerThread();
    }
}

umf_memory_pool_handle_t SitePoolManager::getOrCreateSitePool(uint32_t siteId) {
    if (!m_profilingMode) return nullptr;

    // Fast-path thread-local 1-entry cache (0 locks, ~2 cycles)
    static thread_local uint32_t t_lastSiteId = 0;
    static thread_local umf_memory_pool_handle_t t_lastPool = nullptr;
    if (siteId == t_lastSiteId && t_lastPool != nullptr) {
        return t_lastPool;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sitePools.find(siteId);
    if (it != m_sitePools.end()) {
        t_lastSiteId = siteId;
        t_lastPool = it->second;
        return it->second;
    }

    // Create dedicated UMF pool for this siteId
    umf_os_memory_provider_params_handle_t params = nullptr;
    umf_memory_pool_handle_t pool = nullptr;

    if (umfOsMemoryProviderParamsCreate(&params) == UMF_RESULT_SUCCESS) {
        unsigned ddrNode = static_cast<unsigned>(topology::getDdrNode());
        umfOsMemoryProviderParamsSetNumaList(params, &ddrNode, 1);
        umfOsMemoryProviderParamsSetNumaMode(params, UMF_NUMA_MODE_PREFERRED);

        std::string providerName = "SitePool_" + std::to_string(siteId);
        umfOsMemoryProviderParamsSetName(params, providerName.c_str());

        umf_memory_provider_handle_t provider = nullptr;
        if (umfMemoryProviderCreate(umfOsMemoryProviderOps(), params, &provider) == UMF_RESULT_SUCCESS) {
            umfPoolCreate(umfScalablePoolOps(), provider, nullptr, UMF_POOL_CREATE_FLAG_OWN_PROVIDER, &pool);
        }
        umfOsMemoryProviderParamsDestroy(params);
    }

    if (pool) {
        umfPoolSetTag(pool, reinterpret_cast<void*>(static_cast<uintptr_t>(siteId)), nullptr);
        m_sitePools[siteId] = pool;
        t_lastSiteId = siteId;
        t_lastPool = pool;
    }
    return pool;
}

void SitePoolManager::updateHeapBounds(uintptr_t start, uintptr_t end) noexcept {
    uintptr_t curMin = m_minHeapAddr.load(std::memory_order_relaxed);
    while (start < curMin && !m_minHeapAddr.compare_exchange_weak(curMin, start, std::memory_order_relaxed)) {}

    uintptr_t curMax = m_maxHeapAddr.load(std::memory_order_relaxed);
    while (end > curMax && !m_maxHeapAddr.compare_exchange_weak(curMax, end, std::memory_order_relaxed)) {}
}

uint32_t SitePoolManager::getSiteId(uintptr_t addr) const noexcept {
    umf_memory_pool_handle_t pool = nullptr;
    void* tag = nullptr;
    if (umfPoolByPtr(reinterpret_cast<const void*>(addr), &pool) == UMF_RESULT_SUCCESS && pool &&
        umfPoolGetTag(pool, &tag) == UMF_RESULT_SUCCESS) {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tag));
    }
    return 0;
}

void SitePoolManager::samplePeakRss() {
    int pagemapFd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemapFd < 0) return;

    FILE* mapsFile = fopen("/proc/self/maps", "r");
    if (!mapsFile) {
        close(pagemapFd);
        return;
    }

    std::unordered_map<uint32_t, size_t> sampledRss;
    char line[512];

    while (fgets(line, sizeof(line), mapsFile)) {
        // Only inspect anonymous readable/writable mappings (rw-p) that are not files or stack/vdso
        if (strstr(line, "rw-p") == nullptr) continue;
        if (strchr(line, '/') != nullptr) continue;
        if (strchr(line, '[') != nullptr) continue;

        uintptr_t start = 0;
        uintptr_t end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
        if (start >= end) continue;

        constexpr uintptr_t STEP = 2 * 1024 * 1024;
        uintptr_t curr = start;
        while (curr < end) {
            uint32_t siteId = getSiteId(curr);
            uintptr_t next = curr + STEP;
            while (next < end && getSiteId(next) == siteId) {
                next += STEP;
            }

            uintptr_t rangeEnd = (next > end) ? end : next;
            if (siteId != 0) {
                updateHeapBounds(curr, rangeEnd);
                sampledRss[siteId] += pagemap::getResidentBytes(pagemapFd, curr, rangeEnd);
            }
            curr = rangeEnd;
        }
    }

    fclose(mapsFile);
    close(pagemapFd);

    if (!sampledRss.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : sampledRss) {
            uint32_t siteId = entry.first;
            if (entry.second > m_peakRssBytes[siteId]) {
                m_peakRssBytes[siteId] = entry.second;
            }
        }
    }
}

void SitePoolManager::drainPebsSamples() {
    if (!m_pebsSampler.isEnabled()) return;

    std::unordered_map<uint32_t, uint64_t> batchCounts;

    m_pebsSampler.drainSamples([&](uint64_t addr) {
        if (!isPossibleHeapAddr(addr)) return;
        if (uint32_t siteId = getSiteId(addr)) {
            batchCounts[siteId]++;
        }
    });

    if (!batchCounts.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kv : batchCounts) {
            m_accessCounts[kv.first] += kv.second;
        }
    }
}


size_t SitePoolManager::getPeakRssBytes(uint32_t siteId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_peakRssBytes.find(siteId);
    if (it != m_peakRssBytes.end()) {
        return it->second;
    }
    return 0;
}

uint64_t SitePoolManager::getAccessCount(uint32_t siteId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_accessCounts.find(siteId);
    if (it != m_accessCounts.end()) {
        return it->second;
    }
    return 0;
}

void SitePoolManager::exportProfileJson(const std::string& profileOut,
                                       const std::string& sitesJson,
                                       const std::string& rssProfileOut) {
    std::lock_guard<std::mutex> lock(m_mutex);


    // 1. Export site_rss_profile.json
    json jRssArray = json::array();
    for (const auto& entry : m_sitePools) {
        uint32_t siteId = entry.first;
        size_t peakRss = 0;
        auto rssIt = m_peakRssBytes.find(siteId);
        if (rssIt != m_peakRssBytes.end()) {
            peakRss = rssIt->second;
        }

        json jObj;
        jObj["site_id"] = siteId;
        jObj["peak_rss_bytes"] = peakRss;
        jRssArray.push_back(jObj);
    }

    std::ofstream rssFile(rssProfileOut);
    if (rssFile.is_open()) {
        rssFile << jRssArray.dump(2) << "\n";
    }

    // 2. Load allocation sites metadata if available
    json sitesData = json::array();
    std::ifstream sitesFile(sitesJson);
    if (sitesFile.is_open()) {
        try {
            sitesFile >> sitesData;
        } catch (...) {}
    }

    json jProfile = json::array();

    if (sitesData.is_array() && !sitesData.empty()) {
        for (const auto& site : sitesData) {
            uint32_t siteId = site.value("site_id", 0);
            size_t rssBytes = 0;
            auto rssIt = m_peakRssBytes.find(siteId);
            if (rssIt != m_peakRssBytes.end()) {
                rssBytes = rssIt->second;
            }

            uint64_t accessCount = 0;
            auto accIt = m_accessCounts.find(siteId);
            if (accIt != m_accessCounts.end()) {
                accessCount = accIt->second;
            }

            double hotness = (rssBytes > 0) ? static_cast<double>(accessCount) / static_cast<double>(rssBytes) : 0.0;
            hotness = std::round(hotness * 1000000.0) / 1000000.0;

            json item;
            item["site_id"] = siteId;
            item["function"] = site.value("function", "unknown");
            item["file"] = site.value("file", "unknown");
            item["line"] = site.value("line", 0);
            item["rss_bytes"] = rssBytes;
            item["access_count"] = accessCount;
            item["hotness"] = hotness;
            jProfile.push_back(item);
        }
    } else {
        for (const auto& entry : m_sitePools) {
            uint32_t siteId = entry.first;
            size_t rssBytes = 0;
            auto rssIt = m_peakRssBytes.find(siteId);
            if (rssIt != m_peakRssBytes.end()) {
                rssBytes = rssIt->second;
            }

            uint64_t accessCount = 0;
            auto accIt = m_accessCounts.find(siteId);
            if (accIt != m_accessCounts.end()) {
                accessCount = accIt->second;
            }

            double hotness = (rssBytes > 0) ? static_cast<double>(accessCount) / static_cast<double>(rssBytes) : 0.0;
            hotness = std::round(hotness * 1000000.0) / 1000000.0;

            json item;
            item["site_id"] = siteId;
            item["function"] = "unknown";
            item["file"] = "unknown";
            item["line"] = 0;
            item["rss_bytes"] = rssBytes;
            item["access_count"] = accessCount;
            item["hotness"] = hotness;
            jProfile.push_back(item);
        }
    }

    std::ofstream profFile(profileOut);
    if (profFile.is_open()) {
        profFile << jProfile.dump(2) << "\n";
        std::cout << "[MemBrainRT] Exported PEBS profile data for " 
                  << jProfile.size() << " allocation sites to '" << profileOut << "'.\n";
    }
}

void SitePoolManager::startSamplerThread() {
    if (m_samplerThread.joinable()) return;
    m_stopSampler = false;

    if (m_pebsEnabled) {
        m_pebsSampler.start();
    }

    m_samplerThread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(m_cvMutex);
        auto lastRssTime = std::chrono::steady_clock::now();

        while (!m_stopSampler.load()) {
            if (m_cvSampler.wait_for(lock, std::chrono::milliseconds(50), [this]() { return m_stopSampler.load(); })) {
                break;
            }
            try {
                this->drainPebsSamples();

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRssTime).count() >= 5) {
                    this->samplePeakRss();
                    lastRssTime = now;
                }
            } catch (...) {}
        }
    });
}

void SitePoolManager::stopSamplerThread() {
    m_stopSampler = true;
    m_cvSampler.notify_all();

    if (m_pebsEnabled) {
        m_pebsSampler.stop();
    }

    if (m_samplerThread.joinable()) {
        if (m_samplerThread.get_id() != std::this_thread::get_id()) {
            m_samplerThread.join();
        } else {
            m_samplerThread.detach();
        }
    }
}

} // namespace membrain
