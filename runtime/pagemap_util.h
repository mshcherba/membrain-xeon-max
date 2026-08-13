#ifndef MEMBRAIN_PAGEMAP_UTIL_H
#define MEMBRAIN_PAGEMAP_UTIL_H

#include <cstddef>
#include <cstdint>

namespace membrain::pagemap {

// Returns the number of physical resident pages (PAGE_IS_PRESENT, bit 63 in pagemap)
// for the virtual address range [startAddr, endAddr).
size_t getResidentPages(uintptr_t startAddr, uintptr_t endAddr);

// Returns the number of physical resident bytes for the range [startAddr, endAddr).
size_t getResidentBytes(uintptr_t startAddr, uintptr_t endAddr);

} // namespace membrain::pagemap

#endif // MEMBRAIN_PAGEMAP_UTIL_H
