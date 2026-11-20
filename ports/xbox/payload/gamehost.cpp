#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>

using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

namespace {

HMODULE g_game = nullptr;
std::wstring g_status;
LoadLibraryExWFn g_load = nullptr;

void Status(const std::string& text) {
    if (FILE* f = _wfopen(g_status.c_str(), L"ab")) {
        const std::string line = std::to_string(GetTickCount64()) + " " + text + "\n";
        std::fwrite(line.data(), 1, line.size(), f);
        std::fclose(f);
    }
}

void ClaimBackButton() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (winrt::hresult_error const&) {
    }
    try {
        auto window = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow();
        window.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [] {
            try {
                winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView().BackRequested([](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::UI::Core::BackRequestedEventArgs const& e) { e.Handled(true); });
                Status("back button claimed");
            } catch (winrt::hresult_error const& e) {
                Status("back button claim failed " + std::to_string(static_cast<long>(e.code())));
            }
        });
        Status("back button handler queued");
    } catch (winrt::hresult_error const& e) {
        Status("back button queue failed " + std::to_string(static_cast<long>(e.code())));
    }
}

std::wstring g_exePath;

DWORD WINAPI HookGetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size) {
    if (module && module != g_game) return GetModuleFileNameW(module, buffer, size);
    if (!size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    const DWORD length = static_cast<DWORD>(g_exePath.size());
    const DWORD copied = length < size ? length : size - 1;
    std::memcpy(buffer, g_exePath.c_str(), copied * sizeof(wchar_t));
    buffer[copied] = 0;
    SetLastError(copied < length ? ERROR_INSUFFICIENT_BUFFER : ERROR_SUCCESS);
    return copied < length ? size : copied;
}

DWORD WINAPI HookGetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size) {
    if (module && module != g_game) return GetModuleFileNameA(module, buffer, size);
    if (!size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    const int length = WideCharToMultiByte(CP_ACP, 0, g_exePath.c_str(), -1, buffer, static_cast<int>(size), nullptr, nullptr);
    if (!length) {
        buffer[size - 1] = 0;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    SetLastError(ERROR_SUCCESS);
    return static_cast<DWORD>(length - 1);
}

HMODULE WINAPI HookGetModuleHandleW(LPCWSTR name) {
    return name ? GetModuleHandleW(name) : g_game;
}

void WINAPI HookExitProcess(UINT code) {
    Status("exit " + std::to_string(code));
    ExitThread(code);
}

void* Hook(const char* name) {
    if (!std::strcmp(name, "GetModuleFileNameW")) return reinterpret_cast<void*>(&HookGetModuleFileNameW);
    if (!std::strcmp(name, "GetModuleFileNameA")) return reinterpret_cast<void*>(&HookGetModuleFileNameA);
    if (!std::strcmp(name, "GetModuleHandleW")) return reinterpret_cast<void*>(&HookGetModuleHandleW);
    if (!std::strcmp(name, "ExitProcess")) return reinterpret_cast<void*>(&HookExitProcess);
    return nullptr;
}

std::string Bind(BYTE* base) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.Size) return "no imports";
    int bound = 0, hooked = 0;
    std::string missing;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        std::wstring wide(dll, dll + std::strlen(dll));
        HMODULE module = g_load(wide.c_str(), nullptr, 0);
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            void* target = nullptr;
            const char* fn = nullptr;
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                target = module ? reinterpret_cast<void*>(GetProcAddress(module, reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(names->u1.Ordinal)))) : nullptr;
            } else {
                fn = reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
                if (void* h = Hook(fn)) {
                    target = h;
                    ++hooked;
                } else if (module) {
                    target = reinterpret_cast<void*>(GetProcAddress(module, fn));
                }
            }
            if (!target) {
                missing += std::string(dll) + "!" + (fn ? fn : "#") + " ";
                continue;
            }
            DWORD old = 0;
            VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<ULONGLONG>(target);
            VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, old, &old);
            ++bound;
        }
    }
    return "bound " + std::to_string(bound) + " hooked " + std::to_string(hooked) + (missing.empty() ? "" : " missing " + missing);
}

LONG WINAPI WatchException(EXCEPTION_POINTERS* info) {
    static volatile LONG seen = 0;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0x40010006 || code == 0x4001000A || code == DBG_PRINTEXCEPTION_C || code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&seen) <= 40) {
        char text[160];
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &module);
        char name[MAX_PATH] = "?";
        if (module) GetModuleFileNameA(module, name, MAX_PATH);
        const char* leaf = std::strrchr(name, '\\');
        std::snprintf(text, sizeof text, "exception %08lx at %p (%s+%llx) thread %lu", code, info->ExceptionRecord->ExceptionAddress, leaf ? leaf + 1 : name, module ? (unsigned long long)(reinterpret_cast<BYTE*>(info->ExceptionRecord->ExceptionAddress) - reinterpret_cast<BYTE*>(module)) : 0ull, GetCurrentThreadId());
        Status(text);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI Heartbeat(void*) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job{};
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &job, sizeof job, nullptr))
        Status("job flags " + std::to_string(job.BasicLimitInformation.LimitFlags) + " process limit MB " + std::to_string(job.ProcessMemoryLimit >> 20) + " job limit MB " + std::to_string(job.JobMemoryLimit >> 20));
    else
        Status("job query failed " + std::to_string(GetLastError()));
    SIZE_T last = 0;
    for (unsigned tick = 0;; ++tick) {
        PROCESS_MEMORY_COUNTERS_EX m{};
        m.cb = sizeof m;
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m)) {
            const SIZE_T diff = m.PrivateUsage > last ? m.PrivateUsage - last : last - m.PrivateUsage;
            if (diff > (64u << 20) || tick % 10 == 0) {
                Status("mem private MB " + std::to_string(m.PrivateUsage >> 20) + " ws MB " + std::to_string(m.WorkingSetSize >> 20) + " peak private MB " + std::to_string(m.PeakPagefileUsage >> 20));
                last = m.PrivateUsage;
            }
        }
        Sleep(1000);
    }
}

DWORD WINAPI Run(void*) {
    auto* base = reinterpret_cast<BYTE*>(g_game);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const auto& tls = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tls.Size) {
        auto* t = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(base + tls.VirtualAddress);
        Status("tls index " + std::to_string(t->AddressOfIndex ? *reinterpret_cast<DWORD*>(t->AddressOfIndex) : 0xffffffffu) + " callbacks " + std::to_string(t->AddressOfCallBacks ? 1 : 0));
    }
    using Entry = int(WINAPI*)();
    auto entry = reinterpret_cast<Entry>(base + nt->OptionalHeader.AddressOfEntryPoint);
    Status("entry");
    const int code = entry();
    Status("returned " + std::to_string(code));
    return static_cast<DWORD>(code);
}

bool PreferredRange(const std::wstring& exe, ULONGLONG& base, DWORD& size) {
    FILE* f = _wfopen(exe.c_str(), L"rb");
    if (!f) return false;
    unsigned char header[4096]{};
    const size_t n = std::fread(header, 1, sizeof header, f);
    std::fclose(f);
    if (n < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(header);
    if (dos->e_lfanew <= 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > n) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(header + dos->e_lfanew);
    base = nt->OptionalHeader.ImageBase;
    size = nt->OptionalHeader.SizeOfImage;
    return true;
}

void ReportRange(ULONGLONG base, DWORD size) {
    char text[512];
    std::snprintf(text, sizeof text, "preferred range %llx..%llx", base, base + size);
    Status(text);
    ULONGLONG at = base;
    int listed = 0;
    while (at < base + size && listed < 40) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info)) break;
        if (info.State != MEM_FREE) {
            wchar_t module[260]{};
            char name[300]{};
            if (info.Type == MEM_IMAGE && GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), module, 259))
                WideCharToMultiByte(CP_UTF8, 0, module, -1, name, 299, nullptr, nullptr);
            std::snprintf(text, sizeof text, "used %p alloc %p size %llx state %lx type %lx protect %lx %s", info.BaseAddress, info.AllocationBase, static_cast<ULONGLONG>(info.RegionSize), info.State, info.Type, info.AllocationProtect, name);
            Status(text);
            ++listed;
        }
        at = reinterpret_cast<ULONGLONG>(info.BaseAddress) + info.RegionSize;
    }
}

struct LoaderEntry {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    ULONG SizeOfImage;
};

struct LoaderData {
    ULONG Length;
    BOOLEAN Initialized;
    void* SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
};

bool MoveLoaderEntry(void* from, void* to) {
    auto* ldr = *reinterpret_cast<LoaderData**>(__readgsqword(0x60) + 0x18);
    for (LIST_ENTRY* link = ldr->InLoadOrderModuleList.Flink; link != &ldr->InLoadOrderModuleList; link = link->Flink) {
        auto* entry = CONTAINING_RECORD(link, LoaderEntry, InLoadOrderLinks);
        if (entry->DllBase == from) {
            entry->DllBase = to;
            return true;
        }
    }
    return false;
}

BYTE* MapAtPreferredBase(const std::wstring& exe, HMODULE loaded) {
    FILE* f = _wfopen(exe.c_str(), L"rb");
    if (!f) {
        Status("map: open failed");
        return nullptr;
    }
    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string file(static_cast<size_t>(fileSize), '\0');
    const size_t got = std::fread(file.data(), 1, file.size(), f);
    std::fclose(f);
    if (got != file.size()) {
        Status("map: short read");
        return nullptr;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    const ULONGLONG preferred = nt->OptionalHeader.ImageBase;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    auto* base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(preferred), size, MEM_COMMIT, PAGE_READWRITE));
    if (base != reinterpret_cast<BYTE*>(preferred)) {
        Status("map: VirtualAlloc at preferred base failed " + std::to_string(GetLastError()));
        return nullptr;
    }
    std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        const DWORD raw = s.SizeOfRawData < s.Misc.VirtualSize || !s.Misc.VirtualSize ? s.SizeOfRawData : s.Misc.VirtualSize;
        if (raw && s.PointerToRawData + static_cast<ULONGLONG>(raw) <= file.size())
            std::memcpy(base + s.VirtualAddress, file.data() + s.PointerToRawData, raw);
    }
    const auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.Size) {
        auto* mine = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(base + tlsDir.VirtualAddress);
        auto* theirs = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(reinterpret_cast<BYTE*>(loaded) + tlsDir.VirtualAddress);
        const DWORD index = *reinterpret_cast<DWORD*>(theirs->AddressOfIndex);
        *reinterpret_cast<DWORD*>(mine->AddressOfIndex) = index;
        Status("map: tls index " + std::to_string(index));
    }
    Status(Bind(base));
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        const DWORD c = s.Characteristics;
        DWORD protect = PAGE_READONLY;
        if (c & IMAGE_SCN_MEM_EXECUTE) protect = (c & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (c & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        const DWORD span = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
        DWORD old = 0;
        if (span && !VirtualProtect(base + s.VirtualAddress, span, protect, &old)) {
            char name[9]{};
            std::memcpy(name, s.Name, 8);
            Status(std::string("map: protect ") + name + " failed " + std::to_string(GetLastError()));
            return nullptr;
        }
    }
    const auto& pdata = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (pdata.Size) {
        const BOOLEAN added = RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(base + pdata.VirtualAddress), pdata.Size / sizeof(RUNTIME_FUNCTION), reinterpret_cast<DWORD64>(base));
        Status(added ? "map: function table added" : "map: function table failed");
    }
    FlushInstructionCache(GetCurrentProcess(), base, size);
    Status(MoveLoaderEntry(loaded, base) ? "map: loader entry moved" : "map: loader entry not found");
    wchar_t probe[MAX_PATH]{};
    const DWORD probeLength = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), probe, MAX_PATH);
    Status(probeLength ? "map: module name lookup works" : "map: module name lookup fails " + std::to_string(GetLastError()));
    HMODULE owner = nullptr;
    const BOOL found = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(base + nt->OptionalHeader.AddressOfEntryPoint), &owner);
    Status(found && owner == reinterpret_cast<HMODULE>(base) ? "map: address lookup works" : "map: address lookup fails");
    return base;
}

std::wstring ReadGameDir(const std::wstring& folder) {
    std::wstring dir = folder + L"\\game";
    if (FILE* f = _wfopen((folder + L"\\game.txt").c_str(), L"rb")) {
        char buf[1024]{};
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        std::string s(buf, n);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty()) {
            wchar_t wide[1024]{};
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wide, 1023);
            dir = wide;
        }
    }
    return dir;
}

}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && !g_status.empty()) Status(reserved ? "process exit" : "unloaded");
    return TRUE;
}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder) {
    g_status = std::wstring(folder) + L"\\gamehost.txt";
    _wremove(g_status.c_str());
    ClaimBackButton();
    g_load = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    const std::wstring dir = ReadGameDir(folder);
    using PutEnvFn = int(__cdecl*)(const wchar_t*, const wchar_t*);
    HMODULE ucrt = g_load ? g_load(L"ucrtbase.dll", nullptr, 0) : nullptr;
    const auto putEnv = ucrt ? reinterpret_cast<PutEnvFn>(GetProcAddress(ucrt, "_wputenv_s")) : nullptr;
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\env.txt").c_str(), L"rb")) {
        char line[2048];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            const size_t eq = s.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            wchar_t name[256]{}, value[1792]{};
            MultiByteToWideChar(CP_UTF8, 0, s.substr(0, eq).c_str(), -1, name, 255);
            MultiByteToWideChar(CP_UTF8, 0, s.substr(eq + 1).c_str(), -1, value, 1791);
            SetEnvironmentVariableW(name, value);
            const int ucrtResult = putEnv ? putEnv(name, value) : -1;
            Status("env " + s.substr(0, eq) + (ucrtResult == 0 ? " (ucrt)" : " (ucrt missed)"));
        }
        std::fclose(f);
    }
    SetCurrentDirectoryW(dir.c_str());
    for (const wchar_t* sub : {L"", L"\\libs"}) {
        WIN32_FIND_DATAW pending{};
        const std::wstring folderPath = dir + sub;
        HANDLE search = FindFirstFileW((folderPath + L"\\*.pending").c_str(), &pending);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring from = folderPath + L"\\" + pending.cFileName;
            const std::wstring to = from.substr(0, from.size() - 8);
            const BOOL moved = MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, pending.cFileName, -1, name, MAX_PATH, nullptr, nullptr);
            Status(std::string("pending ") + name + (moved ? " applied" : " failed " + std::to_string(GetLastError())));
        } while (FindNextFileW(search, &pending));
        FindClose(search);
    }
    std::wstring exe;
    WIN32_FIND_DATAW find{};
    HANDLE h = FindFirstFileExW((dir + L"\\*.exe").c_str(), FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        exe = dir + L"\\" + find.cFileName;
        FindClose(h);
    }
    if (exe.empty()) {
        Status("no exe");
        return 1;
    }
    g_exePath = exe;
    ULONGLONG preferred = 0;
    DWORD imageSize = 0;
    if (PreferredRange(exe, preferred, imageSize)) {
        ReportRange(preferred, imageSize);
        if (!VirtualAlloc(reinterpret_cast<void*>(preferred), imageSize, MEM_RESERVE, PAGE_NOACCESS)) Status("reserve failed " + std::to_string(GetLastError()));
    }
    g_game = g_load(exe.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!g_game) {
        Status("load failed " + std::to_string(GetLastError()));
        return 2;
    }
    char text[64];
    std::snprintf(text, sizeof text, "loaded at %p", static_cast<void*>(g_game));
    Status(text);
    bool mapped = false;
    if (preferred && reinterpret_cast<ULONGLONG>(g_game) != preferred) {
        BYTE* base = MapAtPreferredBase(exe, g_game);
        if (!base) return 4;
        g_game = reinterpret_cast<HMODULE>(base);
        mapped = true;
    }
    HANDLE pre = FindFirstFileExW((dir + L"\\*.dll").c_str(), FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    while (pre != INVALID_HANDLE_VALUE) {
        const std::wstring path = dir + L"\\" + find.cFileName;
        HMODULE m = g_load(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        char name[600]{};
        WideCharToMultiByte(CP_UTF8, 0, find.cFileName, -1, name, 500, nullptr, nullptr);
        Status(std::string("preload ") + name + (m ? " ok" : " failed " + std::to_string(GetLastError())));
        if (!FindNextFileW(pre, &find)) break;
    }
    if (pre != INVALID_HANDLE_VALUE) FindClose(pre);
    if (!mapped) Status(Bind(reinterpret_cast<BYTE*>(g_game)));
    auto** imageBase = reinterpret_cast<void**>(__readgsqword(0x60) + 0x10);
    *imageBase = g_game;
    Status(GetModuleHandleW(nullptr) == g_game ? "peb image = game" : "peb image swap failed");
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<BYTE*>(g_game) + reinterpret_cast<IMAGE_DOS_HEADER*>(g_game)->e_lfanew);
    SIZE_T stack = static_cast<SIZE_T>(nt->OptionalHeader.SizeOfStackReserve);
    if (stack < (16u << 20)) stack = 16u << 20;
    HANDLE thread = CreateThread(nullptr, stack, &Run, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) {
        Status("thread failed " + std::to_string(GetLastError()));
        return 3;
    }
    AddVectoredExceptionHandler(1, &WatchException);
    if (HANDLE beat = CreateThread(nullptr, 0, &Heartbeat, nullptr, 0, nullptr)) CloseHandle(beat);
    WaitForSingleObject(thread, INFINITE);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    Status("game thread ended " + std::to_string(code));
    return 0;
}
