#include "prx/libc/include/general/VabiMacros.hpp"
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>

extern "C" {
double APS5_VABI logb_nid_postfix(double);
float APS5_VABI sinhf_nid_postfix(float);
float APS5_VABI nearbyintf_nid_postfix(float);
float APS5_VABI __powisf2_nid_postfix(float, int);
}

static void Require(bool value) { if (!value) std::abort(); }

static void CheckLogbSinhfNearbyintf() {
    const auto infinity = std::numeric_limits<double>::infinity();
    Require(logb_nid_postfix(8.) == 3. && logb_nid_postfix(-0.75) == -1.);
    Require(logb_nid_postfix(std::numeric_limits<double>::denorm_min()) == -1074.);
    Require(logb_nid_postfix(std::numeric_limits<double>::max()) == 1023.);
    Require(logb_nid_postfix(0.) == -infinity && logb_nid_postfix(-0.) == -infinity);
    Require(logb_nid_postfix(-infinity) == infinity);
    Require(std::isnan(logb_nid_postfix(std::numeric_limits<double>::quiet_NaN())));
    const auto infinityf = std::numeric_limits<float>::infinity();
    Require(sinhf_nid_postfix(0.f) == 0.f && !std::signbit(sinhf_nid_postfix(0.f)));
    Require(sinhf_nid_postfix(-0.f) == 0.f && std::signbit(sinhf_nid_postfix(-0.f)));
    Require(std::abs(sinhf_nid_postfix(1.f) - 1.1752012f) < 0.000001f);
    Require(std::abs(sinhf_nid_postfix(-2.f) + 3.6268604f) < 0.000001f);
    Require(sinhf_nid_postfix(100.f) == infinityf && sinhf_nid_postfix(-100.f) == -infinityf);
    Require(sinhf_nid_postfix(infinityf) == infinityf && sinhf_nid_postfix(-infinityf) == -infinityf);
    Require(std::isnan(sinhf_nid_postfix(std::numeric_limits<float>::quiet_NaN())));
    std::feclearexcept(FE_ALL_EXCEPT);
    Require(nearbyintf_nid_postfix(2.5f) == 2.f && nearbyintf_nid_postfix(3.5f) == 4.f);
    Require(nearbyintf_nid_postfix(-2.5f) == -2.f && nearbyintf_nid_postfix(1.4f) == 1.f);
    Require(nearbyintf_nid_postfix(-0.4f) == 0.f && std::signbit(nearbyintf_nid_postfix(-0.4f)));
    Require(nearbyintf_nid_postfix(8388609.f) == 8388609.f);
    Require(std::fetestexcept(FE_INEXACT) == 0);
    Require(nearbyintf_nid_postfix(-infinityf) == -infinityf);
    Require(std::isnan(nearbyintf_nid_postfix(std::numeric_limits<float>::quiet_NaN())));
    const int rounding = std::fegetround();
    std::fesetround(FE_UPWARD);
    Require(nearbyintf_nid_postfix(1.1f) == 2.f && nearbyintf_nid_postfix(-1.9f) == -1.f);
    std::fesetround(FE_TOWARDZERO);
    Require(nearbyintf_nid_postfix(1.9f) == 1.f && nearbyintf_nid_postfix(-1.9f) == -1.f);
    std::fesetround(rounding);
}

static void CheckPowisf2() {
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto maximum = std::numeric_limits<float>::max();
    const auto minimum = std::numeric_limits<int>::min();
    Require(__powisf2_nid_postfix(2.f, 0) == 1.f && __powisf2_nid_postfix(0.f, 0) == 1.f);
    Require(__powisf2_nid_postfix(std::numeric_limits<float>::quiet_NaN(), 0) == 1.f);
    Require(__powisf2_nid_postfix(infinity, 0) == 1.f);
    Require(__powisf2_nid_postfix(2.f, 1) == 2.f && __powisf2_nid_postfix(2.f, 10) == 1024.f);
    Require(__powisf2_nid_postfix(-2.f, 3) == -8.f && __powisf2_nid_postfix(-2.f, 4) == 16.f);
    Require(__powisf2_nid_postfix(2.f, -1) == 0.5f && __powisf2_nid_postfix(2.f, -3) == 0.125f);
    Require(__powisf2_nid_postfix(-2.f, -1) == -0.5f);
    Require(__powisf2_nid_postfix(0.f, -1) == infinity && __powisf2_nid_postfix(-0.f, -1) == -infinity);
    Require(__powisf2_nid_postfix(infinity, -1) == 0.f && __powisf2_nid_postfix(infinity, 3) == infinity);
    Require(std::isnan(__powisf2_nid_postfix(std::numeric_limits<float>::quiet_NaN(), 2)));
    Require(__powisf2_nid_postfix(1.f, minimum) == 1.f && __powisf2_nid_postfix(-1.f, minimum) == 1.f);
    Require(__powisf2_nid_postfix(-1.f, std::numeric_limits<int>::max()) == -1.f);
    Require(__powisf2_nid_postfix(2.f, minimum) == 0.f && __powisf2_nid_postfix(0.5f, minimum) == infinity);
    Require(__powisf2_nid_postfix(2.f, 127) == 0x1p127f && __powisf2_nid_postfix(2.f, -126) == 0x1p-126f);
    Require(__powisf2_nid_postfix(2.f, 128) == infinity);
    for (const int exponent : {1, -1}) {
        std::feclearexcept(FE_ALL_EXCEPT);
        const float result = __powisf2_nid_postfix(maximum, exponent);
        Require(result == (exponent == 1 ? maximum : 1.f / maximum));
        Require(std::fetestexcept(FE_OVERFLOW) == 0);
    }
    std::feclearexcept(FE_ALL_EXCEPT);
    Require(__powisf2_nid_postfix(0x1p100f, 2) == infinity && std::fetestexcept(FE_OVERFLOW) != 0);
}

int main() {
    CheckLogbSinhfNearbyintf();
    CheckPowisf2();
}
