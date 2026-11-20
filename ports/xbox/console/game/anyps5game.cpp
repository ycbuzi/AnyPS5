#include <windows.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

namespace {

std::wstring g_local;
std::wstring g_game;
std::mutex g_logLock;
std::atomic<bool> g_activated{false};

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring LocalFolder() {
    if (g_local.empty()) g_local = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
    return g_local;
}

void Log(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_logLock);
    if (FILE* f = _wfopen((LocalFolder() + L"\\gamepart.txt").c_str(), L"ab")) {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        std::fprintf(f, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text.c_str());
        std::fclose(f);
    }
}

bool SafeGameName(const std::wstring& name) {
    if (name.empty() || name.size() > 200 || name == L"." || name == L"..") return false;
    for (wchar_t c : name) {
        if (c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') return false;
    }
    return true;
}

void ChooseGame(const std::wstring& name) {
    if (!SafeGameName(name)) {
        Log("rejected game name " + Narrow(name));
        return;
    }
    const std::wstring dir = LocalFolder() + L"\\games\\" + name;
    const DWORD attributes = GetFileAttributesW(dir.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        Log("game folder missing: games\\" + Narrow(name));
        return;
    }
    g_game = dir;
    const std::string path = Narrow(dir);
    if (FILE* f = _wfopen((LocalFolder() + L"\\game.txt").c_str(), L"wb")) {
        std::fwrite(path.data(), 1, path.size(), f);
        std::fclose(f);
    }
    if (FILE* f = _wfopen((LocalFolder() + L"\\last.txt").c_str(), L"wb")) {
        const std::string n = Narrow(name);
        std::fwrite(n.data(), 1, n.size(), f);
        std::fclose(f);
    }
    Log("game: games\\" + Narrow(name));
}

std::wstring LastGame() {
    std::wstring name;
    if (FILE* f = _wfopen((LocalFolder() + L"\\last.txt").c_str(), L"rb")) {
        char buf[512]{};
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        std::string s(buf, n);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        wchar_t wide[512]{};
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wide, 511);
        name = wide;
    }
    return name;
}

void HandleActivation(winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs const& args) {
    using namespace winrt::Windows::ApplicationModel::Activation;
    Log("activated kind " + std::to_string(static_cast<int>(args.Kind())));
    if (args.Kind() != ActivationKind::Protocol) {
        const std::wstring last = LastGame();
        if (!last.empty()) ChooseGame(last);
        return;
    }
    const auto uri = args.as<ProtocolActivatedEventArgs>().Uri();
    try {
        const auto query = uri.QueryParsed();
        ChooseGame(std::wstring(query.GetFirstValueByName(L"game")));
    } catch (winrt::hresult_error const& e) {
        Log("bad launch link " + Narrow(std::wstring(e.message())));
    }
}

using PayloadMain = int(__cdecl*)(const wchar_t*, char*, size_t);

HMODULE ResolveModule(const char* name) {
    wchar_t wide[260]{};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 259);
    if (HMODULE module = GetModuleHandleW(wide)) return module;
    if (HMODULE module = LoadPackagedLibrary(wide, 0)) return module;
    using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
    static auto loadLibrary = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    return loadLibrary ? loadLibrary(wide, nullptr, 0) : nullptr;
}

std::string RunPayload(const std::wstring& path, const std::wstring& folder) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return "no payload";
    std::string image;
    char chunk[65536];
    for (size_t got; (got = std::fread(chunk, 1, sizeof chunk, file)) != 0;) image.append(chunk, got);
    std::fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (image.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) return "bad dos header";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return "bad nt header";
    const SIZE_T size = nt->OptionalHeader.SizeOfImage;
    auto* base = static_cast<unsigned char*>(VirtualAllocFromApp(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) return "alloc failed " + std::to_string(GetLastError());
    std::memcpy(base, image.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (section->SizeOfRawData) std::memcpy(base + section->VirtualAddress, image.data() + section->PointerToRawData, section->SizeOfRawData);
    }
    const auto& directories = nt->OptionalHeader.DataDirectory;
    const auto delta = reinterpret_cast<std::uint64_t>(base) - nt->OptionalHeader.ImageBase;
    const auto& relocs = directories[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && relocs.Size) {
        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + relocs.VirtualAddress);
        auto* end = reinterpret_cast<unsigned char*>(block) + relocs.Size;
        while (reinterpret_cast<unsigned char*>(block) < end && block->SizeOfBlock) {
            const auto* entries = reinterpret_cast<const WORD*>(block + 1);
            const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            for (size_t i = 0; i < count; ++i) {
                if ((entries[i] >> 12) == IMAGE_REL_BASED_DIR64) *reinterpret_cast<std::uint64_t*>(base + block->VirtualAddress + (entries[i] & 0xFFF)) += delta;
            }
            block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<unsigned char*>(block) + block->SizeOfBlock);
        }
    }
    std::string missing;
    const auto& imports = directories[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.Size) {
        for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); descriptor->Name; ++descriptor) {
            const char* dll = reinterpret_cast<const char*>(base + descriptor->Name);
            HMODULE module = ResolveModule(dll);
            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
            const auto* lookup = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
            for (; lookup->u1.AddressOfData; ++lookup, ++thunk) {
                FARPROC proc = nullptr;
                std::string what;
                if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) {
                    what = "#" + std::to_string(IMAGE_ORDINAL64(lookup->u1.Ordinal));
                    if (module) proc = GetProcAddress(module, reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(lookup->u1.Ordinal)));
                } else {
                    what = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData)->Name;
                    if (module) proc = GetProcAddress(module, what.c_str());
                }
                if (!proc) missing += (missing.empty() ? "" : ",") + std::string(dll) + "!" + what;
                thunk->u1.Function = reinterpret_cast<std::uint64_t>(proc);
            }
        }
    }
    if (!missing.empty()) return "missing imports " + missing;
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD c = section->Characteristics;
        ULONG protect = PAGE_READONLY;
        if (c & IMAGE_SCN_MEM_EXECUTE) protect = (c & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (c & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        ULONG old = 0;
        const SIZE_T length = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        if (length && !VirtualProtectFromApp(base + section->VirtualAddress, length, protect, &old)) return "protect failed " + std::to_string(GetLastError());
    }
    const auto& exceptions = directories[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.Size) {
        RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(base + exceptions.VirtualAddress), exceptions.Size / sizeof(RUNTIME_FUNCTION), reinterpret_cast<DWORD64>(base));
    }
    using DllMainFn = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    if (nt->OptionalHeader.AddressOfEntryPoint) {
        auto entry = reinterpret_cast<DllMainFn>(base + nt->OptionalHeader.AddressOfEntryPoint);
        if (!entry(reinterpret_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr)) return "dllmain failed";
    }
    const auto& exports = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exports.Size) return "no exports";
    const auto* table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exports.VirtualAddress);
    const auto* names = reinterpret_cast<const DWORD*>(base + table->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + table->AddressOfNameOrdinals);
    const auto* functions = reinterpret_cast<const DWORD*>(base + table->AddressOfFunctions);
    for (DWORD i = 0; i < table->NumberOfNames; ++i) {
        if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), "PayloadMain") == 0) {
            auto main = reinterpret_cast<PayloadMain>(base + functions[ordinals[i]]);
            std::string output(1 << 16, static_cast<char>(0));
            const int code = main(folder.c_str(), output.data(), output.size());
            return "game host returned " + std::to_string(code);
        }
    }
    return "no PayloadMain";
}

std::wstring PayloadPath() {
    const std::wstring local = LocalFolder() + L"\\payload.dll";
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;
    return std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path()) + L"\\payload.dll";
}

void RunGame() {
    try {
        const auto limit = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
        Log("memory limit " + std::to_string(limit >> 20) + " MB, in use " + std::to_string(winrt::Windows::System::MemoryManager::AppMemoryUsage() >> 20) + " MB");
    } catch (...) {
        Log("memory limit unknown");
    }
    if (g_game.empty()) {
        Log("no game chosen; using game.txt");
    }
    const std::wstring payload = PayloadPath();
    Log(std::string("payload from ") + (payload.compare(0, LocalFolder().size(), LocalFolder()) == 0 ? "LocalState" : "the package"));
    std::string result;
    try {
        result = RunPayload(payload, LocalFolder());
    } catch (...) {
        result = "payload threw";
    }
    Log(result);
}

void QuitToLibrary() {
    Log("quit to library");
    try {
        winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(L"anyps5:library")).get();
    } catch (winrt::hresult_error const& e) {
        Log("library launch failed " + Narrow(std::wstring(e.message())));
    }
    Sleep(300);
    Log("game part exits (quit)");
    TerminateProcess(GetCurrentProcess(), 0);
}

void WatchQuitCombo() {
    using namespace winrt::Windows::Gaming::Input;
    ULONGLONG heldSince = 0;
    for (;;) {
        bool both = false;
        try {
            for (auto const& pad : Gamepad::Gamepads()) {
                const auto buttons = pad.GetCurrentReading().Buttons;
                if ((buttons & GamepadButtons::View) == GamepadButtons::View && (buttons & GamepadButtons::Menu) == GamepadButtons::Menu) both = true;
            }
        } catch (...) {
        }
        const ULONGLONG now = GetTickCount64();
        if (!both) heldSince = 0;
        else if (!heldSince) heldSince = now;
        else if (now - heldSince >= 1500) {
            QuitToLibrary();
            return;
        }
        Sleep(50);
    }
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const& view) {
        view.Activated([](CoreApplicationView const&, winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs const& args) {
            HandleActivation(args);
            g_activated = true;
            CoreWindow::GetForCurrentThread().Activate();
        });
    }
    void Load(hstring const&) {}
    void Uninitialize() {}
    void SetWindow(CoreWindow const&) {}
    void Run() {
        auto window = CoreWindow::GetForCurrentThread();
        window.Activate();
        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        std::atomic<bool> done{false};
        const ULONGLONG start = GetTickCount64();
        while (!g_activated && GetTickCount64() - start < 5000) {
            window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            Sleep(5);
        }
        std::thread([] { WatchQuitCombo(); }).detach();
        std::thread worker([&] { RunGame(); done = true; });
        while (!done) {
            window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            Sleep(8);
        }
        worker.join();
        Log("game part exits");
        CoreApplication::Exit();
    }
};

}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment();
    const auto v = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
    Log("game part started, package " + std::to_string(v.Major) + "." + std::to_string(v.Minor) + "." + std::to_string(v.Build) + "." + std::to_string(v.Revision));
    CoreApplication::Run(make<App>());
}
