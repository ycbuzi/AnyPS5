#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::wstring g_folder;
std::string g_out;

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\pkgload.txt").c_str(), L"wb")) {
        std::fwrite(g_out.data(), 1, g_out.size(), f);
        std::fclose(f);
    }
}

std::string Narrow(const std::wstring& w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
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

struct LoadJob {
    std::wstring path;
    const char* symbol;
    DWORD error = 0;
    HMODULE module = nullptr;
    bool symbolFound = false;
};

DWORD WINAPI LoadThread(void* p) {
    auto* job = static_cast<LoadJob*>(p);
    job->module = LoadLibraryExW(job->path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    job->error = job->module ? 0 : GetLastError();
    if (job->module && job->symbol) job->symbolFound = GetProcAddress(job->module, job->symbol) != nullptr;
    return 0;
}

void Try(const std::wstring& dir, const wchar_t* file, const char* symbol) {
    auto* job = new LoadJob{dir + L"\\" + file, symbol};
    g_out += Narrow(file) + " ";
    Save();
    HANDLE t = CreateThread(nullptr, 0, LoadThread, job, 0, nullptr);
    if (WaitForSingleObject(t, 8000) != WAIT_OBJECT_0) {
        g_out += "hang\n";
        Save();
        return;
    }
    CloseHandle(t);
    if (job->module) g_out += std::string("loaded sym=") + (job->symbolFound ? "1" : "0") + "\n";
    else g_out += "err " + std::to_string(job->error) + "\n";
    Save();
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    const std::wstring web = FindPackage(L"WebView2Runtime");
    const std::wstring vclibs = FindPackage(L"VCLibs.140.00_");
    const std::wstring desktop = FindPackage(L"VCLibs.140.00.UWPDesktop");
    const std::wstring xaml = FindPackage(L"UI.Xaml.2.8");
    const std::wstring av1 = FindPackage(L"AV1VideoExtension");
    g_out += "found web=" + std::to_string(!web.empty()) + " vclibs=" + std::to_string(!vclibs.empty()) + "\n";
    Save();
    if (!vclibs.empty()) Try(vclibs, L"vcruntime140_app.dll", "memcpy");
    if (!desktop.empty()) Try(desktop, L"vcruntime140.dll", "memcpy");
    if (!desktop.empty()) Try(desktop, L"msvcp140.dll", nullptr);
    if (!web.empty()) {
        Try(web, L"ffmpeg.dll", "avcodec_find_decoder");
        Try(web, L"libEGL.dll", "eglGetDisplay");
        Try(web, L"libGLESv2.dll", "glDrawArrays");
        Try(web, L"dxil.dll", "DxcCreateInstance");
        Try(web, L"dxcompiler.dll", "DxcCreateInstance");
        Try(web, L"d3dcompiler_47.dll", "D3DCompile");
    }
    if (!xaml.empty()) Try(xaml, L"Microsoft.UI.Xaml.dll", nullptr);
    if (!av1.empty()) Try(av1, L"av1decodermft_xbox.dll", "DllGetClassObject");
    const std::string done = "{\"pkgload\":\"pkgload.txt\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
