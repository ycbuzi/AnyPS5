#include "prx/libkernel/include/Api.hpp"
#include "SceTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <link.h>
#include <stdexcept>
#include <string>

extern "C" int GetModuleInfoExByHandle_nid_no_patch(KernelModule handle, ModuleInfoEx* info);

using PhdrCallback = int (*)(dl_phdr_info*, std::size_t, void*);

constexpr std::size_t ShortSize = offsetof(dl_phdr_info, dlpi_adds);

static bool shortenInfo = false;
static int truncatedCalls = 0;
thread_local int tlsMarker = 7;

struct Forward {
    PhdrCallback callback;
    void* data;
};

static int Truncated(dl_phdr_info* info, std::size_t, void* data) {
    const auto& forward = *static_cast<Forward*>(data);
    dl_phdr_info copy;
    std::memset(&copy, 0xFF, sizeof(copy));
    std::memcpy(&copy, info, ShortSize);
    ++truncatedCalls;
    return forward.callback(&copy, ShortSize, forward.data);
}

extern "C" int dl_iterate_phdr(PhdrCallback callback, void* data) {
    static const auto next = reinterpret_cast<int (*)(PhdrCallback, void*)>(dlsym(RTLD_NEXT, "dl_iterate_phdr"));
    if (next == nullptr) throw std::runtime_error("no next dl_iterate_phdr");
    if (!shortenInfo) return next(callback, data);
    Forward forward{callback, data};
    return next(Truncated, &forward);
}

static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static ModuleInfoEx FromAddress(const void* address, bool shortInfo) {
    ModuleInfoEx info{};
    info.st_size = sizeof(info);
    shortenInfo = shortInfo;
    const int status = sceKernelGetModuleInfoFromAddr(reinterpret_cast<std::uintptr_t>(address), 1, &info);
    shortenInfo = false;
    Require(status == 0, "sceKernelGetModuleInfoFromAddr failed");
    return info;
}

static ModuleInfoEx FromHandle(KernelModule handle, bool shortInfo) {
    ModuleInfoEx info{};
    info.st_size = sizeof(info);
    shortenInfo = shortInfo;
    const int status = GetModuleInfoExByHandle_nid_no_patch(handle, &info);
    shortenInfo = false;
    Require(status == 0, "module info by handle failed");
    return info;
}

static void RequireSame(const ModuleInfoEx& full, const ModuleInfoEx& partial, const char* message) {
    Require(std::string(full.name) == partial.name && full.id == partial.id && full.tls_index == partial.tls_index &&
        full.tls_init_addr == partial.tls_init_addr && full.tls_size == partial.tls_size &&
        full.eh_frame_hdr_addr == partial.eh_frame_hdr_addr && full.eh_frame_addr == partial.eh_frame_addr &&
        full.eh_frame_size == partial.eh_frame_size && full.segment_count == partial.segment_count, message);
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "usage: guest_module_info_short_phdr_tests <fixture>");
        Require(tlsMarker == 7, "thread-local marker lost its initial value");
        void* module = dlopen_nid_postfix(argv[1], 2);
        Require(module != nullptr, "fixture did not load");
        const void* add = dlsym_nid_postfix(module, "GuestModuleAdd");
        Require(add != nullptr, "fixture has no GuestModuleAdd");

        const auto self = FromAddress(reinterpret_cast<const void*>(&Require), false);
        Require(self.tls_index != 0 && self.tls_size != 0, "the test program has a TLS segment but no TLS module index");
        RequireSame(self, FromAddress(reinterpret_cast<const void*>(&Require), true), "short dl_phdr_info changed the program's module info");
        Require(truncatedCalls > 0, "the short dl_phdr_info path was not taken");
        RequireSame(FromHandle(self.id, false), FromHandle(self.id, true), "short dl_phdr_info changed the program's module info by handle");

        const auto fixture = FromAddress(add, false);
        Require(fixture.tls_index == 0 && fixture.tls_size == 0, "the fixture has no TLS segment but reported a TLS module");
        RequireSame(fixture, FromAddress(add, true), "short dl_phdr_info changed the fixture's module info");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
