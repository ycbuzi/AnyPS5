#include "prx/libc/include/general/VabiMacros.hpp"
#include <bit>
#include <cerrno>
#include <cfenv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>

extern "C" {
double APS5_VABI j0_nid_postfix(double);
double APS5_VABI j1_nid_postfix(double);
double APS5_VABI jn_nid_postfix(int, double);
double APS5_VABI y0_nid_postfix(double);
double APS5_VABI y1_nid_postfix(double);
double APS5_VABI yn_nid_postfix(int, double);
int* APS5_VABI __error_nid_postfix();
}

static void Require(bool value) { if (!value) std::abort(); }

static double Value(std::uint64_t bits) { return std::bit_cast<double>(bits); }

static std::uint64_t Bits(double value) { return std::bit_cast<std::uint64_t>(value); }

struct UnaryCase { std::uint64_t input; std::uint64_t output; };
struct OrderCase { int order; std::uint64_t input; std::uint64_t output; };

static void CheckUnary(const char* name, double (APS5_VABI *function)(double), const UnaryCase* cases, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        *__error_nid_postfix() = 12345;
        const double result = function(Value(cases[index].input));
        if (Bits(result) != cases[index].output || *__error_nid_postfix() != 12345) {
            std::fprintf(stderr, "Guest %s failed for %016llx: %016llx\n", name, static_cast<unsigned long long>(cases[index].input),
                         static_cast<unsigned long long>(Bits(result)));
            std::abort();
        }
    }
}

static void CheckOrder(const char* name, double (APS5_VABI *function)(int, double), const OrderCase* cases, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        *__error_nid_postfix() = 12345;
        const double result = function(cases[index].order, Value(cases[index].input));
        if (Bits(result) != cases[index].output || *__error_nid_postfix() != 12345) {
            std::fprintf(stderr, "Guest %s failed for %d, %016llx: %016llx\n", name, cases[index].order,
                         static_cast<unsigned long long>(cases[index].input), static_cast<unsigned long long>(Bits(result)));
            std::abort();
        }
    }
}

static void CheckFirstKind() {
    const UnaryCase zero[] = {
        {0x0000000000000000ull, 0x3ff0000000000000ull}, {0x8000000000000000ull, 0x3ff0000000000000ull}, {0x3ddb7cdfd9d7bdbbull, 0x3ff0000000000000ull},
        {0x3e40000000000000ull, 0x3ff0000000000000ull}, {0x3eb0000000000000ull, 0x3feffffffffff800ull}, {0x3f1fffffffffffffull, 0x3feffffffe000000ull},
        {0x3f20000000000000ull, 0x3feffffffe000000ull}, {0x3fd0000000000000ull, 0x3fef807fc72aa864ull}, {0xbfe0000000000000ull, 0x3fee07f1d54c3f34ull},
        {0x3fe8000000000000ull, 0x3feba7df6a752a19ull}, {0x3fefffffffffffffull, 0x3fe87c7fdbd7b8f0ull}, {0x3ff0000000000000ull, 0x3fe87c7fdbd7b8f0ull},
        {0xbff8000000000000ull, 0x3fe060e46ce9651bull}, {0x3fffffffffffffffull, 0x3fcca873fb24cefcull}, {0x7ff0000000000000ull, 0x0000000000000000ull},
        {0xfff0000000000000ull, 0x0000000000000000ull}, {0x7ff8000000000123ull, 0x7ff8000000000123ull}, {0x3fee4317b35275e1ull, 0x3fe93c4854d71c27ull},
        {0x3fe7084d7429a0caull, 0x3febfce08c913631ull}, {0x3ff896d4ffdc2a13ull, 0x3fdf6faac7e7d3ceull}, {0x3ff28d079588dbb8ull, 0x3fe61dc352e1dc33ull},
        {0x3ffe7d3678a4d9a8ull, 0x3fd1d44a54769967ull},
    };
    CheckUnary("j0", j0_nid_postfix, zero, std::size(zero));
    const UnaryCase one[] = {
        {0x0000000000000000ull, 0x0000000000000000ull}, {0x8000000000000000ull, 0x8000000000000000ull}, {0x3ddb7cdfd9d7bdbbull, 0x3dcb7cdfd9d7bdbbull},
        {0x3e40000000000000ull, 0x3e30000000000000ull}, {0x3eb0000000000000ull, 0x3e9ffffffffffc00ull}, {0x3f1fffffffffffffull, 0x3f0ffffffeffffffull},
        {0x3f20000000000000ull, 0x3f0fffffff000000ull}, {0x3fd0000000000000ull, 0x3fbfc02a9c749ee9ull}, {0xbfe0000000000000ull, 0xbfcf02a71f4870d7ull},
        {0x3fe8000000000000ull, 0x3fd65a01d66b68bdull}, {0x3fefffffffffffffull, 0x3fdc29c9ee970c6cull}, {0x3ff0000000000000ull, 0x3fdc29c9ee970c6cull},
        {0xbff8000000000000ull, 0xbfe1da9da9d6fc81ull}, {0x3fffffffffffffffull, 0x3fe27487958371f0ull}, {0x7ff0000000000000ull, 0x0000000000000000ull},
        {0xfff0000000000000ull, 0x8000000000000000ull}, {0x7ff8000000000123ull, 0x7ff8000000000123ull}, {0x3fecd8b073b795c0ull, 0x3fda038c68b70948ull},
        {0x3fffe99bfa2fcbc2ull, 0x3fe2775e2433b1d8ull}, {0x3ffa917f997cc231ull, 0x3fe267a06350cc21ull},
    };
    CheckUnary("j1", j1_nid_postfix, one, std::size(one));
}

static void CheckSecondKind() {
    const UnaryCase zero[] = {
        {0x0000000000000000ull, 0xfff0000000000000ull}, {0x8000000000000000ull, 0xfff0000000000000ull}, {0xbff0000000000000ull, 0x7ff8000000000000ull},
        {0x8000000000000001ull, 0x7ff8000000000000ull}, {0x7ff0000000000000ull, 0x0000000000000000ull},
    };
    CheckUnary("y0", y0_nid_postfix, zero, std::size(zero));
    const UnaryCase one[] = {
        {0x0000000000000000ull, 0xfff0000000000000ull}, {0x8000000000000000ull, 0xfff0000000000000ull}, {0xbff0000000000000ull, 0x7ff8000000000000ull},
        {0x8000000000000001ull, 0x7ff8000000000000ull}, {0x7ff0000000000000ull, 0x0000000000000000ull}, {0x3bc79ca10c924223ull, 0xc40b9be638027fc9ull},
        {0x3c90000000000000ull, 0xc3445f306dc9c883ull}, {0x0000000000000001ull, 0xfff0000000000000ull},
    };
    CheckUnary("y1", y1_nid_postfix, one, std::size(one));
    Require(std::isnan(y0_nid_postfix(-std::numeric_limits<double>::infinity())) && std::isnan(y1_nid_postfix(-std::numeric_limits<double>::infinity())));
    Require(std::isnan(y0_nid_postfix(std::numeric_limits<double>::quiet_NaN())) && std::isnan(y1_nid_postfix(std::numeric_limits<double>::quiet_NaN())));
    for (const double input : {0.0, -0.0, -1.0, -std::numeric_limits<double>::denorm_min()}) {
        std::feclearexcept(FE_ALL_EXCEPT);
        y0_nid_postfix(input);
        y1_nid_postfix(input);
        yn_nid_postfix(3, input);
        Require(std::fetestexcept(FE_DIVBYZERO | FE_INVALID) == 0);
    }
}

static void CheckIntegerOrders() {
    const OrderCase first[] = {
        {2, 0x3ddb7cdfd9d7bdbbull, 0x3b979ca10c924224ull}, {5, 0xbddb7cdfd9d7bdbbull, 0xb4cfed4f3ec0c466ull}, {33, 0x3e00000000000000ull, 0x0000000000000000ull},
        {34, 0x3e00000000000000ull, 0x0000000000000000ull}, {-3, 0x3e00000000000000ull, 0xb9c5555555555555ull}, {2, 0x3ff8000000000000ull, 0x3fcdb50c80d5039aull},
        {5, 0x3ff0000000000000ull, 0x3f305e3d487e8377ull}, {10, 0x3fe0000000000000ull, 0x3d52637b5990470aull}, {-3, 0x3ff4000000000000ull, 0xbfa2e068dde7f417ull},
        {3, 0xbff4000000000000ull, 0xbfa2e068dde7f417ull}, {4, 0xbff4000000000000ull, 0x3f781279da92ee26ull}, {40, 0x3ff8000000000000ull, 0x34f2a1a3311d8462ull},
        {300, 0x3ff0000000000000ull, 0x0000000000000000ull}, {3, 0x8000000000000000ull, 0x8000000000000000ull}, {3, 0xfff0000000000000ull, 0x8000000000000000ull},
        {2, 0xfff0000000000000ull, 0x0000000000000000ull}, {7, 0x7ff8000000000123ull, 0x7ff8000000000123ull}, {-1, 0x3fe0000000000000ull, 0xbfcf02a71f4870d7ull},
        {0, 0xbfe0000000000000ull, 0x3fee07f1d54c3f34ull}, {3, 0x3fe91c9d97f75a72ull, 0x3f83d672272043afull}, {3, 0x3ff33bf06c96fafdull, 0x3fa0ea842d47fdfdull},
        {3, 0x3ffa0a4f9419df40ull, 0x3fb36db8f4e44fabull}, {3, 0x3ffb2f9912a3a9a6ull, 0x3fb5c4cf9b0be24full},
    };
    CheckOrder("jn", jn_nid_postfix, first, std::size(first));
    const OrderCase second[] = {
        {2, 0x7ff0000000000000ull, 0x0000000000000000ull}, {-3, 0x7ff0000000000000ull, 0x0000000000000000ull}, {3, 0x0000000000000000ull, 0xfff0000000000000ull},
        {-4, 0x8000000000000000ull, 0xfff0000000000000ull}, {3, 0xbff0000000000000ull, 0x7ff8000000000000ull}, {5, 0x7ff8000000000123ull, 0x7ff8000000000123ull},
        {INT_MIN, 0x7ff0000000000000ull, 0x0000000000000000ull},
    };
    CheckOrder("yn", yn_nid_postfix, second, std::size(second));
    for (const double x : {0.75, 2.5, 3.0, 17.25, 1.0e5, 0x1p303}) {
        Require(Bits(jn_nid_postfix(0, x)) == Bits(j0_nid_postfix(x)) && Bits(jn_nid_postfix(1, x)) == Bits(j1_nid_postfix(x)));
        Require(Bits(jn_nid_postfix(-1, x)) == Bits(j1_nid_postfix(-x)) && Bits(jn_nid_postfix(-3, x)) == Bits(-jn_nid_postfix(3, x)));
        Require(Bits(jn_nid_postfix(4, -x)) == Bits(jn_nid_postfix(4, x)) && Bits(jn_nid_postfix(5, -x)) == Bits(-jn_nid_postfix(5, x)));
        Require(Bits(yn_nid_postfix(0, x)) == Bits(y0_nid_postfix(x)) && Bits(yn_nid_postfix(1, x)) == Bits(y1_nid_postfix(x)));
        Require(Bits(yn_nid_postfix(-1, x)) == Bits(-y1_nid_postfix(x)) && Bits(yn_nid_postfix(-4, x)) == Bits(yn_nid_postfix(4, x)));
        if (x < 0x1p302) Require(Bits(yn_nid_postfix(INT_MIN, x)) == Bits(yn_nid_postfix(2, x)));
    }
}

static bool Near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= std::fabs(expected) * tolerance;
}

static void CheckValues() {
    for (const double x : {0.3, 1.7, 2.5, 7.25, 25.0, 123.5}) {
        Require(Near(j0_nid_postfix(x), std::cyl_bessel_j(0.0, x), 1e-12) && Near(j1_nid_postfix(x), std::cyl_bessel_j(1.0, x), 1e-12));
        Require(Near(y0_nid_postfix(x), std::cyl_neumann(0.0, x), 1e-12) && Near(y1_nid_postfix(x), std::cyl_neumann(1.0, x), 1e-12));
        for (const int order : {2, 3, 7}) {
            Require(Near(jn_nid_postfix(order, x), std::cyl_bessel_j(order, x), 1e-11));
            Require(Near(yn_nid_postfix(order, x), std::cyl_neumann(order, x), 1e-11));
        }
    }
    Require(Near(j0_nid_postfix(-2.5), std::cyl_bessel_j(0.0, 2.5), 1e-12) && Near(j1_nid_postfix(-2.5), -std::cyl_bessel_j(1.0, 2.5), 1e-12));
    *__error_nid_postfix() = 12345;
    for (const double x : {0x1p-30, 0.5, 3.0, 1.0e10, 0x1p310, std::numeric_limits<double>::max()}) {
        Require(std::isfinite(j0_nid_postfix(x)) && std::isfinite(j1_nid_postfix(x)) && std::isfinite(y0_nid_postfix(x)) && std::isfinite(y1_nid_postfix(x)));
        Require(std::isfinite(jn_nid_postfix(6, x)) && !std::isnan(yn_nid_postfix(6, x)));
    }
    Require(*__error_nid_postfix() == 12345);
    *__error_nid_postfix() = 0;
}

int main() {
    CheckFirstKind();
    CheckSecondKind();
    CheckIntegerOrders();
    CheckValues();
    return 0;
}
