#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <libusb.h>
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t SCE_USBD_ERROR_INVALID_ARG = static_cast<std::int32_t>(0x80240002);

struct UsbdTimeval {
    std::int64_t seconds;
    std::int64_t microseconds;
};

using UsbdTransferCallback = void (APS5_VABI *)(libusb_transfer*);

std::mutex contextMutex;
std::shared_ptr<libusb_context> context;
std::size_t initializationCount = 0;

std::int32_t ConvertError(int result) {
    if (result >= 0) return result;
    if (result == LIBUSB_ERROR_OTHER) return static_cast<std::int32_t>(0x802400ff);
    return static_cast<std::int32_t>(0x80240000u - static_cast<std::uint32_t>(result));
}

std::shared_ptr<libusb_context> GetContext() {
    std::lock_guard lock(contextMutex);
    if (!context) throw std::runtime_error("libSceUsbd: library is not initialized");
    return context;
}

}

extern "C" {

std::int32_t APS5_VABI sceUsbdInit() {
    std::lock_guard lock(contextMutex);
    if (context) {
        ++initializationCount;
        return 0;
    }
    libusb_context* created = nullptr;
    const int result = libusb_init_context(&created, nullptr, 0);
    if (result < 0) return ConvertError(result);
    context = std::shared_ptr<libusb_context>(created, libusb_exit);
    initializationCount = 1;
    return 0;
}

void APS5_VABI sceUsbdExit() {
    std::shared_ptr<libusb_context> released;
    {
        std::lock_guard lock(contextMutex);
        if (initializationCount != 0 && --initializationCount == 0) released.swap(context);
    }
}

std::int64_t APS5_VABI sceUsbdGetDeviceList(libusb_device*** list) {
    if (list == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    const auto current = GetContext();
    const auto result = libusb_get_device_list(current.get(), list);
    return result < 0 ? ConvertError(static_cast<int>(result)) : result;
}

void APS5_VABI sceUsbdFreeDeviceList(libusb_device** list, std::int32_t unrefDevices) {
    libusb_free_device_list(list, unrefDevices);
}

std::int32_t APS5_VABI sceUsbdHandleEventsTimeout(const UsbdTimeval* timeout) {
    if (timeout == nullptr || timeout->seconds < 0 || timeout->microseconds < 0 || timeout->microseconds >= 1000000) return SCE_USBD_ERROR_INVALID_ARG;
    timeval native{};
    if (timeout->seconds > std::numeric_limits<decltype(native.tv_sec)>::max()) {
        throw std::runtime_error("sceUsbdHandleEventsTimeout: timeout exceeds host timeval range");
    }
    native.tv_sec = static_cast<decltype(native.tv_sec)>(timeout->seconds);
    native.tv_usec = static_cast<decltype(native.tv_usec)>(timeout->microseconds);
    const auto current = GetContext();
    return ConvertError(libusb_handle_events_timeout(current.get(), &native));
}

libusb_transfer* APS5_VABI sceUsbdAllocTransfer(int isoPackets) {
    if (isoPackets < 0) return nullptr;
    return libusb_alloc_transfer(isoPackets);
}

std::int32_t APS5_VABI sceUsbdAttachKernelDriver(libusb_device_handle* handle, int interfaceNumber) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_attach_kernel_driver(handle, interfaceNumber));
}

int APS5_VABI sceUsbdCancelTransfer(libusb_transfer* transfer) {
    (void)transfer;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceUsbdCheckConnected(libusb_device_handle* handle) {
    (void)handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

std::int32_t APS5_VABI sceUsbdClaimInterface(libusb_device_handle* handle, int interfaceNumber) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_claim_interface(handle, interfaceNumber));
}

void APS5_VABI sceUsbdClose(libusb_device_handle* handle) {
    libusb_close(handle);
}

std::int32_t APS5_VABI sceUsbdControlTransfer(libusb_device_handle* handle, std::uint8_t requestType, std::uint8_t request, std::uint16_t value, std::uint16_t index, unsigned char* data, std::int32_t length, std::uint32_t timeout) {
    if (handle == nullptr || length < 0 || length > UINT16_MAX || (length != 0 && data == nullptr)) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_control_transfer(handle, requestType, request, value, index, data, static_cast<std::uint16_t>(length), timeout));
}

std::int32_t APS5_VABI sceUsbdEventHandlingOk() {
    const auto current = GetContext();
    return libusb_event_handling_ok(current.get());
}

void APS5_VABI sceUsbdFillInterruptTransfer(libusb_transfer* transfer, libusb_device_handle* handle, std::uint8_t endpoint, unsigned char* buffer, int length, UsbdTransferCallback callback, void* userData, std::uint32_t timeout) {
    if (transfer == nullptr) APS5_INVALID_ARG_EX;
    libusb_fill_interrupt_transfer(transfer, handle, endpoint, buffer, length, reinterpret_cast<libusb_transfer_cb_fn>(callback), userData, timeout);
}

void APS5_VABI sceUsbdFreeConfigDescriptor(libusb_config_descriptor* descriptor) {
    libusb_free_config_descriptor(descriptor);
}

void APS5_VABI sceUsbdFreeTransfer(libusb_transfer* transfer) {
    libusb_free_transfer(transfer);
}

std::int32_t APS5_VABI sceUsbdGetActiveConfigDescriptor(libusb_device* device, libusb_config_descriptor** config) {
    if (device == nullptr || config == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_get_active_config_descriptor(device, config));
}

std::uint8_t APS5_VABI sceUsbdGetBusNumber(libusb_device* device) {
    if (device == nullptr) APS5_INVALID_ARG_EX;
    return libusb_get_bus_number(device);
}

std::int32_t APS5_VABI sceUsbdGetConfigDescriptor(libusb_device* device, std::uint8_t index, libusb_config_descriptor** config) {
    if (device == nullptr || config == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_get_config_descriptor(device, index, config));
}

std::uint8_t APS5_VABI sceUsbdGetDeviceAddress(libusb_device* device) {
    if (device == nullptr) APS5_INVALID_ARG_EX;
    return libusb_get_device_address(device);
}

std::int32_t APS5_VABI sceUsbdGetDeviceDescriptor(libusb_device* device, libusb_device_descriptor* descriptor) {
    if (device == nullptr || descriptor == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_get_device_descriptor(device, descriptor));
}

std::int32_t APS5_VABI sceUsbdGetStringDescriptor(libusb_device_handle* handle, std::uint8_t index, std::uint16_t language, unsigned char* data, int length) {
    if (handle == nullptr || data == nullptr || length < 0 || length > UINT16_MAX) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_get_string_descriptor(handle, index, language, data, length));
}

std::int32_t APS5_VABI sceUsbdKernelDriverActive(libusb_device_handle* handle, int interfaceNumber) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_kernel_driver_active(handle, interfaceNumber));
}

std::int32_t APS5_VABI sceUsbdOpen(libusb_device* device, libusb_device_handle** handle) {
    if (device == nullptr || handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_open(device, handle));
}

libusb_device* APS5_VABI sceUsbdRefDevice(libusb_device* device) {
    if (device == nullptr) return nullptr;
    return libusb_ref_device(device);
}

std::int32_t APS5_VABI sceUsbdReleaseInterface(libusb_device_handle* handle, int interfaceNumber) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_release_interface(handle, interfaceNumber));
}

std::int32_t APS5_VABI sceUsbdResetDevice(libusb_device_handle* handle) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_reset_device(handle));
}

std::int32_t APS5_VABI sceUsbdSetConfiguration(libusb_device_handle* handle, int configuration) {
    if (handle == nullptr) return SCE_USBD_ERROR_INVALID_ARG;
    return ConvertError(libusb_set_configuration(handle, configuration));
}

int APS5_VABI sceUsbdSubmitTransfer(libusb_transfer* transfer) {
    (void)transfer;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

void APS5_VABI sceUsbdUnrefDevice(libusb_device* device) {
    libusb_unref_device(device);
}

}
