#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/Shutdown.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

extern "C" {
std::uint32_t APS5_VABI sceAgcDcbBeginOcclusionQueryGetSize(std::uint32_t);
std::uint32_t APS5_VABI sceAgcDcbEndOcclusionQueryGetSize();
std::uint32_t APS5_VABI sceAgcDcbSetIndexIndirectArgsGetSize();
}

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main() {
    try {
        check(sceAgcDcbBeginOcclusionQueryGetSize(0) == 16, "begin occlusion query size for type 0");
        check(sceAgcDcbBeginOcclusionQueryGetSize(1) == 288, "begin occlusion query size for type 1");
        for (const auto mode : std::array<std::uint32_t, 5>{2, 255, 256, 257, 0xffffffffu}) {
            bool rejected = false;
            try {
                static_cast<void>(sceAgcDcbBeginOcclusionQueryGetSize(mode));
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            check(rejected, "unknown occlusion query type was accepted");
        }
        check(sceAgcDcbEndOcclusionQueryGetSize() == 16, "end occlusion query size");
        check(sceAgcDcbSetIndexIndirectArgsGetSize() == 16, "index indirect args size");
        LibcRunShutdown_nid_postfix();
        std::puts("AGC draw packet size tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
