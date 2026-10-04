#include "SceTypes.hpp"
#include <cstdint>
#include <cstdlib>

extern "C" {
int APS5_VABI sceVoiceInit(VoiceInitParam* param, std::int32_t version);
int APS5_VABI sceVoiceCreatePort(std::uint32_t* port_id, const VoicePortParam* param);
int APS5_VABI sceVoiceWriteToIPort(std::uint32_t input_port_id, const void* data, std::uint32_t* size, std::int16_t frame_gaps);
}

namespace {

void Require(bool value) { if (!value) std::abort(); }

constexpr std::int32_t kPortInPcm = 1;

}

int main() {
    VoiceInitParam init{};
    Require(sceVoiceInit(&init, 100) == 0);

    VoicePortParam param{};
    param.port_type = kPortInPcm;
    std::uint32_t port = 0;
    Require(sceVoiceCreatePort(&port, &param) == 0);

    std::uint8_t data[64] = {};
    std::uint32_t size = sizeof(data);
    Require(sceVoiceWriteToIPort(port, data, &size, 0) == 0);
    Require(size == sizeof(data));
}
