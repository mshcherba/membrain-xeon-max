#include "site_pool_manager.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <cstdlib>

int main() {
    std::cout << "[TestSitePoolManager] Initializing SitePoolManager profiling test...\n";

    setenv("MEMBRAIN_PROFILE", "1", 1);
    auto& manager = membrain::SitePoolManager::instance();
    manager.initFromEnv();

    assert(manager.isProfilingMode());

    // Site 1: Allocate 10 pages (40,960 bytes)
    size_t size1 = 40960;
    umf_memory_pool_handle_t pool1 = manager.getOrCreateSitePool(1);
    assert(pool1 != nullptr);

    void* ptr1 = umfPoolMalloc(pool1, size1);
    assert(ptr1 != nullptr);

    // Touch first 5 pages of Site 1
    char* cptr1 = static_cast<char*>(ptr1);
    for (size_t i = 0; i < 5 * 4096; i += 4096) {
        cptr1[i] = 0xAA;
    }

    // Site 2: Allocate 20 pages (81,920 bytes)
    size_t size2 = 81920;
    umf_memory_pool_handle_t pool2 = manager.getOrCreateSitePool(2);
    assert(pool2 != nullptr);

    void* ptr2 = umfPoolMalloc(pool2, size2);
    assert(ptr2 != nullptr);

    // Touch all 20 pages of Site 2
    char* cptr2 = static_cast<char*>(ptr2);
    for (size_t i = 0; i < 20 * 4096; i += 4096) {
        cptr2[i] = 0xBB;
    }

    // Sample Peak RSS
    manager.samplePeakRss();

    size_t peakRss1 = manager.getPeakRssBytes(1);
    size_t peakRss2 = manager.getPeakRssBytes(2);

    std::cout << "[TestSitePoolManager] Site 1 Peak RSS: " << peakRss1 << " bytes (expected >= 20480)\n";
    std::cout << "[TestSitePoolManager] Site 2 Peak RSS: " << peakRss2 << " bytes (expected >= 81920)\n";

    assert(peakRss1 >= 20480);
    assert(peakRss2 >= 81920);

    // Export profile JSON
    manager.exportProfileJson("test_site_rss_profile.json");

    std::ifstream jfile("test_site_rss_profile.json");
    assert(jfile.is_open());
    std::string content((std::istreambuf_iterator<char>(jfile)), std::istreambuf_iterator<char>());
    assert(content.find("\"site_id\": 1") != std::string::npos);
    assert(content.find("\"site_id\": 2") != std::string::npos);
    jfile.close();
    std::remove("test_site_rss_profile.json");

    // Clean up
    umfFree(ptr1);
    umfFree(ptr2);

    std::cout << "[TestSitePoolManager] ALL SITE POOL MANAGER TESTS PASSED!\n";
    return 0;
}
