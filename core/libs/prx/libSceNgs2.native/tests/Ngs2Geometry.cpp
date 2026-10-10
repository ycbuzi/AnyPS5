#include "prx/libSceNgs2.native/include/Ngs2Types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern "C" {
int APS5_VABI sceNgs2GeomResetListenerParam(Ngs2GeomListenerParam*);
int APS5_VABI sceNgs2GeomResetSourceParam(Ngs2GeomSourceParam*);
}

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template <typename TParam, std::size_t N, typename TReset>
static void TestReset(TReset reset, const std::array<std::uint32_t, N>& expected) {
    struct Guarded {
        std::array<unsigned char, 16> before;
        TParam param;
        std::array<unsigned char, 16> after;
    };
    static_assert(sizeof(TParam) == N * sizeof(std::uint32_t));
    static_assert(offsetof(Guarded, param) == 16);
    static_assert(offsetof(Guarded, after) == 16 + N * sizeof(std::uint32_t));
    Guarded storage{};
    for (const auto poison : {0xa5, 0x5a}) {
        std::memset(&storage, poison, sizeof(storage));
        Check(reset(&storage.param) == SCE_NGS2_OK, "reset failed");
        Check(std::memcmp(&storage.param, expected.data(), sizeof(TParam)) == 0, "reset output differs from the reference bytes");
        Check(std::all_of(storage.before.begin(), storage.before.end(), [poison](auto byte) { return byte == poison; }), "reset overwrote bytes before the parameter");
        Check(std::all_of(storage.after.begin(), storage.after.end(), [poison](auto byte) { return byte == poison; }), "reset overwrote bytes after the parameter");
        Check(reset(&storage.param) == SCE_NGS2_OK, "repeated reset failed");
        Check(std::memcmp(&storage.param, expected.data(), sizeof(TParam)) == 0, "repeated reset changed the defaults");
    }
    bool rejected = false;
    try {
        reset(nullptr);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "null output was not rejected");
}

int main() {
    try {
        constexpr std::array<std::uint32_t, 15> listener{
            0, 0, 0,
            0, 0, 0x3f800000,
            0, 0x3f800000, 0,
            0, 0, 0,
            0x43ab8000, 0, 0
        };
        constexpr std::array<std::uint32_t, 27> source{
            0, 0, 0,
            0, 0, 0,
            0, 0, 0x3f800000,
            0x3f800000, 0x43b40000, 0x3f800000, 0x43b40000,
            0, 0x49742400, 0x3f800000, 0x3f800000,
            0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
            0, 0, 2, 2, 0, 0
        };
        TestReset<Ngs2GeomListenerParam>(sceNgs2GeomResetListenerParam, listener);
        TestReset<Ngs2GeomSourceParam>(sceNgs2GeomResetSourceParam, source);
        std::puts("NGS2 geometry reset tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "NGS2 geometry reset test failed: %s\n", error.what());
        return 1;
    }
}
