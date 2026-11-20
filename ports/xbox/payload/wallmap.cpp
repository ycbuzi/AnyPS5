#include <windows.h>
#include <winternl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::wstring g_folder;

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

std::string Hex(unsigned long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "\"0x%llx\"", v);
    return b;
}

void Save(const std::wstring& name, const std::string& text) {
    if (FILE* f = _wfopen((g_folder + L"\\" + name).c_str(), L"wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

struct ObjectDirectoryInformation {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
};

using NtOpenDirectoryObjectFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
using NtQueryDirectoryObjectFn = NTSTATUS(NTAPI*)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

std::string ObjectDirectory(const wchar_t* path, const wchar_t* file) {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto open = reinterpret_cast<NtOpenDirectoryObjectFn>(GetProcAddress(nt, "NtOpenDirectoryObject"));
    auto query = reinterpret_cast<NtQueryDirectoryObjectFn>(GetProcAddress(nt, "NtQueryDirectoryObject"));
    if (!open || !query) return "\"no ntdll exports\"";
    UNICODE_STRING name{static_cast<USHORT>(std::wcslen(path) * 2), static_cast<USHORT>(std::wcslen(path) * 2 + 2), const_cast<wchar_t*>(path)};
    OBJECT_ATTRIBUTES attributes{sizeof attributes, nullptr, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr};
    HANDLE dir = nullptr;
    const NTSTATUS opened = open(&dir, 0x0001, &attributes);
    if (opened < 0) return "{\"status\":" + Hex(static_cast<unsigned long>(opened)) + "}";
    std::vector<unsigned char> buffer(1 << 16);
    ULONG context = 0, length = 0;
    int count = 0;
    std::string list;
    bool first = true;
    while (true) {
        const NTSTATUS status = query(dir, buffer.data(), static_cast<ULONG>(buffer.size()), FALSE, first ? TRUE : FALSE, &context, &length);
        first = false;
        if (status < 0) break;
        for (auto* e = reinterpret_cast<ObjectDirectoryInformation*>(buffer.data()); e->Name.Buffer; ++e) {
            ++count;
            list += Utf8(std::wstring(e->Name.Buffer, e->Name.Length / 2)) + " [" + Utf8(std::wstring(e->TypeName.Buffer, e->TypeName.Length / 2)) + "]\n";
        }
        if (status != 0x105) break;
    }
    CloseHandle(dir);
    if (file) Save(file, list);
    return "{\"count\":" + std::to_string(count) + "}";
}

std::string Token() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return "{\"error\":" + std::to_string(GetLastError()) + "}";
    std::string j = "{";
    DWORD isApp = 0, size = 0;
    GetTokenInformation(token, TokenIsAppContainer, &isApp, sizeof isApp, &size);
    j += "\"app_container\":" + std::string(isApp ? "true" : "false") + ",";
    unsigned char buffer[4096];
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof buffer, &size)) {
        auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer);
        const DWORD level = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
        j += "\"integrity\":" + Hex(level) + ",";
    }
    if (GetTokenInformation(token, TokenCapabilities, buffer, sizeof buffer, &size)) {
        j += "\"capabilities\":" + std::to_string(reinterpret_cast<TOKEN_GROUPS*>(buffer)->GroupCount) + ",";
    }
    if (GetTokenInformation(token, TokenPrivileges, buffer, sizeof buffer, &size)) {
        auto* privileges = reinterpret_cast<TOKEN_PRIVILEGES*>(buffer);
        std::string names;
        for (DWORD i = 0; i < privileges->PrivilegeCount; ++i) {
            wchar_t name[128]{};
            DWORD length = 127;
            if (LookupPrivilegeNameW(nullptr, &privileges->Privileges[i].Luid, name, &length)) names += std::string(names.empty() ? "" : ",") + Esc(Utf8(name));
        }
        j += "\"privileges\":[" + names + "],";
    }
    CloseHandle(token);
    BOOL inJob = FALSE;
    IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
    j += "\"in_job\":" + std::string(inJob ? "true" : "false") + ",";
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof limits, nullptr)) {
        j += "\"job_flags\":" + Hex(limits.BasicLimitInformation.LimitFlags) + ",\"job_process_mb\":" + std::to_string(limits.ProcessMemoryLimit >> 20) + ",\"job_mb\":" + std::to_string(limits.JobMemoryLimit >> 20) + ",\"job_active_processes\":" + std::to_string(limits.BasicLimitInformation.ActiveProcessLimit) + ",";
    } else {
        j += "\"job_query\":" + std::to_string(GetLastError()) + ",";
    }
    j.pop_back();
    return j + "}";
}

std::string Processes() {
    auto query = reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    std::vector<unsigned char> buffer(1 << 20);
    ULONG length = 0;
    const NTSTATUS status = query ? query(5, buffer.data(), static_cast<ULONG>(buffer.size()), &length) : -1;
    if (status < 0) return "{\"status\":" + Hex(static_cast<unsigned long>(status)) + "}";
    int count = 0;
    std::string list;
    for (ULONG at = 0;;) {
        auto* p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(buffer.data() + at);
        ++count;
        list += std::to_string(reinterpret_cast<ULONG_PTR>(p->UniqueProcessId)) + " " + (p->ImageName.Buffer ? Utf8(std::wstring(p->ImageName.Buffer, p->ImageName.Length / 2)) : std::string("(idle)")) + " threads=" + std::to_string(p->NumberOfThreads) + "\n";
        if (!p->NextEntryOffset) break;
        at += p->NextEntryOffset;
    }
    Save(L"wallmap-processes.txt", list);
    return "{\"count\":" + std::to_string(count) + "}";
}

std::string Registry() {
    struct Probe { HKEY root; const char* rootName; const wchar_t* path; };
    const Probe probes[] = {
        {HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE"}, {HKEY_LOCAL_MACHINE, "HKLM", L"SYSTEM"}, {HKEY_LOCAL_MACHINE, "HKLM", L"HARDWARE"},
        {HKEY_LOCAL_MACHINE, "HKLM", L"SAM"}, {HKEY_LOCAL_MACHINE, "HKLM", L"SECURITY"},
        {HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"}, {HKEY_LOCAL_MACHINE, "HKLM", L"SYSTEM\\CurrentControlSet\\Services"},
        {HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Microsoft\\Durango"}, {HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Microsoft\\Xbox"},
        {HKEY_LOCAL_MACHINE, "HKLM", L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"},
        {HKEY_CURRENT_USER, "HKCU", L"Software"}, {HKEY_CLASSES_ROOT, "HKCR", L""},
    };
    std::string j = "{";
    std::string services;
    for (const auto& probe : probes) {
        HKEY key = nullptr;
        const LONG rc = RegOpenKeyExW(probe.root, probe.path, 0, KEY_READ, &key);
        std::string value;
        if (rc == ERROR_SUCCESS) {
            DWORD subkeys = 0, values = 0;
            RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
            value = "{\"subkeys\":" + std::to_string(subkeys) + ",\"values\":" + std::to_string(values) + "}";
            if (!std::wcscmp(probe.path, L"SYSTEM\\CurrentControlSet\\Services")) {
                wchar_t name[256];
                for (DWORD i = 0;; ++i) {
                    DWORD length = 255;
                    if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                    services += Utf8(name) + "\n";
                }
            }
            if (!std::wcscmp(probe.path, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion")) {
                for (const wchar_t* field : {L"ProductName", L"BuildLabEx", L"EditionID"}) {
                    wchar_t data[256]{};
                    DWORD bytes = sizeof data - 2, type = 0;
                    if (RegQueryValueExW(key, field, nullptr, &type, reinterpret_cast<BYTE*>(data), &bytes) == ERROR_SUCCESS && type == REG_SZ) {
                        value.pop_back();
                        value += "," + Esc(Utf8(field)) + ":" + Esc(Utf8(data)) + "}";
                    }
                }
            }
            RegCloseKey(key);
        } else {
            value = std::to_string(rc);
        }
        j += Esc(std::string(probe.rootName) + "\\" + Utf8(probe.path)) + ":" + value + ",";
    }
    if (!services.empty()) Save(L"wallmap-services.txt", services);
    j.pop_back();
    return j + "}";
}

std::string Services() {
    using OpenScm = SC_HANDLE(WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
    HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
    if (!advapi) advapi = LoadPackagedLibrary(L"advapi32.dll", 0);
    auto open = advapi ? reinterpret_cast<OpenScm>(GetProcAddress(advapi, "OpenSCManagerW")) : nullptr;
    if (!open) return "\"OpenSCManagerW not available\"";
    SC_HANDLE scm = open(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return std::to_string(GetLastError());
    CloseServiceHandle(scm);
    return "\"opened\"";
}

std::string Environment() {
    wchar_t* block = GetEnvironmentStringsW();
    std::string names;
    int count = 0;
    for (const wchar_t* e = block; e && *e; e += std::wcslen(e) + 1) {
        const std::wstring entry(e);
        const size_t eq = entry.find(L'=', 1);
        names += std::string(names.empty() ? "" : ",") + Esc(Utf8(entry.substr(0, eq)));
        ++count;
    }
    if (block) FreeEnvironmentStringsW(block);
    return "{\"count\":" + std::to_string(count) + ",\"names\":[" + names + "]}";
}

std::string OpenTests() {
    struct Target { const char* name; const wchar_t* path; };
    const Target targets[] = {
        {"physical_drive0", L"\\\\.\\PhysicalDrive0"}, {"volume_c", L"\\\\.\\C:"}, {"volume_d", L"\\\\.\\D:"},
        {"nul", L"\\\\.\\NUL"}, {"pipe_root", L"\\\\.\\pipe\\"},
    };
    std::string j = "{";
    for (const auto& t : targets) {
        HANDLE h = CreateFileW(t.path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        j += Esc(t.name) + ":" + (h != INVALID_HANDLE_VALUE ? std::string("\"opened (no access rights requested)\"") : std::to_string(GetLastError())) + ",";
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    WIN32_FIND_DATAW fd{};
    HANDLE pipes = FindFirstFileW(L"\\\\.\\pipe\\*", &fd);
    int count = 0;
    std::string list;
    if (pipes != INVALID_HANDLE_VALUE) {
        do { ++count; list += Utf8(fd.cFileName) + "\n"; } while (FindNextFileW(pipes, &fd));
        FindClose(pipes);
        Save(L"wallmap-pipes.txt", list);
    }
    j += "\"pipes_listed\":" + std::to_string(pipes == INVALID_HANDLE_VALUE ? -static_cast<long long>(GetLastError()) : count);
    return j + "}";
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    std::string j = "{";
    j += "\"token\":" + Token() + ",";
    j += "\"processes\":" + Processes() + ",";
    j += "\"objects\":{\"root\":" + ObjectDirectory(L"\\", L"wallmap-objroot.txt") + ",\"device\":" + ObjectDirectory(L"\\Device", L"wallmap-devices.txt") + ",\"driver\":" + ObjectDirectory(L"\\Driver", nullptr) + ",\"knowndlls\":" + ObjectDirectory(L"\\KnownDlls", nullptr) + ",\"basenamed\":" + ObjectDirectory(L"\\BaseNamedObjects", nullptr) + ",\"sessions\":" + ObjectDirectory(L"\\Sessions", nullptr) + "},";
    j += "\"registry\":" + Registry() + ",";
    j += "\"scm\":" + Services() + ",";
    j += "\"environment\":" + Environment() + ",";
    j += "\"open\":" + OpenTests() + "}";
    Save(L"wallmap.json", j);
    const std::string done = "{\"wallmap\":\"wallmap.json\",\"bytes\":" + std::to_string(j.size()) + "}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
