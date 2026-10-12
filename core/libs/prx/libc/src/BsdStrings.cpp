#include "prx/libc/include/general/VabiMacros.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {

void APS5_VABI bcopy_nid_postfix(const void* source, void* destination, std::size_t count) {
    std::memmove(destination, source, count);
}

char* APS5_VABI index_nid_postfix(const char* text, int character) {
    return const_cast<char*>(std::strchr(text, character));
}

char* APS5_VABI rindex_nid_postfix(const char* text, int character) {
    return const_cast<char*>(std::strrchr(text, character));
}


int APS5_VABI flsl_nid_postfix(std::int64_t value) {
    return static_cast<int>(std::bit_width(static_cast<std::uint64_t>(value)));
}

}
