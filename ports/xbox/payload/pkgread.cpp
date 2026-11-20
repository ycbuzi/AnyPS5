#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::wstring g_folder;
std::string g_out;

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\pkgread.txt").c_str(), L"wb")) {
        std::fwrite(g_out.data(), 1, g_out.size(), f);
        std::fclose(f);
    }
}

std::wstring Wide(const std::string& s) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::string Narrow(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

void List(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    g_out += "== " + Narrow(dir.c_str());
    if (h == INVALID_HANDLE_VALUE) {
        g_out += " err " + std::to_string(GetLastError()) + "\n";
        Save();
        return;
    }
    std::string names;
    int count = 0;
    do {
        if (!std::wcscmp(fd.cFileName, L".") || !std::wcscmp(fd.cFileName, L"..")) continue;
        ++count;
        if (count <= 40) names += "  " + Narrow(fd.cFileName) + ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "/" : "") + "\n";
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    g_out += " items " + std::to_string(count) + "\n" + names;
    Save();
}

void ReadProbe(const std::wstring& file) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    g_out += "read " + Narrow(file.c_str());
    if (h == INVALID_HANDLE_VALUE) {
        g_out += " err " + std::to_string(GetLastError()) + "\n";
    } else {
        LARGE_INTEGER size{};
        GetFileSizeEx(h, &size);
        char buf[2];
        DWORD got = 0;
        const BOOL ok = ReadFile(h, buf, 2, &got, nullptr);
        g_out += " size " + std::to_string(size.QuadPart) + " read " + std::to_string(ok && got == 2 ? 1 : 0) + "\n";
        CloseHandle(h);
    }
    Save();
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    std::vector<std::wstring> dirs = {L"S:\\", L"S:\\Program Files", L"S:\\Program Files\\WindowsApps", L"C:\\Windows\\SystemApps"};
    if (FILE* f = _wfopen((g_folder + L"\\diag-pkgs.txt").c_str(), L"rb")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            const size_t bar = s.find('|');
            if (bar == std::string::npos) continue;
            const std::string path = s.substr(bar + 1);
            if (path.size() > 3) dirs.push_back(Wide(path));
        }
        std::fclose(f);
    }
    for (const std::wstring& d : dirs) List(d);
    for (const std::wstring& d : dirs) {
        if (d.find(L"WebView2Runtime") == std::wstring::npos) continue;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((d + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') List(d + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (const std::wstring& d : dirs) {
        if (d.find(L"VCLibs.140.00_") != std::wstring::npos) ReadProbe(d + L"\\vcruntime140.dll");
        if (d.find(L"AV1VideoExtension") != std::wstring::npos) ReadProbe(d + L"\\AppxManifest.xml");
    }
    const std::string done = "{\"pkgread\":\"pkgread.txt\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
