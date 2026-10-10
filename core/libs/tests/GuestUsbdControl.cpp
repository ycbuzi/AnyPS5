#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <libusb.h>

extern "C" {
std::int32_t APS5_VABI sceUsbdControlTransfer(libusb_device_handle*, std::uint8_t, std::uint8_t, std::uint16_t, std::uint16_t, unsigned char*, std::int32_t, std::uint32_t);
std::int32_t APS5_VABI sceUsbdGetStringDescriptor(libusb_device_handle*, std::uint8_t, std::uint16_t, unsigned char*, int);
}

namespace {

struct ControlCall {
    libusb_device_handle* handle;
    std::uint8_t requestType;
    std::uint8_t request;
    std::uint16_t value;
    std::uint16_t index;
    unsigned char* data;
    std::uint16_t length;
    unsigned int timeout;
};

ControlCall lastCall{};
int nativeResult = 0;
int nativeCalls = 0;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "USBD control: %s\n", message);
        std::abort();
    }
}

}

extern "C" int LIBUSB_CALL __wrap_libusb_control_transfer(libusb_device_handle* handle, std::uint8_t requestType, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::uint16_t length, unsigned int timeout) {
    lastCall = {handle, requestType, request, value, index, data, length, timeout};
    ++nativeCalls;
    if (nativeResult > 0) {
        Require(nativeResult <= length, "test backend result exceeds transfer length");
        for (int i = 0; i < nativeResult; ++i) data[i] = static_cast<unsigned char>(0x20 + i);
    }
    return nativeResult;
}

int main() {
    unsigned char handleStorage{};
    auto* handle = reinterpret_cast<libusb_device_handle*>(&handleStorage);
    unsigned char buffer[10];
    std::memset(buffer, 0x5a, sizeof(buffer));
    nativeResult = 5;
    Require(sceUsbdControlTransfer(handle, 0xc1, 0x37, 0x1234, 0xabcd, buffer + 1, 8, 1200) == 5, "byte count not preserved");
    Require(lastCall.handle == handle && lastCall.requestType == 0xc1 && lastCall.request == 0x37, "handle or request changed");
    Require(lastCall.value == 0x1234 && lastCall.index == 0xabcd && lastCall.timeout == 1200, "setup fields or timeout changed");
    Require(lastCall.data == buffer + 1 && lastCall.length == 8, "buffer or length changed");
    for (int i = 0; i < 5; ++i) Require(buffer[i + 1] == 0x20 + i, "native output did not reach guest buffer");
    Require(buffer[0] == 0x5a && buffer[6] == 0x5a && buffer[9] == 0x5a, "buffer canary overwritten");

    for (int error = -1; error >= -12; --error) {
        nativeResult = error;
        const auto expected = static_cast<std::int32_t>(0x80240000u - static_cast<std::uint32_t>(error));
        Require(sceUsbdControlTransfer(handle, 0x80, 0, 0, 0, buffer, sizeof(buffer), 0) == expected, "USB error mapping incorrect");
    }
    nativeResult = LIBUSB_ERROR_OTHER;
    Require(sceUsbdControlTransfer(handle, 0x80, 0, 0, 0, buffer, sizeof(buffer), 0) == static_cast<std::int32_t>(0x802400ff), "OTHER error mapping incorrect");
    nativeResult = 0;
    Require(sceUsbdControlTransfer(handle, 0x40, 0, 0, 0, nullptr, 0, 0) == 0, "zero length transfer rejected");
    Require(lastCall.data == nullptr && lastCall.length == 0, "zero length transfer changed");
    std::vector<unsigned char> maximumBuffer(UINT16_MAX);
    Require(sceUsbdControlTransfer(handle, 0x80, 0, 0, 0, maximumBuffer.data(), maximumBuffer.size(), 0) == 0, "maximum USB control length rejected");
    Require(lastCall.length == UINT16_MAX, "maximum USB control length truncated");

    nativeResult = 4;
    Require(sceUsbdGetStringDescriptor(handle, 7, 0x409, buffer + 1, 8) == 4, "string descriptor byte count changed");
    Require(lastCall.handle == handle && lastCall.requestType == 0x80 && lastCall.request == LIBUSB_REQUEST_GET_DESCRIPTOR, "string descriptor request incorrect");
    Require(lastCall.value == 0x307 && lastCall.index == 0x409 && lastCall.length == 8 && lastCall.data == buffer + 1, "string descriptor setup incorrect");
    nativeResult = LIBUSB_ERROR_PIPE;
    Require(sceUsbdGetStringDescriptor(handle, 7, 0x409, buffer, 8) == static_cast<std::int32_t>(0x80240009), "string descriptor error not mapped");

    const int beforeInvalidCalls = nativeCalls;
    constexpr auto invalidArgument = static_cast<std::int32_t>(0x80240002);
    Require(sceUsbdControlTransfer(nullptr, 0, 0, 0, 0, buffer, 8, 0) == invalidArgument, "null handle accepted");
    Require(sceUsbdControlTransfer(handle, 0, 0, 0, 0, nullptr, 1, 0) == invalidArgument, "null nonempty buffer accepted");
    Require(sceUsbdControlTransfer(handle, 0, 0, 0, 0, buffer, -1, 0) == invalidArgument, "negative transfer length accepted");
    Require(sceUsbdControlTransfer(handle, 0, 0, 0, 0, buffer, 65536, 0) == invalidArgument, "oversized transfer length accepted");
    Require(sceUsbdGetStringDescriptor(nullptr, 0, 0, buffer, 8) == invalidArgument, "null string descriptor handle accepted");
    Require(sceUsbdGetStringDescriptor(handle, 0, 0, nullptr, 8) == invalidArgument, "null string descriptor buffer accepted");
    Require(sceUsbdGetStringDescriptor(handle, 0, 0, buffer, -1) == invalidArgument, "negative descriptor length accepted");
    Require(sceUsbdGetStringDescriptor(handle, 0, 0, buffer, 65536) == invalidArgument, "oversized descriptor length accepted");
    Require(nativeCalls == beforeInvalidCalls, "invalid request reached native USB backend");
    std::puts("USBD control tests passed");
    return 0;
}
