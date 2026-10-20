#include "ProcessParametersFixture.hpp"
#include "prx/libSceHmd/Hmd.hpp"
#include "prx/libSceHmd2/Hmd2.hpp"
#include "prx/libSceUserService/UserService.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

extern "C" {
int APS5_VABI sceHmd2GazeActivateGazeTracking();
int APS5_VABI sceHmd2ReprojectionGetCurrentOutput();
int APS5_VABI sceHmd2SetFieldOfView();
}
namespace {

alignas(16) std::array<std::byte, 0x40> process{};

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "HMD2: %s\n", message);
        std::abort();
    }
}

void InstallProcessParameters() {
    const std::uint64_t size = process.size();
    const std::uint32_t magic = 0x4942524f;
    std::memcpy(process.data(), &size, sizeof(size));
    std::memcpy(process.data() + 8, &magic, sizeof(magic));
    Require(ProcessParametersFixture::Install(process.data(), process.size()), "process parameters were not installed");
}

void TestBeforeInitialize() {
    Hmd2::DeviceInformation info;
    std::memset(&info, 0xcc, sizeof(info));
    Require(sceHmd2GetDeviceInformation(&info) == Hmd2::NotInitialized, "device information before initialization");
    const Hmd2::DeviceInformation cleared{};
    Require(std::memcmp(&info, &cleared, sizeof(info)) == 0, "device information was not cleared before the initialization check");
    Require(sceHmd2GetDeviceInformation(nullptr) == Hmd2::ParameterNull, "null device information before initialization");
    Hmd2::FieldOfView fieldOfView{};
    Require(sceHmd2GetFieldOfViewWithoutHandle(&fieldOfView) == Hmd2::NotInitialized, "field of view before initialization");
    Require(sceHmd2GetFieldOfView(0, &fieldOfView) == Hmd2::InvalidHandle, "field of view of handle 0 before initialization");
    Require(sceHmd2GetDeviceInformationByHandle(0, &info) == Hmd2::NotInitialized, "device information of handle 0 before initialization");
    Require(sceHmd2Open(USER_SERVICE_INITIAL_USER_ID, 0, 0, nullptr) == Hmd2::NotInitialized, "open before initialization");
    Require(sceHmd2Close(0) == Hmd2::NotInitialized, "close before initialization");
    Require(sceHmd2Terminate() == Hmd2::NotInitialized, "terminate before initialization");
    Require(sceHmd2SetVibration(0, 0) == 0, "vibration of the unopened handle 0 failed");
}

void TestWithoutHeadset() {
    const std::uint8_t param[16]{};
    Require(sceHmd2Initialize(param) == Hmd::UnsupportedFeature, "initialization without a headset did not fail");
    Require(sceHmd2Initialize(nullptr) == Hmd::UnsupportedFeature, "initialization without a param did not fail as unsupported");
    Require(sceHmd2Open(USER_SERVICE_INITIAL_USER_ID, 0, 0, nullptr) == Hmd2::NotInitialized, "open without a headset did not fail");
    Hmd2::DeviceInformation info;
    Require(sceHmd2GetDeviceInformation(&info) == Hmd2::NotInitialized, "device information without a headset");
    Require(sceHmd2Terminate() == Hmd2::NotInitialized, "terminate after the failed initialization");
}

void TestReprojection() {
    Require(sceHmd2ReprojectionEnableVrMode(Hmd2::OutputMode89_91Hz) == Hmd2::ReprojectionNotInitialized,
            "enabling VR mode without a session");
    Require(sceHmd2ReprojectionSetTiming(3000) == Hmd2::ReprojectionNotInitialized, "setting the timing without a session");
    const Hmd2::SizeAlign sizeAlign = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(sizeAlign.size == Hmd2::ReprojectionBufferSize && sizeAlign.align == Hmd2::ReprojectionBufferAlign,
            "the reprojection work size did not answer the console's requirement");
    alignas(Hmd2::ReprojectionBufferAlign) static std::uint8_t work[Hmd2::ReprojectionBufferSize]{};
    std::uint8_t display[64]{};
    std::uint8_t seeThrough[64]{};
    Hmd2::ReprojectionInitializeParam initParam{};
    Require(sceHmd2ReprojectionInitialize(nullptr) == Hmd2::ParameterNull, "initialization without a param");
    Require(sceHmd2ReprojectionInitialize(&initParam) == Hmd2::ParameterNull, "initialization without buffers");
    initParam.reprojectionBuffer = work;
    Require(sceHmd2ReprojectionInitialize(&initParam) == Hmd2::ParameterNull, "initialization without a display buffer");
    initParam.displayBuffer = display;
    initParam.seeThroughBuffer = seeThrough;
    bool threw = false;
    try {
        sceHmd2ReprojectionInitialize(&initParam);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "initialization with a see-through buffer did not throw");
    initParam.seeThroughBuffer = nullptr;
    initParam.reprojectionTiming = 0;
    Require(sceHmd2ReprojectionInitialize(&initParam) == 0, "initialization did not succeed");
    const auto* state = reinterpret_cast<const Hmd2::ReprojectionState*>(work);
    Require(state->outputMode == 0 && state->timingUs == 3000, "the initial state did not carry the default timing");
    Require(sceHmd2ReprojectionInitialize(&initParam) == Hmd2::ReprojectionAlreadyInitialized, "a second initialization");
    Require(sceHmd2ReprojectionSetTiming(2500) == 0, "setting the timing did not succeed");
    Require(state->timingUs == 2500, "the timing was not recorded in the work buffer");
    Require(sceHmd2ReprojectionSetTiming(2001) == 0 && state->timingUs == 2100, "the timing was not rounded up to hundreds");
    threw = false;
    try {
        sceHmd2ReprojectionSetTiming(1999);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "a timing below the range did not throw");
    threw = false;
    try {
        sceHmd2ReprojectionSetTiming(7001);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "a timing above the range did not throw");
    Require(sceHmd2ReprojectionEnableVrMode(0x2000C) == Hmd2::VideoOutNoDevice, "enabling VR mode did not report no device");
    Require(sceHmd2ReprojectionEnableVrMode(0x2000F) == Hmd2::VideoOutNoDevice, "enabling the second VR mode did not report no device");
    Require(sceHmd2ReprojectionEnableVrMode(0) == Hmd2::VideoOutInvalidValue, "an unknown output mode did not report invalid value");
    Require(state->outputMode == 0, "the output mode changed without a VR runtime");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(6, 0x100000001, 1) == Hmd2::ParameterInvalid,
            "a positional setting above the range did not report invalid parameter");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(-1, 0x100000001, 1) == Hmd2::ParameterInvalid,
            "a negative positional setting did not report invalid parameter");
    Hmd2::SizeAlign positional = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(positional.size == Hmd2::ReprojectionBufferSize && positional.align == Hmd2::ReprojectionBufferAlign,
            "a rejected positional setting changed the work-size answer");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(2, 0x100000001, 1) == 0,
            "the positional setting did not succeed");
    positional = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(positional.size == Hmd2::ReprojectionPositionalBufferSize + 2 * Hmd2::ReprojectionPositionalBufferSizeStep
                && positional.align == Hmd2::ReprojectionPositionalBufferAlign,
            "the positional work size did not follow the setting");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(1, 0, 0) == 0,
            "the lowest positional setting did not succeed");
    positional = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(positional.size == Hmd2::ReprojectionPositionalBufferSize + Hmd2::ReprojectionPositionalBufferSizeStep
                && positional.align == Hmd2::ReprojectionPositionalBufferAlign,
            "the lowest positional work size did not answer the console's requirement");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(5, 0, 0) == 0,
            "the highest positional setting did not succeed");
    positional = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(positional.size == Hmd2::ReprojectionPositionalBufferSize + 5 * Hmd2::ReprojectionPositionalBufferSizeStep
                && positional.align == Hmd2::ReprojectionPositionalBufferAlign,
            "the highest positional work size did not answer the console's requirement");
    Require(sceHmd2ReprojectionSetAllowPositionalReprojection(0, 0x100000001, 1) == 0,
            "clearing the positional setting did not succeed");
    const Hmd2::SizeAlign cleared = sceHmd2ReprojectionQueryBufferSizeAlign();
    Require(cleared.size == Hmd2::ReprojectionBufferSize && cleared.align == Hmd2::ReprojectionBufferAlign,
            "clearing the positional setting did not restore the work-size answer");
    Require(sceHmd2ReprojectionTerminate() == 0, "terminate did not succeed");
    Require(sceHmd2ReprojectionSetTiming(3000) == Hmd2::ReprojectionNotInitialized,
            "setting the timing after terminate");
    Require(sceHmd2ReprojectionEnableVrMode(Hmd2::OutputMode89_91Hz) == Hmd2::ReprojectionNotInitialized,
            "enabling VR mode after terminate");
    Require(sceHmd2ReprojectionInitialize(&initParam) == 0, "re-initialization after terminate");
    Require(state->outputMode == 0 && state->timingUs == Hmd2::ReprojectionTimingDefault,
            "the re-initialized state carries the default timing");
}

}

int main() {
    InstallProcessParameters();
    TestBeforeInitialize();
    TestWithoutHeadset();
    TestReprojection();
    bool threw = false;
    try {
        sceHmd2GazeActivateGazeTracking();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "gaze activation did not throw");
    threw = false;
    try {
        sceHmd2ReprojectionGetCurrentOutput();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "reprojection get current output did not throw");
    threw = false;
    try {
        sceHmd2SetFieldOfView();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "set field of view did not throw");
    std::puts("HMD2 tests passed");
    return 0;
}
