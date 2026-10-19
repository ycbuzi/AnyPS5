#include "prx/libc/include/general/VabiMacros.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {

char* APS5_VABI index_nid_postfix(const char* text, int character) {
    return const_cast<char*>(std::strchr(text, character));
}

}
