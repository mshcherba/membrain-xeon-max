#include "pagemap_util.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>

namespace membrain::pagemap {

static const size_t PAGE_SIZE = 4096;
static const uint64_t PM_PRESENT_BIT = (1ULL << 63);

size_t getResidentPages(uintptr_t startAddr, uintptr_t endAddr) {
    if (startAddr >= endAddr) return 0;

    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    uintptr_t alignedStart = startAddr & ~(PAGE_SIZE - 1);
    size_t totalBytes = endAddr - alignedStart;
    size_t numPages = (totalBytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t startPageIndex = alignedStart / PAGE_SIZE;
    off_t offset = static_cast<off_t>(startPageIndex * sizeof(uint64_t));

    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return 0;
    }

    size_t residentPages = 0;
    const size_t BUFFER_PAGES = 512;
    uint64_t pageEntries[BUFFER_PAGES];

    size_t pagesRemaining = numPages;
    while (pagesRemaining > 0) {
        size_t pagesToRead = (pagesRemaining < BUFFER_PAGES) ? pagesRemaining : BUFFER_PAGES;
        ssize_t bytesToRead = static_cast<ssize_t>(pagesToRead * sizeof(uint64_t));
        ssize_t bytesRead = read(fd, pageEntries, bytesToRead);

        if (bytesRead <= 0) break;

        size_t entriesRead = bytesRead / sizeof(uint64_t);
        for (size_t i = 0; i < entriesRead; ++i) {
            if ((pageEntries[i] & PM_PRESENT_BIT) != 0) {
                residentPages++;
            }
        }
        pagesRemaining -= entriesRead;
    }

    close(fd);
    return residentPages;
}

size_t getResidentBytes(uintptr_t startAddr, uintptr_t endAddr) {
    return getResidentPages(startAddr, endAddr) * PAGE_SIZE;
}

} // namespace membrain::pagemap
