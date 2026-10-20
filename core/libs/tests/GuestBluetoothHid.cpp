#include "prx/libc/include/general/VabiMacros.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
std::int32_t APS5_VABI sceBluetoothHidInit();
std::int32_t APS5_VABI sceBluetoothHidRegisterCallback(void* callback, void* userData, const void* param);
std::int32_t APS5_VABI sceBluetoothHidUnregisterCallback();
std::int32_t APS5_VABI sceBluetoothHidRegisterDevice(std::uint16_t vendorId, std::uint16_t productId);
std::int32_t APS5_VABI sceBluetoothHidUnregisterDevice(std::uint16_t vendorId, std::uint16_t productId);
std::int32_t APS5_VABI sceBluetoothHidGetDeviceName(std::uint64_t device, char* name);
std::int32_t APS5_VABI sceBluetoothHidGetInputReport(std::uint64_t device, std::uint8_t reportId);
std::int32_t APS5_VABI sceBluetoothHidSetOutputReport(std::uint64_t device, std::uint8_t reportId, const std::uint8_t* data, std::uint64_t size);
}

namespace {

constexpr std::int32_t InvalidArgument = static_cast<std::int32_t>(0x81540001);
constexpr std::int32_t NotInitialized = static_cast<std::int32_t>(0x81540004);
constexpr std::int32_t UnknownDevice = static_cast<std::int32_t>(0x81540005);
constexpr std::int32_t NoCallback = static_cast<std::int32_t>(0x81540006);
constexpr std::int32_t ReportIdMismatch = static_cast<std::int32_t>(0x8154000b);
constexpr std::int32_t DeviceNotConnected = static_cast<std::int32_t>(0x8154000d);
constexpr std::int32_t PriorityUnsupported = static_cast<std::int32_t>(0x8002002d);
constexpr std::uint64_t Device = 0x0000123456789abcull;
constexpr std::uint64_t FlaggedDevice = Device | (1ull << 48);

struct ThreadParam {
    std::int32_t priority;
    std::uint64_t affinityMask;
    std::uint32_t reserved;
};

struct Param {
    const ThreadParam* threadParam;
    std::uint32_t reserved;
};

int callbackCalls = 0;

void APS5_VABI Callback(void*, void*) {
    ++callbackCalls;
}

void* CallbackPointer() {
    return reinterpret_cast<void*>(&Callback);
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "BluetoothHid: %s\n", message);
        std::abort();
    }
}

void TestBeforeInit() {
    std::array<char, 8> name{};
    const std::uint8_t data[8]{1};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, nullptr) == NotInitialized, "callback registration before init");
    Require(sceBluetoothHidUnregisterCallback() == NotInitialized, "callback unregistration before init");
    Require(sceBluetoothHidRegisterDevice(0x46d, 0xc24f) == NotInitialized, "device registration before init");
    Require(sceBluetoothHidUnregisterDevice(0x46d, 0xc24f) == NotInitialized, "device unregistration before init");
    Require(sceBluetoothHidGetDeviceName(Device, name.data()) == NotInitialized, "device name before init");
    Require(sceBluetoothHidGetInputReport(Device, 1) == NotInitialized, "input report before init");
    Require(sceBluetoothHidSetOutputReport(Device, 1, data, sizeof(data)) == NotInitialized, "output report before init");
}

void TestWithoutCallback() {
    std::array<char, 8> name{};
    const std::uint8_t data[8]{1};
    Require(sceBluetoothHidRegisterDevice(0x46d, 0xc24f) == NoCallback, "device registration without a callback");
    Require(sceBluetoothHidUnregisterDevice(0x46d, 0xc24f) == 0, "device unregistration without a callback");
    Require(sceBluetoothHidUnregisterCallback() == NoCallback, "callback unregistration without a callback");
    Require(sceBluetoothHidGetInputReport(Device, 1) == NoCallback, "input report without a callback");
    Require(sceBluetoothHidSetOutputReport(Device, 1, data, sizeof(data)) == NoCallback, "output report without a callback");
    Require(sceBluetoothHidGetDeviceName(Device, name.data()) == UnknownDevice, "device name without a callback");
}

void TestRegisterCallback() {
    Require(sceBluetoothHidRegisterCallback(nullptr, nullptr, nullptr) == InvalidArgument, "null callback");
    const Param reservedParam{nullptr, 1};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &reservedParam) == InvalidArgument, "param with a reserved value");
    const ThreadParam reservedThread{700, 1, 1};
    const Param reservedThreadParam{&reservedThread, 0};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &reservedThreadParam) == InvalidArgument, "thread param with a reserved value");
    const ThreadParam lowPriority{255, 1, 0};
    const Param lowPriorityParam{&lowPriority, 0};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &lowPriorityParam) == PriorityUnsupported, "priority 255");
    const ThreadParam highPriority{768, 1, 0};
    const Param highPriorityParam{&highPriority, 0};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &highPriorityParam) == PriorityUnsupported, "priority 768");
    Require(sceBluetoothHidUnregisterCallback() == NoCallback, "a rejected registration kept the callback");

    const ThreadParam thread{700, 0x7f, 0};
    const Param param{&thread, 0};
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &param) == 0, "registration with a thread param failed");
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), reinterpret_cast<void*>(0x1234), nullptr) == 0, "replacing the callback failed");
    Require(sceBluetoothHidRegisterCallback(CallbackPointer(), nullptr, &lowPriorityParam) == PriorityUnsupported, "priority 255 while registered");
}

void TestNoDevice() {
    Require(sceBluetoothHidRegisterDevice(0x46d, 0xc24f) == 0, "device registration failed");
    Require(sceBluetoothHidRegisterDevice(0x46d, 0xc24f) == 0, "second device registration failed");
    Require(sceBluetoothHidUnregisterDevice(0x46d, 0xc24f) == 0, "device unregistration failed");

    std::array<char, 256> name;
    name.fill('\xcc');
    Require(sceBluetoothHidGetDeviceName(Device, name.data()) == UnknownDevice && name[0] == '\xcc', "name of an unknown device");
    Require(sceBluetoothHidGetDeviceName(Device, nullptr) == InvalidArgument, "null device name");
    Require(sceBluetoothHidGetDeviceName(0, name.data()) == 0, "name of device 0 failed");
    for (std::size_t index = 0; index < name.size(); ++index)
        Require(name[index] == (index < 121 ? '\0' : '\xcc'), "device 0 did not clear exactly 121 bytes");

    Require(sceBluetoothHidGetInputReport(Device, 1) == DeviceNotConnected, "input report of an unknown device");
    Require(sceBluetoothHidGetInputReport(0, 0) == DeviceNotConnected, "input report of device 0");
    const std::uint8_t data[8]{1, 2, 3, 4, 5, 6, 7, 8};
    Require(sceBluetoothHidSetOutputReport(Device, 1, data, sizeof(data)) == DeviceNotConnected, "output report of an unknown device");
    Require(sceBluetoothHidSetOutputReport(Device, 1, nullptr, sizeof(data)) == InvalidArgument, "output report without data");
    Require(sceBluetoothHidSetOutputReport(Device, 1, data, 0) == InvalidArgument, "empty output report");
    Require(sceBluetoothHidSetOutputReport(FlaggedDevice, 1, data, sizeof(data)) == DeviceNotConnected, "output report with its id in the data");
    Require(sceBluetoothHidSetOutputReport(FlaggedDevice, 2, data, sizeof(data)) == ReportIdMismatch, "mismatched report id in the data");
    Require(sceBluetoothHidSetOutputReport(FlaggedDevice, 1, data, 1) == InvalidArgument, "report id without data");
    Require(callbackCalls == 0, "a device event was reported");
}

}

int main() {
    TestBeforeInit();
    Require(sceBluetoothHidInit() == 0, "initialization failed");
    Require(sceBluetoothHidInit() == 0, "second initialization failed");
    TestWithoutCallback();
    TestRegisterCallback();
    TestNoDevice();
    Require(sceBluetoothHidUnregisterCallback() == 0, "callback unregistration failed");
    Require(sceBluetoothHidUnregisterCallback() == NoCallback, "second callback unregistration");
    Require(sceBluetoothHidRegisterDevice(0x46d, 0xc24f) == NoCallback, "device registration after unregistering the callback");
    std::puts("BluetoothHid tests passed");
    return 0;
}
