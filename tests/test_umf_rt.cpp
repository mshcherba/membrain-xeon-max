#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../runtime/membrain_rt.h"

void createGuidanceFile() {
    std::ofstream file("site_tier_guidance.json");
    file << "{\"site_id\": 1, \"tier\": 0}\n";
    file << "{\"site_id\": 2, \"tier\": 2}\n";
    file.close();
}

void removeGuidanceFile() {
    std::remove("site_tier_guidance.json");
}

int main() {
    std::cout << "[TestUMF] Creating temporary site_tier_guidance.json...\n";
    createGuidanceFile();

    std::cout << "[TestUMF] Initializing MemBrain UMF Runtime Test...\n";
    membrain_init();

    // Test 1: Allocate 64 bytes on DDR5 (Site ID 1 -> NUMA 0)
    void *ptrDdr = membrain_alloc(64, 1);
    assert(ptrDdr != nullptr);
    std::memset(ptrDdr, 0xAB, 64);
    std::cout << "[TestUMF] Successfully allocated and wrote 64 bytes via UMF DDR5 Pool (ptr: " << ptrDdr << ")\n";

    // Test 2: posix_memalign 128 bytes with 64-byte alignment on HBM2e (Site ID 2 -> NUMA 2)
    void *ptrHbm = nullptr;
    int res = membrain_posix_memalign(&ptrHbm, 64, 128, 2);
    assert(res == 0);
    assert(ptrHbm != nullptr);
    assert((reinterpret_cast<uintptr_t>(ptrHbm) % 64) == 0);
    std::memset(ptrHbm, 0xCD, 128);
    std::cout << "[TestUMF] Successfully posix_memalign 128 bytes via UMF HBM2e Pool (ptr: " << ptrHbm << ")\n";

    // Test 3: Free allocations via umfFree
    membrain_free(ptrDdr);
    membrain_free(ptrHbm);
    std::cout << "[TestUMF] Successfully freed all UMF allocations!\n";

    removeGuidanceFile();
    std::cout << "[TestUMF] ALL TESTS PASSED!\n";
    return 0;
}
