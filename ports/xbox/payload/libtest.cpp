#include <windows.h>
#include <cstdio>
#include <string>

using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder) {
    auto load = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    const std::wstring dll = std::wstring(folder) + L"\\libtest\\psotest.dll";
    std::string j = "{";
    const DWORD flags[] = {0, LOAD_WITH_ALTERED_SEARCH_PATH, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS};
    const char* names[] = {"plain", "altered", "dlldir"};
    for (int i = 0; i < 3; ++i) {
        SetLastError(0);
        HMODULE m = load ? load(dll.c_str(), nullptr, flags[i]) : nullptr;
        const DWORD error = GetLastError();
        j += std::string(i ? "," : "") + "\"" + names[i] + "\":[" + (m ? "true" : "false") + "," + std::to_string(error) + "," + (m && GetProcAddress(m, "PayloadMain") ? "true" : "false") + "]";
        if (m) break;
    }
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dyn{};
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig{};
    GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDynamicCodePolicy, &dyn, sizeof dyn);
    GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSignaturePolicy, &sig, sizeof sig);
    j += ",\"dynamic_code\":" + std::to_string(dyn.Flags) + ",\"signature\":" + std::to_string(sig.Flags) + "}";
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\payload.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    return 0;
}
