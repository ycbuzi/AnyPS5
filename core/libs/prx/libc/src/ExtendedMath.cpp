#include "prx/libc/include/general/VabiMacros.hpp"

#include <algorithm>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

static_assert(sizeof(long double) == 16 && std::numeric_limits<long double>::digits == 64);
static_assert(sizeof(long long) == sizeof(std::int64_t));

namespace {

long double DomainError_nid_no_patch() {
    errno = EDOM;
    std::feraiseexcept(FE_INVALID);
    return std::numeric_limits<long double>::quiet_NaN();
}

long double CheckRange_nid_no_patch(long double input, long double result) {
    if (std::isfinite(input)) {
        if (std::isinf(result)) {
            errno = ERANGE;
            std::feraiseexcept(FE_OVERFLOW | FE_INEXACT);
        } else if (result == 0.L && input != 0.L) {
            errno = ERANGE;
            std::feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
        }
    }
    return result;
}

long double CheckNeighbor_nid_no_patch(long double from, long double to, long double result) {
    if (from == to || std::isnan(result)) return result;
    if (std::isfinite(from) && std::isinf(result)) {
        errno = ERANGE;
        std::feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    } else if (result == 0.L || std::fpclassify(result) == FP_SUBNORMAL) {
        errno = ERANGE;
        std::feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    }
    return result;
}

}

extern "C" {

long double APS5_VABI nextafterl_nid_postfix(long double from, long double to) { return CheckNeighbor_nid_no_patch(from, to, std::nextafter(from, to)); }
long double APS5_VABI nexttowardl_nid_postfix(long double from, long double to) { return CheckNeighbor_nid_no_patch(from, to, std::nexttoward(from, to)); }

}
