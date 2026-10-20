#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <cstddef>

extern "C" void* APS5_VABI memmem_nid_postfix(const void* haystack, std::size_t haystackLength,
    const void* needle, std::size_t needleLength) {
    if (needleLength == 0) return const_cast<void*>(haystack);
    if (haystackLength < needleLength) return nullptr;
    const auto* first = static_cast<const unsigned char*>(haystack);
    const auto* pattern = static_cast<const unsigned char*>(needle);
    const auto* found = std::search(first, first + haystackLength, pattern, pattern + needleLength);
    return found == first + haystackLength ? nullptr : const_cast<unsigned char*>(found);
}
