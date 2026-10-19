#include "prx/libc/include/general/VabiMacros.hpp"
#include <bit>
#include <cstdint>

extern "C" {

int APS5_VABI fls_nid_postfix(std::int32_t value) {
    return static_cast<int>(std::bit_width(static_cast<std::uint32_t>(value)));
}

int APS5_VABI flsl_nid_postfix(std::int64_t value) {
    return static_cast<int>(std::bit_width(static_cast<std::uint64_t>(value)));
}

int APS5_VABI flsll_nid_postfix(std::int64_t value) {
    return static_cast<int>(std::bit_width(static_cast<std::uint64_t>(value)));
}

}
