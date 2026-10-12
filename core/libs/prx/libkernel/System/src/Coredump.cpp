#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>
#include <vector>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/KernelErrors.hpp"

// The handler is recorded but never invoked: host crashes are not turned into guest core dumps.
static std::atomic<uint64_t> g_coredumpHandler{0};
static std::atomic<uint64_t> g_coredumpContext{0};

static std::mutex g_userDataLock;
static std::vector<std::uint8_t> g_userData;

extern "C" {

int APS5_VABI sceCoredumpRegisterCoredumpHandler(uint64_t handler, size_t stack_size, uint64_t context) {
    (void)stack_size;
    g_coredumpHandler.store(handler, std::memory_order_relaxed);
    g_coredumpContext.store(context, std::memory_order_relaxed);
    return 0;
}

int APS5_VABI sceCoredumpUnregisterCoredumpHandler(void) {
    g_coredumpHandler.store(0, std::memory_order_relaxed);
    g_coredumpContext.store(0, std::memory_order_relaxed);
    return 0;
}

int APS5_VABI sceKernelDebugWriteCppExceptionInfo(const void* exception, uint64_t unknown, const char* typeName, const char* what) {
    (void)unknown;
    std::fprintf(stderr, "[coredump] uncaught C++ exception %p of type %s%s%s\n", exception, typeName ? typeName : "(unknown)", what ? ", what(): " : "", what ? what : "");
    return 0;
}


int APS5_VABI sceCoredumpAttachUserFile(void) {
    NotImplemented_nid_no_patch("5nc2gdLNsok");
    return 0;
}

int APS5_VABI sceCoredumpGetStopInfoGpu_Agc(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceCoredumpAttachMemoryRegionAsUserFile(void) {
    NotImplemented_nid_no_patch("MEJ7tc7ThwM");
    return 0;
}

int APS5_VABI sceCoredumpSetUserDataType(void) {
    NotImplemented_nid_no_patch("Uxqkdta7wEg");
    return 0;
}

int APS5_VABI sceCoredumpDebugTextOut(const char* str, int len) {
    if (str == nullptr || len <= 0) return 0;
    std::fprintf(stderr, "[coredump] %.*s\n", len, str);
    return 0;
}

int APS5_VABI sceCoredumpWriteUserData(const void* data, std::size_t size) {
    if (size == 0) return 0;
    if (data == nullptr || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return SCE_KERNEL_ERROR_EINVAL;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::lock_guard lock(g_userDataLock);
    g_userData.insert(g_userData.end(), bytes, bytes + size);
    return static_cast<int>(size);
}

int APS5_VABI sceCoredumpGetStopInfoCpu(void) {
    NotImplemented_nid_no_patch("kK0DUW1Ukgc");
    return 0;
}

int APS5_VABI sceCoredumpGetThreadContextInfo(void* threadContextInfo, size_t threadContextInfoSize) {
    (void)threadContextInfo;
    (void)threadContextInfoSize;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceCoredumpWriteUserString() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceCoredumpAttachUserMemoryFile(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceCoredumpAttachMemoryRegion(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}
}
