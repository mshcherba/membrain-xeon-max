#include "site_pool_manager.h"
#include "pagemap_util.h"
#include "topology_config.h"
#include "third_party/nlohmann/json.hpp"

#include <umf/memory_provider.h>
#include <umf/pools/pool_scalable.h>
#include <umf/providers/provider_os_memory.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
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
        exportProfileJson();
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

    if (m_profilingMode) {
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
    m_ptrSiteMap[ptr] = {siteId, size};
}

void SitePoolManager::unregisterAllocation(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_ptrSiteMap.find(ptr);
    if (it == m_ptrSiteMap.end()) return;

    uint32_t siteId = it->second.first;
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);

    auto& regions = m_siteRegions[siteId];
    regions.erase(
        std::remove_if(regions.begin(), regions.end(),
                       [start](const AllocRegion& r) { return r.start == start; }),
        regions.end()
    );

    m_ptrSiteMap.erase(it);
}

void SitePoolManager::samplePeakRss() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_siteRegions) {
        uint32_t siteId = entry.first;
        size_t currentResidentBytes = 0;

        for (const auto& reg : entry.second) {
            currentResidentBytes += pagemap::getResidentBytes(reg.start, reg.end);
        }

        if (currentResidentBytes > m_peakRssBytes[siteId]) {
            m_peakRssBytes[siteId] = currentResidentBytes;
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

void SitePoolManager::exportProfileJson(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    json jArray = json::array();

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
        jArray.push_back(jObj);
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << jArray.dump(2) << "\n";
        std::cout << "[MemBrainRT] Exported per-site physical Peak RSS profile for "
                  << jArray.size() << " allocation sites to '" << filepath << "'.\n";
    }
}

void SitePoolManager::startSamplerThread() {
    if (m_samplerThread.joinable()) return;
    m_stopSampler = false;
    m_samplerThread = std::thread([this]() {
        while (!m_stopSampler.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (m_stopSampler.load()) break;
            try {
                this->samplePeakRss();
            } catch (...) {}
        }
    });
}

void SitePoolManager::stopSamplerThread() {
    m_stopSampler = true;
    if (m_samplerThread.joinable()) {
        if (m_samplerThread.get_id() != std::this_thread::get_id()) {
            m_samplerThread.join();
        } else {
            m_samplerThread.detach();
        }
    }
}

} // namespace membrain
