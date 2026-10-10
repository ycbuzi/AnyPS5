#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
int APS5_VABI sceShareGetCurrentStatus(std::uint32_t feature_flag, void* status);
int APS5_VABI sceShareInitialize(std::size_t heap_size, int thread_priority, std::uint64_t affinity_mask);
int APS5_VABI sceShareTerminate(void);
}

namespace {

void Require(bool value) { if (!value) std::abort(); }

}

int main() {
    Require(sceShareInitialize(0, 0, 0) == 0);

    constexpr std::int32_t invalidParam = static_cast<std::int32_t>(0x81960002);
    std::uint8_t status[18];
    std::memset(status, 0x5a, sizeof(status));
    Require(sceShareGetCurrentStatus(1, status) == 0);
    for (std::size_t index = 0; index < sizeof(status); ++index) Require(status[index] == (index < 16 ? 0 : 0x5a));
    std::memset(status, 0x5a, sizeof(status));
    Require(sceShareGetCurrentStatus(0xffffffffu, status) == 0);
    Require(status[0] == 0 && status[15] == 0 && status[16] == 0x5a);
    std::memset(status, 0x5a, sizeof(status));
    Require(sceShareGetCurrentStatus(0, status) == invalidParam);
    Require(status[0] == 0x5a);
    Require(sceShareGetCurrentStatus(1, nullptr) == invalidParam);

    Require(sceShareTerminate() == 0);
    return 0;
}
