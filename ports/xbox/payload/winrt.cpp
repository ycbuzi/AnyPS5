#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace {

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Hex(unsigned long v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08lx", v);
    return b;
}

std::string Namespace(const std::wstring& cls) {
    size_t cut = cls.find(L'.');
    for (int i = 0; i < 2 && cut != std::wstring::npos; ++i) cut = cls.find(L'.', cut + 1);
    return Utf8(cls.substr(0, cut));
}

struct Job {
    std::wstring name;
    HRESULT hr = E_PENDING;
};

DWORD WINAPI Activate(void* param) {
    auto* job = static_cast<Job*>(param);
    RoInitialize(RO_INIT_MULTITHREADED);
    HSTRING cls = nullptr;
    WindowsCreateString(job->name.c_str(), static_cast<UINT32>(job->name.size()), &cls);
    IUnknown* factory = nullptr;
    const HRESULT hr = RoGetActivationFactory(cls, __uuidof(IUnknown), reinterpret_cast<void**>(&factory));
    if (factory) factory->Release();
    WindowsDeleteString(cls);
    job->hr = hr;
    return 0;
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    RoInitialize(RO_INIT_MULTITHREADED);
    HKEY root = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId", 0, KEY_READ, &root);
    std::string j;
    if (opened != ERROR_SUCCESS) {
        j = "{\"registry\":" + std::to_string(opened) + "}";
    } else {
        std::string list;
        std::map<std::string, std::pair<int, int>> byNamespace;
        std::map<std::string, int> byResult;
        int total = 0, ok = 0, stoppedAt = -1, start = 0;
        if (FILE* f = _wfopen((std::wstring(folder) + L"\\winrt-start.txt").c_str(), L"rb")) { std::fscanf(f, "%d", &start); std::fclose(f); }
        std::set<std::wstring> only;
        if (FILE* f = _wfopen((std::wstring(folder) + L"\\winrt-only.txt").c_str(), L"rb")) {
            char line[512];
            while (std::fgets(line, sizeof line, f)) {
                std::string s(line);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
                if (!s.empty()) only.insert(std::wstring(s.begin(), s.end()));
            }
            std::fclose(f);
        }
        const ULONGLONG deadline = GetTickCount64() + 100000;
        FILE* progress = _wfopen((std::wstring(folder) + L"\\winrt-progress.txt").c_str(), start ? L"ab" : L"wb");
        wchar_t name[512];
        for (DWORD i = 0;; ++i) {
            DWORD length = 511;
            if (RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            if (static_cast<int>(i) < start) continue;
            if (!only.empty() && !only.count(std::wstring(name, length))) continue;
            if (GetTickCount64() > deadline) { stoppedAt = static_cast<int>(i); break; }
            auto* job = new Job{std::wstring(name, length)};
            HANDLE thread = CreateThread(nullptr, 0, Activate, job, 0, nullptr);
            const bool hung = !thread || WaitForSingleObject(thread, 3000) != WAIT_OBJECT_0;
            if (thread) CloseHandle(thread);
            const HRESULT hr = hung ? static_cast<HRESULT>(0xFFFFFFFF) : job->hr;
            if (!hung) delete job;
            if (progress) { std::fprintf(progress, "%s %s\n", Hex(static_cast<unsigned long>(hr)).c_str(), Utf8(name).c_str()); std::fflush(progress); }
            ++total;
            if (SUCCEEDED(hr)) ++ok;
            auto& ns = byNamespace[Namespace(name)];
            ++ns.first;
            if (SUCCEEDED(hr)) ++ns.second;
            ++byResult[Hex(static_cast<unsigned long>(hr))];
            list += Hex(static_cast<unsigned long>(hr)) + " " + Utf8(name) + "\n";
        }
        RegCloseKey(root);
        if (progress) std::fclose(progress);
        if (FILE* f = _wfopen((std::wstring(folder) + L"\\winrt-classes.txt").c_str(), start ? L"ab" : L"wb")) {
            std::fwrite(list.data(), 1, list.size(), f);
            std::fclose(f);
        }
        j = "{\"start\":" + std::to_string(start) + ",\"stopped_at\":" + std::to_string(stoppedAt) + ",\"total\":" + std::to_string(total) + ",\"ok\":" + std::to_string(ok) + ",\"results\":{";
        for (const auto& [k, v] : byResult) j += "\"" + k + "\":" + std::to_string(v) + ",";
        if (j.back() == ',') j.pop_back();
        j += "},\"namespaces\":{";
        for (const auto& [k, v] : byNamespace) j += "\"" + k + "\":[" + std::to_string(v.first) + "," + std::to_string(v.second) + "],";
        if (j.back() == ',') j.pop_back();
        j += "}}";
    }
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\winrt.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    const std::string done = "{\"winrt\":\"winrt.json\",\"bytes\":" + std::to_string(j.size()) + "}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
