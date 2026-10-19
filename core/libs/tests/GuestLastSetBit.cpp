#include "prx/libc/include/general/VabiMacros.hpp"
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>

extern "C" {
int APS5_VABI fls_nid_postfix(std::int32_t);
int APS5_VABI flsl_nid_postfix(std::int64_t);
int APS5_VABI flsll_nid_postfix(std::int64_t);
}

static void Require(bool value) {
    if (!value) std::abort();
}

template <typename TSigned, typename TUnsigned>
static void CheckWidth(int (APS5_VABI *lastSet)(TSigned)) {
    constexpr int width = std::numeric_limits<TUnsigned>::digits;
    Require(lastSet(0) == 0);
    Require(lastSet(-1) == width);
    for (int last = 0; last < width; ++last) {
        const auto high = TUnsigned{1} << last;
        Require(lastSet(std::bit_cast<TSigned>(high)) == last + 1);
        Require(lastSet(std::bit_cast<TSigned>(high - 1)) == last);
        for (int first = 0; first <= last; ++first) {
            const auto bits = high | (TUnsigned{1} << first);
            Require(lastSet(std::bit_cast<TSigned>(bits)) == last + 1);
        }
    }
    std::uint64_t state = 0x123456789abcdef0;
    for (int sample = 0; sample < 4096; ++sample) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const auto bits = static_cast<TUnsigned>(state);
        int last = 0;
        for (int bit = 0; bit < width; ++bit) {
            if ((bits & (TUnsigned{1} << bit)) != 0) last = bit + 1;
        }
        Require(lastSet(std::bit_cast<TSigned>(bits)) == last);
    }
}

int main() {
    CheckWidth<std::int32_t, std::uint32_t>(fls_nid_postfix);
    CheckWidth<std::int64_t, std::uint64_t>(flsl_nid_postfix);
    CheckWidth<std::int64_t, std::uint64_t>(flsll_nid_postfix);
}
