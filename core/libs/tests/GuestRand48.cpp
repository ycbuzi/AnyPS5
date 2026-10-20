#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

extern "C" {
int APS5_VABI rand_nid_postfix();
void APS5_VABI srand_nid_postfix(unsigned int);
std::int64_t APS5_VABI lrand48_nid_postfix();
void APS5_VABI srand48_nid_postfix(std::int64_t);
}

static void Require(bool value) { if (!value) std::abort(); }

static void RequireRand48(std::initializer_list<std::int64_t> expected) {
    for (const auto value : expected) {
        const auto result = lrand48_nid_postfix();
        if (result != value) {
            std::fprintf(stderr, "Guest lrand48 returned %lld, expected %lld\n", static_cast<long long>(result), static_cast<long long>(value));
            std::abort();
        }
    }
}

static void CheckRand48() {
    RequireRand48({851401618, 1804928587, 758783491});
    srand48_nid_postfix(0);
    RequireRand48({366850414, 1610402240, 206956554});
    srand48_nid_postfix(1);
    RequireRand48({89400484, 976015093, 1792756325});
    srand48_nid_postfix(-1);
    RequireRand48({644300343, 97305740, 768640432});
    srand48_nid_postfix(INT64_C(0x123456789));
    RequireRand48({1707919128, 174994009, 774796281});
    srand48_nid_postfix(0);
    RequireRand48({366850414});
    srand48_nid_postfix(0);
    RequireRand48({366850414, 1610402240});
    srand48_nid_postfix(0);
    srand_nid_postfix(7);
    const int first = rand_nid_postfix();
    RequireRand48({366850414});
    srand_nid_postfix(12345);
    rand_nid_postfix();
    RequireRand48({1610402240});
    srand_nid_postfix(7);
    srand48_nid_postfix(99);
    lrand48_nid_postfix();
    Require(rand_nid_postfix() == first);
    for (int i = 0; i < 1000; ++i) {
        const auto value = lrand48_nid_postfix();
        Require(value >= 0 && value <= INT64_C(0x7fffffff));
    }
}

int main() {
    CheckRand48();
}
