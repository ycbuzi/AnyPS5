#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Esc(const std::string& s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

std::string List(const std::wstring& dir, int depth, int max = 40) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return "{\"error\":" + std::to_string(GetLastError()) + "}";
    std::string items;
    int count = 0;
    do {
        if (!std::wcscmp(fd.cFileName, L".") || !std::wcscmp(fd.cFileName, L"..")) continue;
        ++count;
        if (count > max) continue;
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const unsigned long long size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        items += std::string(items.empty() ? "" : ",") + Esc(Utf8(fd.cFileName)) + ":";
        if (isDir && depth > 0) items += List(dir + L"\\" + fd.cFileName, depth - 1, 15);
        else if (isDir) items += "\"dir\"";
        else items += std::to_string(size);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return "{\"count\":" + std::to_string(count) + ",\"items\":{" + items + "}}";
}

std::string Writable(const std::wstring& dir) {
    const std::wstring file = dir + L"\\anyps5-write-test.tmp";
    CREATEFILE2_EXTENDED_PARAMETERS ep{sizeof ep};
    ep.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    HANDLE h = CreateFile2(file.c_str(), GENERIC_WRITE, 0, CREATE_NEW, &ep);
    if (h == INVALID_HANDLE_VALUE) return std::to_string(GetLastError());
    DWORD done = 0;
    WriteFile(h, "ok", 2, &done, nullptr);
    CloseHandle(h);
    const BOOL removed = DeleteFileW(file.c_str());
    return removed ? "\"yes\"" : "\"written, delete failed " + std::to_string(GetLastError()) + "\"";
}

std::string Space(const std::wstring& dir) {
    ULARGE_INTEGER freeBytes{}, total{};
    if (!GetDiskFreeSpaceExW((dir + L"\\").c_str(), &freeBytes, &total, nullptr)) return std::to_string(GetLastError());
    return "[" + std::to_string(total.QuadPart >> 30) + "," + std::to_string(freeBytes.QuadPart >> 30) + "]";
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    std::string j = "{\"listings\":{";
    const std::vector<std::pair<const char*, const wchar_t*>> deep = {
        {"D", L"D:"}, {"K", L"K:"}, {"M", L"M:"}, {"N", L"N:"}, {"O", L"O:"}, {"P", L"P:"}, {"Q", L"Q:"}, {"V", L"V:"},
    };
    bool first = true;
    for (const auto& [name, path] : deep) {
        j += std::string(first ? "" : ",") + Esc(name) + ":" + List(path, std::wcscmp(path, L"D:") == 0 ? 2 : 1);
        first = false;
    }
    j += "},\"writable\":{";
    const std::vector<std::pair<const char*, const wchar_t*>> targets = {
        {"D", L"D:"}, {"D_DevelopmentFiles", L"D:\\DevelopmentFiles"}, {"D_Temp", L"D:\\Temp"}, {"D_Titles", L"D:\\Titles"}, {"D_Drives", L"D:\\Drives"},
    };
    first = true;
    for (const auto& [name, path] : targets) {
        j += std::string(first ? "" : ",") + Esc(name) + ":{\"write\":" + Writable(path) + ",\"space_gb\":" + Space(path) + "}";
        first = false;
    }
    j += "},\"localstate_space_gb\":" + Space(folder) + "}";
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\drives.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    const std::string done = "{\"drives\":\"drives.json\",\"bytes\":" + std::to_string(j.size()) + "}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
