#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.management.deployment.h>
#include <windows.system.diagnostics.h>
#include <windows.gaming.input.h>
#include <windows.system.h>
#include <cstdio>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
namespace AF = ABI::Windows::Foundation;
namespace AFC = ABI::Windows::Foundation::Collections;

namespace {

std::wstring g_folder;
std::string g_json = "{";

void Save() {
    if (FILE* f = _wfopen((g_folder + L"\\winabi.json").c_str(), L"wb")) {
        const std::string closed = g_json + "\"end\":1}";
        std::fwrite(closed.data(), 1, closed.size(), f);
        std::fclose(f);
    }
}

std::string Hr(HRESULT hr) {
    char b[24];
    std::snprintf(b, sizeof b, "\"hr 0x%08lx\"", static_cast<unsigned long>(hr));
    return b;
}

void Put(const char* key, const std::string& value) {
    g_json += std::string("\"") + key + "\":" + value + ",";
    Save();
}

template <typename T>
HRESULT Factory(const wchar_t* cls, ComPtr<T>& out) {
    return RoGetActivationFactory(HStringReference(cls).Get(), __uuidof(T), reinterpret_cast<void**>(out.GetAddressOf()));
}

template <typename Op>
HRESULT Wait(Op* op, DWORD ms) {
    ComPtr<AF::IAsyncInfo> info;
    HRESULT hr = op->QueryInterface(IID_PPV_ARGS(&info));
    if (FAILED(hr)) return hr;
    const ULONGLONG end = GetTickCount64() + ms;
    AsyncStatus status = AsyncStatus::Started;
    while (SUCCEEDED(info->get_Status(&status)) && status == AsyncStatus::Started) {
        if (GetTickCount64() > end) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        Sleep(20);
    }
    if (status != AsyncStatus::Completed) {
        HRESULT error = E_FAIL;
        info->get_ErrorCode(&error);
        return error;
    }
    return S_OK;
}

void Packages() {
    ComPtr<IInspectable> instance;
    HRESULT hr = RoActivateInstance(HStringReference(RuntimeClass_Windows_Management_Deployment_PackageManager).Get(), &instance);
    if (FAILED(hr)) return Put("packages_for_user", Hr(hr));
    ComPtr<ABI::Windows::Management::Deployment::IPackageManager> manager;
    instance.As(&manager);
    ComPtr<AFC::IIterable<ABI::Windows::ApplicationModel::Package*>> packages;
    hr = manager->FindPackagesByUserSecurityId(HStringReference(L"").Get(), &packages);
    if (FAILED(hr)) return Put("packages_for_user", Hr(hr));
    ComPtr<AFC::IIterator<ABI::Windows::ApplicationModel::Package*>> it;
    packages->First(&it);
    boolean has = false;
    int count = 0;
    for (it->get_HasCurrent(&has); has; it->MoveNext(&has)) ++count;
    Put("packages_for_user", std::to_string(count));
}

void Processes() {
    ComPtr<ABI::Windows::System::Diagnostics::IProcessDiagnosticInfoStatics> statics;
    HRESULT hr = Factory(RuntimeClass_Windows_System_Diagnostics_ProcessDiagnosticInfo, statics);
    if (FAILED(hr)) return Put("process_diagnostics", Hr(hr));
    ComPtr<AFC::IVectorView<ABI::Windows::System::Diagnostics::ProcessDiagnosticInfo*>> list;
    hr = statics->GetForProcesses(&list);
    if (FAILED(hr)) return Put("process_diagnostics", Hr(hr));
    unsigned n = 0;
    list->get_Size(&n);
    Put("process_diagnostics", std::to_string(n));
}

void Memory() {
    ComPtr<ABI::Windows::System::Diagnostics::ISystemDiagnosticInfoStatics> statics;
    HRESULT hr = Factory(RuntimeClass_Windows_System_Diagnostics_SystemDiagnosticInfo, statics);
    if (FAILED(hr)) return Put("system_memory_mb", Hr(hr));
    ComPtr<ABI::Windows::System::Diagnostics::ISystemDiagnosticInfo> info;
    hr = statics->GetForCurrentSystem(&info);
    if (FAILED(hr)) return Put("system_memory_mb", Hr(hr));
    ComPtr<ABI::Windows::System::Diagnostics::ISystemMemoryUsage> usage;
    ComPtr<ABI::Windows::System::Diagnostics::ISystemMemoryUsageReport> report;
    hr = info->get_MemoryUsage(&usage);
    if (SUCCEEDED(hr)) hr = usage->GetReport(&report);
    if (FAILED(hr)) return Put("system_memory_mb", Hr(hr));
    UINT64 total = 0, available = 0;
    report->get_TotalPhysicalSizeInBytes(&total);
    report->get_AvailableSizeInBytes(&available);
    Put("system_memory_mb", "[" + std::to_string(total >> 20) + "," + std::to_string(available >> 20) + "]");
}

void Gamepads() {
    ComPtr<ABI::Windows::Gaming::Input::IGamepadStatics> statics;
    HRESULT hr = Factory(RuntimeClass_Windows_Gaming_Input_Gamepad, statics);
    if (FAILED(hr)) return Put("gamepads", Hr(hr));
    ComPtr<AFC::IVectorView<ABI::Windows::Gaming::Input::Gamepad*>> list;
    hr = statics->get_Gamepads(&list);
    if (FAILED(hr)) return Put("gamepads", Hr(hr));
    unsigned n = 0;
    list->get_Size(&n);
    Put("gamepads", std::to_string(n));
}

void Users() {
    ComPtr<ABI::Windows::System::IUserStatics> statics;
    HRESULT hr = Factory(RuntimeClass_Windows_System_User, statics);
    if (FAILED(hr)) return Put("users", Hr(hr));
    ComPtr<AF::IAsyncOperation<AFC::IVectorView<ABI::Windows::System::User*>*>> op;
    hr = statics->FindAllAsync(&op);
    if (SUCCEEDED(hr)) hr = Wait(op.Get(), 8000);
    if (FAILED(hr)) return Put("users", Hr(hr));
    ComPtr<AFC::IVectorView<ABI::Windows::System::User*>> list;
    op->GetResults(&list);
    unsigned n = 0;
    if (list) list->get_Size(&n);
    Put("users", std::to_string(n));
}

void AppDiagnostics() {
    ComPtr<ABI::Windows::System::IAppDiagnosticInfoStatics> statics;
    HRESULT hr = Factory(RuntimeClass_Windows_System_AppDiagnosticInfo, statics);
    if (FAILED(hr)) return Put("app_diagnostics", Hr(hr));
    ComPtr<AF::IAsyncOperation<AFC::IVector<ABI::Windows::System::AppDiagnosticInfo*>*>> op;
    hr = statics->RequestInfoAsync(&op);
    if (SUCCEEDED(hr)) hr = Wait(op.Get(), 8000);
    if (FAILED(hr)) return Put("app_diagnostics", Hr(hr));
    ComPtr<AFC::IVector<ABI::Windows::System::AppDiagnosticInfo*>> list;
    op->GetResults(&list);
    unsigned n = 0;
    if (list) list->get_Size(&n);
    Put("app_diagnostics", std::to_string(n));
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    Put("started", "1");
    const HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    Put("ro_init", Hr(init));
    Memory();
    Gamepads();
    Processes();
    Users();
    AppDiagnostics();
    Packages();
    const std::string done = "{\"winabi\":\"winabi.json\"}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
