#ifndef CORE_LIBS_PRX_LIBSCEHMD2_HMD2_HPP
#define CORE_LIBS_PRX_LIBSCEHMD2_HMD2_HPP

#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstddef>
#include <cstdint>

namespace Hmd2 {

inline constexpr std::int32_t AlreadyInitialized = static_cast<std::int32_t>(0x8a720001);
inline constexpr std::int32_t NotInitialized = static_cast<std::int32_t>(0x8a720002);
inline constexpr std::int32_t InvalidHandle = static_cast<std::int32_t>(0x8a720003);
inline constexpr std::int32_t AlreadyOpened = static_cast<std::int32_t>(0x8a720006);
inline constexpr std::int32_t ParameterNull = static_cast<std::int32_t>(0x8a720008);
inline constexpr std::int32_t ParameterInvalid = static_cast<std::int32_t>(0x8a720009);
inline constexpr std::int32_t ReprojectionNotInitialized = static_cast<std::int32_t>(0x8a72000b);
inline constexpr std::int32_t ReprojectionAlreadyInitialized = static_cast<std::int32_t>(0x8a72000c);
inline constexpr std::int32_t VideoOutInvalidValue = static_cast<std::int32_t>(0x80290001);
inline constexpr std::int32_t VideoOutNoDevice = static_cast<std::int32_t>(0x80290018);

inline constexpr std::uint32_t UserIdSdkVersion = 0x07000000;

inline constexpr std::uint64_t OutputMode89_91Hz = 0x2000c;
inline constexpr std::uint64_t OutputMode119_88Hz = 0x2000f;
inline constexpr std::int32_t ReprojectionTimingMin = 2000;
inline constexpr std::int32_t ReprojectionTimingMax = 7000;
inline constexpr std::uint32_t ReprojectionTimingDefault = 3000;

inline constexpr std::uint64_t ReprojectionBufferSize = 0x100828;
inline constexpr std::uint64_t ReprojectionBufferAlign = 0x100;
inline constexpr std::int32_t AllowPositionalReprojectionMax = 5;
inline constexpr std::uint64_t ReprojectionPositionalBufferSize = 0x1103000;
inline constexpr std::uint64_t ReprojectionPositionalBufferSizeStep = 0x1000000;
inline constexpr std::uint64_t ReprojectionPositionalBufferAlign = 0x1000;

enum class DeviceStatus : std::uint32_t {
    NotDetected = 2,
};

struct DeviceInformation {
    DeviceStatus status;
    std::int32_t userId;
    std::uint32_t panelWidth;
    std::uint32_t panelHeight;
    std::uint16_t latency90Hz;
    std::uint16_t latency120Hz;
    std::uint8_t hmuMount;
    std::uint8_t lensSeparationDistance;
    std::uint8_t reserved[2];
    float virtualImageDistance;
};

struct FieldOfView {
    float tanOut;
    float tanIn;
    float tanTop;
    float tanBottom;
};

static_assert(sizeof(DeviceInformation) == 28);
static_assert(offsetof(DeviceInformation, userId) == 4);
static_assert(offsetof(DeviceInformation, panelWidth) == 8);
static_assert(offsetof(DeviceInformation, latency90Hz) == 16);
static_assert(offsetof(DeviceInformation, hmuMount) == 20);
static_assert(offsetof(DeviceInformation, virtualImageDistance) == 24);
static_assert(sizeof(FieldOfView) == 16);

struct ReprojectionState {
    std::uint64_t outputMode;
    std::uint32_t timingUs;
};

struct SizeAlign {
    std::uint64_t size;
    std::uint64_t align;
};

struct ReprojectionInitializeParam {
    void* reprojectionBuffer;
    void* displayBuffer;
    std::int32_t threadPriority;
    std::int32_t cpuAffinityMask;
    std::uint32_t pipeId;
    std::uint32_t queueId;
    std::uint32_t type;
    void* seeThroughBuffer;
    std::int32_t reprojectionTiming;
    std::int32_t reserved[5];
};

static_assert(sizeof(ReprojectionState) == 16);
static_assert(alignof(ReprojectionState) == 8);
static_assert(offsetof(ReprojectionState, timingUs) == 8);
static_assert(sizeof(SizeAlign) == 16);
static_assert(alignof(SizeAlign) == 8);
static_assert(sizeof(ReprojectionInitializeParam) == 72);
static_assert(alignof(ReprojectionInitializeParam) == 8);
static_assert(offsetof(ReprojectionInitializeParam, threadPriority) == 16);
static_assert(offsetof(ReprojectionInitializeParam, type) == 32);
static_assert(offsetof(ReprojectionInitializeParam, seeThroughBuffer) == 40);
static_assert(offsetof(ReprojectionInitializeParam, reprojectionTiming) == 48);
static_assert(offsetof(ReprojectionInitializeParam, reserved) == 52);

}

extern "C" {
std::int32_t APS5_VABI sceHmd2Initialize(const void* param);
std::int32_t APS5_VABI sceHmd2Terminate();
std::int32_t APS5_VABI sceHmd2Open(std::int32_t userId, std::int32_t type, std::int32_t index, const void* param);
std::int32_t APS5_VABI sceHmd2Close(std::int32_t handle);
std::int32_t APS5_VABI sceHmd2GetDeviceInformation(Hmd2::DeviceInformation* info);
std::int32_t APS5_VABI sceHmd2GetDeviceInformationByHandle(std::int32_t handle, Hmd2::DeviceInformation* info);
std::int32_t APS5_VABI sceHmd2GetFieldOfView(std::int32_t handle, Hmd2::FieldOfView* fieldOfView);
std::int32_t APS5_VABI sceHmd2GetFieldOfViewWithoutHandle(Hmd2::FieldOfView* fieldOfView);
std::int32_t APS5_VABI sceHmd2SetVibration(std::int32_t handle, std::uint32_t level);
std::int32_t APS5_VABI sceHmd2ReprojectionInitialize(const Hmd2::ReprojectionInitializeParam* param);
Hmd2::SizeAlign APS5_VABI sceHmd2ReprojectionQueryBufferSizeAlign();
std::int32_t APS5_VABI sceHmd2ReprojectionSetAllowPositionalReprojection(std::int32_t mode, std::uint64_t param, std::int32_t flags);
std::int32_t APS5_VABI sceHmd2ReprojectionSetTiming(std::int32_t timing);
std::int32_t APS5_VABI sceHmd2ReprojectionEnableVrMode(std::uint64_t outputMode);
std::int32_t APS5_VABI sceHmd2ReprojectionTerminate();
}

#endif
