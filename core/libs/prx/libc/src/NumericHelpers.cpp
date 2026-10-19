#include "prx/libc/include/general/VabiMacros.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

template<typename TValue>
bool IsNan_nid_no_patch(TValue value) {
    if constexpr (sizeof(TValue) == sizeof(std::uint32_t)) {
        const auto bits = std::bit_cast<std::uint32_t>(value) & UINT32_C(0x7fffffff);
        return bits > UINT32_C(0x7f800000);
    } else {
        const auto bits = std::bit_cast<std::uint64_t>(value) & UINT64_C(0x7fffffffffffffff);
        return bits > UINT64_C(0x7ff0000000000000);
    }
}

template<typename TValue>
TValue Extremum_nid_no_patch(TValue left, TValue right, bool maximum) {
    if (IsNan_nid_no_patch(left)) return right;
    if (IsNan_nid_no_patch(right)) return left;
    if (left == right) {
        if (maximum) return std::signbit(left) ? right : left;
        return std::signbit(left) ? left : right;
    }
    return (maximum ? left > right : left < right) ? left : right;
}

int BoundExponent_nid_no_patch(std::int64_t exponent) {
    return static_cast<int>(std::clamp(exponent, static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                      static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

template<typename TValue, typename TTarget>
TValue CheckNeighbor_nid_no_patch(TValue from, TTarget to, TValue result) {
    if (from == to || std::isnan(result)) return result;
    if (std::isfinite(from) && std::isinf(result)) {
        errno = ERANGE;
        std::feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    } else if (result == 0 || std::fpclassify(result) == FP_SUBNORMAL) {
        errno = ERANGE;
        std::feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    }
    return result;
}

}

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(sizeof(long double) == 16 && std::numeric_limits<long double>::digits == 64);

extern "C" {

double APS5_VABI nexttoward_nid_postfix(double from, long double to) { return CheckNeighbor_nid_no_patch(from, to, std::nexttoward(from, to)); }
float APS5_VABI nexttowardf_nid_postfix(float from, long double to) { return CheckNeighbor_nid_no_patch(from, to, std::nexttoward(from, to)); }

}
