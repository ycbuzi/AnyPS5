#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::wstring g_folder;
std::string g_out;

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\ffcheck.txt").c_str(), L"wb")) {
        std::fwrite(g_out.data(), 1, g_out.size(), f);
        std::fclose(f);
    }
}

std::wstring FindPackage(const wchar_t* prefix) {
    std::wstring found;
    if (FILE* f = _wfopen((g_folder + L"\\diag-pkgs.txt").c_str(), L"rb")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            const size_t bar = s.find('|');
            if (bar == std::string::npos) continue;
            const std::string path = s.substr(bar + 1);
            const int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
            std::wstring w(len > 0 ? len - 1 : 0, L'\0');
            if (len > 1) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), len);
            if (w.find(prefix) != std::wstring::npos) found = w;
        }
        std::fclose(f);
    }
    return found;
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    const std::wstring web = FindPackage(L"WebView2Runtime");
    HMODULE ff = web.empty() ? nullptr : LoadLibraryExW((web + L"\\ffmpeg.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!ff) {
        g_out += "no ffmpeg " + std::to_string(GetLastError()) + "\n";
    } else {
        using FindIdFn = const void* (*)(int);
        using NameFn = const char* (*)(int);
        auto dec = reinterpret_cast<FindIdFn>(GetProcAddress(ff, "avcodec_find_decoder"));
        auto name = reinterpret_cast<NameFn>(GetProcAddress(ff, "avcodec_get_name"));
        const int ids[] = {27, 173, 139, 167, 225, 12, 2, 31, 86018, 86017, 86076, 86021, 86028, 65536, 65557, 86056, 86103};
        for (int id : ids) {
            g_out += std::string(name ? name(id) : "?") + "=" + (dec && dec(id) ? "1" : "0") + " ";
        }
        g_out += "\n";
    }
    Save();
    const std::string done = "{\"ffcheck\":\"ffcheck.txt\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
