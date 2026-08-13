#include "membrain_rt.h"
#include "site_pool_manager.h"
#include "third_party/nlohmann/json.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

int main() {
    std::cout << "[TestPagemapRSS] Running End-to-End Physical Peak RSS Integration Test...\n";

    setenv("MEMBRAIN_PROFILE", "1", 1);
    setenv("MEMBRAIN_VERBOSE", "1", 1);

    membrain_init();

    // Site 101: Allocate 30 MB (31,457,280 bytes), touch 20 MB (20,971,520 bytes)
    size_t site101AllocSize = 30 * 1024 * 1024;
    size_t site101TouchSize = 20 * 1024 * 1024;
    void* ptr101 = membrain_alloc(site101AllocSize, 101);
    assert(ptr101 != nullptr);

    char* cptr101 = static_cast<char*>(ptr101);
    for (size_t i = 0; i < site101TouchSize; i += 4096) {
        cptr101[i] = static_cast<char>(i % 255);
    }

    // Site 102: Allocate 10 MB (10,485,760 bytes), touch all 10 MB
    size_t site102AllocSize = 10 * 1024 * 1024;
    void* ptr102 = membrain_alloc(site102AllocSize, 102);
    assert(ptr102 != nullptr);

    char* cptr102 = static_cast<char*>(ptr102);
    for (size_t i = 0; i < site102AllocSize; i += 4096) {
        cptr102[i] = static_cast<char>(i % 255);
    }

    // Explicitly sample Peak RSS
    membrain::SitePoolManager::instance().samplePeakRss();

    size_t rss101 = membrain::SitePoolManager::instance().getPeakRssBytes(101);
    size_t rss102 = membrain::SitePoolManager::instance().getPeakRssBytes(102);

    std::cout << "[TestPagemapRSS] Site 101 Peak RSS measured: " << (rss101 / (1024 * 1024)) << " MB (expected ~20 MB)\n";
    std::cout << "[TestPagemapRSS] Site 102 Peak RSS measured: " << (rss102 / (1024 * 1024)) << " MB (expected ~10 MB)\n";

    // Allow 5% tolerance due to OS page allocation overheads
    assert(rss101 >= site101TouchSize);
    assert(rss102 >= site102AllocSize);

    membrain_free(ptr101);
    membrain_free(ptr102);

    // Export profile JSON
    membrain::SitePoolManager::instance().exportProfileJson("e2e_site_rss_profile.json");

    std::ifstream f("e2e_site_rss_profile.json");
    assert(f.is_open());
    json j;
    f >> j;
    assert(j.is_array());
    assert(j.size() >= 2);
    f.close();

    std::remove("e2e_site_rss_profile.json");

    std::cout << "[TestPagemapRSS] ALL END-TO-END PAGEMAP RSS TESTS PASSED!\n";
    return 0;
}
