#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" __declspec(dllexport) int __cdecl DloadPing() { return 42; }

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    std::string j = "{";
    const std::wstring self = std::wstring(folder) + L"\\payload.dll";
    const std::wstring dir = L"D:\\Temp\\anyps5-dload";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring copy = dir + L"\\copy.dll";
    const BOOL copied = CopyFileW(self.c_str(), copy.c_str(), FALSE);
    j += "\"copy\":" + std::string(copied ? "\"ok\"" : std::to_string(GetLastError())) + ",";
    HMODULE m = copied ? LoadLibraryExW(copy.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
    j += "\"loadlibrary_d\":" + std::string(m ? "\"ok\"" : std::to_string(GetLastError())) + ",";
    if (m) {
        auto ping = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(m, "DloadPing"));
        j += "\"call\":" + std::to_string(ping ? ping() : -1) + ",";
        FreeLibrary(m);
    }
    void* rwx = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    j += "\"rwx_alloc\":" + std::string(rwx ? "\"ok\"" : std::to_string(GetLastError())) + ",";
    if (rwx) VirtualFree(rwx, 0, MEM_RELEASE);
    void* rw = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (rw) {
        const unsigned char code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
        std::memcpy(rw, code, sizeof code);
        DWORD old = 0;
        const BOOL prot = VirtualProtect(rw, 4096, PAGE_EXECUTE_READ, &old);
        j += "\"rw_to_rx\":" + std::string(prot ? "\"ok\"" : std::to_string(GetLastError())) + ",";
        if (prot) {
            FlushInstructionCache(GetCurrentProcess(), rw, sizeof code);
            j += "\"jit_call\":" + std::to_string(reinterpret_cast<int(*)()>(rw)()) + ",";
        }
        VirtualFree(rw, 0, MEM_RELEASE);
    }
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"cmd.exe /c exit 7";
    const BOOL started = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    j += "\"createprocess\":" + std::string(started ? "\"ok\"" : std::to_string(GetLastError())) + ",";
    if (started) {
        WaitForSingleObject(pi.hProcess, 5000);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        j += "\"child_exit\":" + std::to_string(code) + ",";
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    DeleteFileW(copy.c_str());
    RemoveDirectoryW(dir.c_str());
    j.back() = '}';
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\dload.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    if (out && size > j.size()) std::memcpy(out, j.c_str(), j.size() + 1);
    return 0;
}
