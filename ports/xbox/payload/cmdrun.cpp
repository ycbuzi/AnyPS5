#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string Esc(const std::string& s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

std::wstring Wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Run(const std::wstring& folder, const std::string& name, const std::string& line, DWORD timeoutMs) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 1 << 16)) return "{\"pipe\":" + std::to_string(GetLastError()) + "}";
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = Wide(line);
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd);
    if (!started) {
        const DWORD error = GetLastError();
        CloseHandle(readEnd);
        return "{\"start\":" + std::to_string(error) + "}";
    }
    std::string output;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    bool timedOut = false;
    char buffer[8192];
    while (true) {
        DWORD available = 0;
        if (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD got = 0;
            if (ReadFile(readEnd, buffer, sizeof buffer, &got, nullptr) && got && output.size() < (4u << 20)) output.append(buffer, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD got = 0;
            while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available && ReadFile(readEnd, buffer, sizeof buffer, &got, nullptr) && got) output.append(buffer, got);
            break;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 0xDEAD);
            timedOut = true;
            break;
        }
        Sleep(20);
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readEnd);
    if (FILE* f = _wfopen((folder + L"\\cmd-" + Wide(name) + L".txt").c_str(), L"wb")) {
        std::fwrite(output.data(), 1, output.size(), f);
        std::fclose(f);
    }
    return "{\"exit\":" + std::to_string(code) + ",\"bytes\":" + std::to_string(output.size()) + (timedOut ? ",\"timeout\":true" : "") + "}";
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    const std::wstring dir(folder);
    std::string list;
    if (FILE* f = _wfopen((dir + L"\\cmds.txt").c_str(), L"rb")) {
        char buffer[4096];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) list.append(buffer, n);
        std::fclose(f);
    }
    std::string j = "{";
    size_t at = 0;
    while (at < list.size()) {
        size_t end = list.find('\n', at);
        if (end == std::string::npos) end = list.size();
        std::string line = list.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t bar = line.find('|');
        if (line.empty() || bar == std::string::npos) continue;
        j += Esc(line.substr(0, bar)) + ":" + Run(dir, line.substr(0, bar), line.substr(bar + 1), 12000) + ",";
    }
    if (j.size() > 1) j.back() = '}'; else j += "}";
    if (FILE* f = _wfopen((dir + L"\\cmdrun.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    const std::string done = "{\"cmdrun\":\"cmdrun.json\",\"bytes\":" + std::to_string(j.size()) + "}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
