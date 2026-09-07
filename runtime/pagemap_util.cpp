#include "pagemap_util.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

namespace membrain::pagemap {

static const size_t PAGE_SIZE = 4096;
static const uint64_t PM_PRESENT_BIT = (1ULL << 63);

size_t getResidentPages(int pagemapFd, uintptr_t startAddr, uintptr_t endAddr) {
    if (pagemapFd < 0 || startAddr >= endAddr) return 0;

    uintptr_t alignedStart = startAddr & ~(PAGE_SIZE - 1);
    size_t totalBytes = endAddr - alignedStart;
    size_t numPages = (totalBytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t startPageIndex = alignedStart / PAGE_SIZE;
    off_t offset = static_cast<off_t>(startPageIndex * sizeof(uint64_t));

    size_t residentPages = 0;
    const size_t BUFFER_PAGES = 512;
    uint64_t pageEntries[BUFFER_PAGES];

    size_t pagesRemaining = numPages;
    while (pagesRemaining > 0) {
        size_t pagesToRead = (pagesRemaining < BUFFER_PAGES) ? pagesRemaining : BUFFER_PAGES;
        size_t bytesToRead = pagesToRead * sizeof(uint64_t);
        ssize_t bytesRead = pread(pagemapFd, pageEntries, bytesToRead, offset);

        if (bytesRead <= 0) break;

        size_t entriesRead = static_cast<size_t>(bytesRead) / sizeof(uint64_t);
        for (size_t i = 0; i < entriesRead; ++i) {
            if ((pageEntries[i] & PM_PRESENT_BIT) != 0) {
                residentPages++;
            }
        }
        pagesRemaining -= entriesRead;
        offset += bytesRead;
    }

    return residentPages;
}

size_t getResidentBytes(int pagemapFd, uintptr_t startAddr, uintptr_t endAddr) {
    return getResidentPages(pagemapFd, startAddr, endAddr) * PAGE_SIZE;
}


} // namespace membrain::pagemap

