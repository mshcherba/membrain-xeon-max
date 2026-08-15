#include "site_pool_manager.h"
#include "pagemap_util.h"
#include "topology_config.h"
#include "third_party/nlohmann/json.hpp"

#include <umf/memory_provider.h>
#include <umf/pools/pool_scalable.h>
#include <umf/providers/provider_os_memory.h>

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

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sitePools.find(siteId);
    if (it != m_sitePools.end()) {
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
        m_sitePools[siteId] = pool;
    }
    return pool;
}

void SitePoolManager::registerAllocation(uint32_t siteId, void* ptr, size_t size) {
    if (!ptr || size == 0) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t end = start + size;

    m_siteRegions[siteId].push_back({start, end, size});
    m_historicalRegions[siteId].push_back({start, end, size});
    m_ptrSiteMap[ptr] = {siteId, size};

    if (m_peakRssBytes[siteId] == 0) {
        m_peakRssBytes[siteId] = size;
    }
}

void SitePoolManager::unregisterAllocation(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_ptrSiteMap.find(ptr);
    if (it == m_ptrSiteMap.end()) return;

    uint32_t siteId = it->second.first;
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    size_t size = it->second.second;
    uintptr_t end = start + size;

    size_t resBytes = pagemap::getResidentBytes(start, end);
    if (resBytes > m_peakRssBytes[siteId]) {
        m_peakRssBytes[siteId] = resBytes;
    }

    auto& regions = m_siteRegions[siteId];
    regions.erase(
        std::remove_if(regions.begin(), regions.end(),
                       [start](const AllocRegion& r) { return r.start == start; }),
        regions.end()
    );

    m_ptrSiteMap.erase(it);
}

void SitePoolManager::samplePeakRss() {
    std::unordered_map<uint32_t, std::vector<AllocRegion>> regionsSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        regionsSnapshot = m_siteRegions;
    }

    std::unordered_map<uint32_t, size_t> sampledRss;
    for (const auto& entry : regionsSnapshot) {
        uint32_t siteId = entry.first;
        size_t currentResidentBytes = 0;

        for (const auto& reg : entry.second) {
            currentResidentBytes += pagemap::getResidentBytes(reg.start, reg.end);
        }

        sampledRss[siteId] = currentResidentBytes;
    }

    {
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

    std::unordered_map<uint32_t, std::vector<AllocRegion>> regionsSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        regionsSnapshot = m_historicalRegions.empty() ? m_siteRegions : m_historicalRegions;
    }

    std::unordered_map<uint32_t, uint64_t> batchCounts;

    m_pebsSampler.drainSamples([&](uint64_t addr) {
        for (const auto& entry : regionsSnapshot) {
            uint32_t siteId = entry.first;
            for (const auto& reg : entry.second) {
                if (addr >= reg.start && addr < reg.end) {
                    batchCounts[siteId]++;
                    return;
                }
            }
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
