#include "pagemap_util.h"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
    std::cout << "[TestPagemap] Running PagemapUtil physical residence test...\n";

    int pagemapFd = open("/proc/self/pagemap", O_RDONLY);
    assert(pagemapFd >= 0);

    const size_t PAGE_SIZE = 4096;
    const size_t NUM_PAGES = 10;
    const size_t ALLOC_SIZE = NUM_PAGES * PAGE_SIZE;

    // Allocate memory via mmap (anonymous, uninitialized pages)
    void* ptr = mmap(NULL, ALLOC_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(ptr != MAP_FAILED && ptr != nullptr);

    uintptr_t startAddr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t endAddr = startAddr + ALLOC_SIZE;

    // Initially, anonymous uninitialized pages should not be resident (0 resident pages)
    size_t initialResident = membrain::pagemap::getResidentPages(pagemapFd, startAddr, endAddr);
    std::cout << "[TestPagemap] Initial resident pages before touch: " << initialResident << " / " << NUM_PAGES << "\n";

    // Touch 5 pages (force physical allocation in kernel page table)
    char* charPtr = static_cast<char*>(ptr);
    for (size_t i = 0; i < 5; ++i) {
        charPtr[i * PAGE_SIZE] = static_cast<char>(i + 1);
    }

    size_t touched5Resident = membrain::pagemap::getResidentPages(pagemapFd, startAddr, endAddr);
    std::cout << "[TestPagemap] Resident pages after touching 5 pages: " << touched5Resident << " / " << NUM_PAGES << "\n";
    assert(touched5Resident >= 5);

    // Touch remaining 5 pages
    for (size_t i = 5; i < NUM_PAGES; ++i) {
        charPtr[i * PAGE_SIZE] = static_cast<char>(i + 1);
    }

    size_t touchedAllResident = membrain::pagemap::getResidentPages(pagemapFd, startAddr, endAddr);
    std::cout << "[TestPagemap] Resident pages after touching all 10 pages: " << touchedAllResident << " / " << NUM_PAGES << "\n";
    assert(touchedAllResident == NUM_PAGES);

    size_t residentBytes = membrain::pagemap::getResidentBytes(pagemapFd, startAddr, endAddr);
    assert(residentBytes == ALLOC_SIZE);

    munmap(ptr, ALLOC_SIZE);
    close(pagemapFd);
    std::cout << "[TestPagemap] ALL PAGEMAP TESTS PASSED!\n";
    return 0;
}

