#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

#include <cstring>
#include <mutex>

extern "C" {
int APS5_VABI scePthreadAttrInit(void** attr);
int APS5_VABI scePthreadAttrDestroy(void** attr);
int APS5_VABI scePthreadAttrSetaffinity(void** attr, std::uint64_t mask);
int APS5_VABI scePthreadAttrSetschedparam(void** attr, const std::int32_t* priority);
}

namespace BluetoothHid {

struct ThreadParam {
    std::int32_t priority;
    std::uint64_t affinityMask;
    std::uint32_t reserved;
};

struct Param {
    const ThreadParam* threadParam;
    std::uint32_t reserved;
};

static_assert(sizeof(ThreadParam) == 24);
static_assert(offsetof(ThreadParam, affinityMask) == 8);
static_assert(offsetof(ThreadParam, reserved) == 16);
static_assert(sizeof(Param) == 16);

}

namespace {

constexpr std::int32_t InvalidArgument = static_cast<std::int32_t>(0x81540001);
constexpr std::int32_t NotInitialized = static_cast<std::int32_t>(0x81540004);
constexpr std::int32_t UnknownDevice = static_cast<std::int32_t>(0x81540005);
constexpr std::int32_t NoCallback = static_cast<std::int32_t>(0x81540006);
constexpr std::int32_t ReportIdMismatch = static_cast<std::int32_t>(0x8154000b);
constexpr std::int32_t DeviceNotConnected = static_cast<std::int32_t>(0x8154000d);

constexpr std::uint64_t ReportIdInDataFlag = 1ull << 48;
constexpr std::size_t EmptyDeviceNameSize = 121;

std::mutex stateMutex;
bool initialized = false;
void* registeredCallback = nullptr;

std::int32_t CheckThreadParam(const BluetoothHid::ThreadParam& threadParam) {
    void* attr = nullptr;
    if (const auto result = scePthreadAttrInit(&attr); result != 0) return result;
    auto result = scePthreadAttrSetaffinity(&attr, threadParam.affinityMask);
    if (result == 0) result = scePthreadAttrSetschedparam(&attr, &threadParam.priority);
    scePthreadAttrDestroy(&attr);
    return result;
}

}

extern "C" {

std::int32_t APS5_VABI sceBluetoothHidInit() {
    std::lock_guard lock(stateMutex);
    initialized = true;
    return 0;
}

std::int32_t APS5_VABI sceBluetoothHidRegisterCallback(void* callback, void* userData, const BluetoothHid::Param* param) {
    (void)userData;
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (!callback) return InvalidArgument;
    if (param) {
        if (param->reserved != 0) return InvalidArgument;
        if (param->threadParam) {
            if (param->threadParam->reserved != 0) return InvalidArgument;
            if (const auto result = CheckThreadParam(*param->threadParam); result != 0) return result;
        }
    }
    registeredCallback = callback;
    return 0;
}

std::int32_t APS5_VABI sceBluetoothHidUnregisterCallback() {
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (!registeredCallback) return NoCallback;
    registeredCallback = nullptr;
    return 0;
}

std::int32_t APS5_VABI sceBluetoothHidRegisterDevice(std::uint16_t vendorId, std::uint16_t productId) {
    (void)vendorId;
    (void)productId;
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (!registeredCallback) return NoCallback;
    return 0;
}

std::int32_t APS5_VABI sceBluetoothHidUnregisterDevice(std::uint16_t vendorId, std::uint16_t productId) {
    (void)vendorId;
    (void)productId;
    std::lock_guard lock(stateMutex);
    return initialized ? 0 : NotInitialized;
}

std::int32_t APS5_VABI sceBluetoothHidGetDeviceName(std::uint64_t device, char* name) {
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (!name) return InvalidArgument;
    if (device != 0) return UnknownDevice;
    std::memset(name, 0, EmptyDeviceNameSize);
    return 0;
}

std::int32_t APS5_VABI sceBluetoothHidGetInputReport(std::uint64_t device, std::uint8_t reportId) {
    (void)device;
    (void)reportId;
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (!registeredCallback) return NoCallback;
    return DeviceNotConnected;
}

std::int32_t APS5_VABI sceBluetoothHidSetOutputReport(std::uint64_t device, std::uint8_t reportId, const std::uint8_t* data, std::uint64_t size) {
    std::lock_guard lock(stateMutex);
    if (!initialized) return NotInitialized;
    if (device & ReportIdInDataFlag) {
        if (!data || size < 2) return InvalidArgument;
        if (data[0] != reportId) return ReportIdMismatch;
    } else if (!data || size == 0) {
        return InvalidArgument;
    }
    if (!registeredCallback) return NoCallback;
    return DeviceNotConnected;
}

}
