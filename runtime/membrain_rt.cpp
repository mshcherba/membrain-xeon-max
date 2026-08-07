#include "membrain_rt.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>

namespace {
static std::atomic<uint64_t> g_totalAllocations{0};
static std::atomic<uint64_t> g_totalBytesAllocated{0};
static bool g_verbose = false;
static std::once_flag g_initFlag;

void initRuntime() {
    if (const char *env = std::getenv("MEMBRAIN_VERBOSE")) {
        g_verbose = (env[0] == '1' || env[0] == 'y');
    }
    if (g_verbose) {
        std::cout << "[MemBrainRT] Runtime initialized (Verbose Mode ON)\n";
    }
}
} // namespace

extern "C" {

void membrain_init(void) {
    std::call_once(g_initFlag, initRuntime);
}

void *membrain_alloc(size_t size, uint32_t site_id) {
    membrain_init();

    g_totalAllocations.fetch_add(1, std::memory_order_relaxed);
    g_totalBytesAllocated.fetch_add(size, std::memory_order_relaxed);

    if (g_verbose) {
        std::cout << "[MemBrainRT] Site ID " << site_id << ": allocating " << size << " bytes\n";
    }

    return std::malloc(size);
}

void membrain_free(void *ptr) {
    if (ptr) {
        std::free(ptr);
    }
}

} // extern "C"
