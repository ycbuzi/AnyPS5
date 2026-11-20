#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder) {
    auto load = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    std::string list;
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\imports.txt").c_str(), L"rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) list.append(buf, n);
        std::fclose(f);
    }
    std::map<std::string, HMODULE> modules;
    std::map<std::string, int> found, total;
    std::string missingDlls, missingFns;
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find('\n', at);
        if (end == std::string::npos) end = list.size();
        const std::string line = list.substr(at, end - at);
        at = end + 1;
        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const std::string dll = line.substr(0, space), fn = line.substr(space + 1);
        auto it = modules.find(dll);
        if (it == modules.end()) {
            std::wstring wide(dll.begin(), dll.end());
            HMODULE m = load ? load(wide.c_str(), nullptr, 0) : nullptr;
            if (!m) missingDlls += std::string(missingDlls.empty() ? "" : ",") + "[\"" + dll + "\"," + std::to_string(GetLastError()) + "]";
            it = modules.emplace(dll, m).first;
        }
        ++total[dll];
        if (it->second && GetProcAddress(it->second, fn.c_str())) {
            ++found[dll];
        } else if (it->second && missingFns.size() < 12000) {
            missingFns += std::string(missingFns.empty() ? "" : ",") + "\"" + dll + "!" + fn + "\"";
        }
    }
    std::string counts;
    for (const auto& [dll, n] : total) counts += std::string(counts.empty() ? "" : ",") + "\"" + dll + "\":[" + std::to_string(found[dll]) + "," + std::to_string(n) + "]";
    const std::string j = "{\"loader\":" + std::string(load ? "true" : "false") + ",\"dlls\":{" + counts + "},\"missing_dlls\":[" + missingDlls + "],\"missing_fns\":[" + missingFns + "]}";
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\payload.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    return 0;
}
