#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "prx/libc/include/General.hpp"

#if defined(__linux__) || defined(_WIN32)
#ifdef __linux__
#include <dlfcn.h>
#include <execinfo.h>
#else
#include <filesystem>
#include <string_view>
#include <windows.h>
#include "prx/libc/include/specifics/itanium/UnwindAbi.hpp"

extern "C" _Unwind_Reason_Code APS5_VABI _Unwind_Backtrace_nid_postfix(_Unwind_Trace_Fn trace, void* argument);
extern "C" _Unwind_Ptr APS5_VABI _Unwind_GetIP_nid_postfix(_Unwind_Context* context);
#endif

namespace {

constexpr int MaxFrames = 256;
constexpr std::size_t NameOffset = 0x10;
constexpr char GuestSuffix[] = ".guest.prx";

struct Frame {
    std::uint32_t offset;
    std::string module;
};

struct FrameRecord {
    FrameRecord* next;
    std::uint32_t offset;
    std::uint32_t reserved;
};
static_assert(sizeof(FrameRecord) == NameOffset);

std::string ModuleName(std::string module) {
    if (const auto slash = module.find_last_of("/\\"); slash != std::string::npos) module.erase(0, slash + 1);
    if (module.size() > sizeof(GuestSuffix) - 1 && module.compare(module.size() - (sizeof(GuestSuffix) - 1), std::string::npos, GuestSuffix) == 0)
        module.resize(module.size() - (sizeof(GuestSuffix) - 1));
    return module;
}

#ifdef __linux__
[[gnu::noinline]] std::vector<Frame> CaptureFrames(int limit) {
    void* addresses[MaxFrames];
    const int captured = backtrace(addresses, MaxFrames);
    std::vector<Frame> frames;
    for (int index = 2; index < captured && (limit < 0 || static_cast<int>(frames.size()) < limit); ++index) {
        Dl_info info{};
        if (dladdr(addresses[index], &info) == 0 || info.dli_fbase == nullptr || info.dli_fname == nullptr) continue;
        const auto offset = reinterpret_cast<std::uintptr_t>(addresses[index]) - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        if (offset > UINT32_MAX) continue;
        frames.push_back({static_cast<std::uint32_t>(offset), ModuleName(info.dli_fname)});
    }
    return frames;
}
#else
std::string ModuleFileName(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
    const auto name = std::filesystem::path(std::wstring_view(path)).filename().u8string();
    return std::string(name.begin(), name.end());
}

struct Trace {
    void* addresses[MaxFrames];
    int count = 0;
};

_Unwind_Reason_Code APS5_VABI CollectFrame(_Unwind_Context* context, void* argument) {
    auto& trace = *static_cast<Trace*>(argument);
    if (trace.count == MaxFrames) return _URC_NORMAL_STOP;
    trace.addresses[trace.count++] = reinterpret_cast<void*>(_Unwind_GetIP_nid_postfix(context));
    return _URC_NO_REASON;
}

[[gnu::noinline]] std::vector<Frame> CaptureFrames(int limit) {
    Trace trace;
    _Unwind_Backtrace_nid_postfix(CollectFrame, &trace);
    const int captured = trace.count;
    void* const* addresses = trace.addresses;
    std::vector<Frame> frames;
    for (int index = 2; index < captured && (limit < 0 || static_cast<int>(frames.size()) < limit); ++index) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCWSTR>(addresses[index]), &module) || module == nullptr) continue;
        const auto offset = reinterpret_cast<std::uintptr_t>(addresses[index]) - reinterpret_cast<std::uintptr_t>(module);
        if (offset > UINT32_MAX) continue;
        auto name = ModuleFileName(module);
        if (name.empty()) continue;
        frames.push_back({static_cast<std::uint32_t>(offset), ModuleName(std::move(name))});
    }
    return frames;
}
#endif

std::size_t RecordSize(const Frame& frame) {
    return (NameOffset + frame.module.size() + 1 + 7) & ~std::size_t{7};
}

}  // namespace

extern "C" {

int APS5_VABI sceLibcBacktraceGetBufferSize_nid_postfix(int limit, int* count, std::size_t* size) {
    if (count == nullptr || size == nullptr) throw std::invalid_argument("sceLibcBacktraceGetBufferSize: null output");
    const auto frames = CaptureFrames(limit);
    std::size_t total = 0;
    for (const auto& frame : frames) total += RecordSize(frame);
    *count = static_cast<int>(frames.size());
    *size = total;
    return 0;
}

int APS5_VABI sceLibcBacktraceSelf_nid_postfix(int limit, void* buffer, std::size_t size, int* count) {
    if (buffer == nullptr || count == nullptr) throw std::invalid_argument("sceLibcBacktraceSelf: null argument");
    const auto frames = CaptureFrames(limit);
    auto* cursor = static_cast<std::byte*>(buffer);
    FrameRecord* previous = nullptr;
    int written = 0;
    for (const auto& frame : frames) {
        const auto bytes = RecordSize(frame);
        if (static_cast<std::size_t>(cursor - static_cast<std::byte*>(buffer)) + bytes > size) break;
        auto* record = reinterpret_cast<FrameRecord*>(cursor);
        record->next = nullptr;
        record->offset = frame.offset;
        record->reserved = 0;
        std::memcpy(cursor + NameOffset, frame.module.c_str(), frame.module.size() + 1);
        if (previous != nullptr) previous->next = record;
        previous = record;
        cursor += bytes;
        ++written;
    }
    *count = written;
    return 0;
}

}

#else

extern "C" {

int APS5_VABI sceLibcBacktraceGetBufferSize_nid_postfix(int limit, int* count, std::size_t* size) {
    (void)limit;
    (void)count;
    (void)size;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceLibcBacktraceSelf_nid_postfix(int limit, void* buffer, std::size_t size, int* count) {
    (void)limit;
    (void)buffer;
    (void)size;
    (void)count;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}

#endif
