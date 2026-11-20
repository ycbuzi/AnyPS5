#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Diagnostics.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#pragma comment(lib, "runtimeobject")
#pragma comment(lib, "ole32")
#pragma comment(lib, "oleaut32")

using namespace winrt;

namespace {

struct Call {
    std::function<std::string()> body;
    std::string result;
};

DWORD WINAPI Runner(void* param) {
    auto* call = static_cast<Call*>(param);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        call->result = call->body();
    } catch (const winrt::hresult_error& e) {
        char b[32];
        std::snprintf(b, sizeof b, "\"hr 0x%08lx\"", static_cast<unsigned long>(e.code().value));
        call->result = b;
    } catch (...) {
        call->result = "\"exception\"";
    }
    return 0;
}

std::string Timed(std::function<std::string()> body) {
    auto* call = new Call{std::move(body), {}};
    HANDLE thread = CreateThread(nullptr, 0, Runner, call, 0, nullptr);
    if (!thread) return "\"no thread\"";
    if (WaitForSingleObject(thread, 8000) != WAIT_OBJECT_0) {
        CloseHandle(thread);
        return "\"hang\"";
    }
    CloseHandle(thread);
    std::string r = call->result;
    delete call;
    return r;
}

std::string N(unsigned long long v) { return std::to_string(v); }

void Save(const wchar_t* folder, const std::string& j) {
    if (FILE* f = _wfopen((std::wstring(folder) + L"\winrtcall.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    using namespace Windows;
    std::string j = "{\"started\":1,";
    Save(folder, j);
    j += "\"device_family_first\":" + Timed([] { return "\"" + to_string(System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily()) + "\""; }) + ",";
    Save(folder, j);
    j += "\"packages_for_user\":" + Timed([] {
        unsigned n = 0;
        for (auto&& p : Management::Deployment::PackageManager().FindPackagesForUser(L"")) { (void)p; ++n; }
        return N(n);
    }) + ",";
    Save(folder, j);
    j += "\"process_diagnostics\":" + Timed([] { return N(System::Diagnostics::ProcessDiagnosticInfo::GetForProcesses().Size()); }) + ",";
    Save(folder, j);
    j += "\"system_memory_mb\":" + Timed([] {
        auto r = System::Diagnostics::SystemDiagnosticInfo::GetForCurrentSystem().MemoryUsage().GetReport();
        return "[" + N(r.TotalPhysicalSizeInBytes() >> 20) + "," + N(r.AvailableSizeInBytes() >> 20) + "]";
    }) + ",";
    Save(folder, j);
    j += "\"system_cpu_idle_ms\":" + Timed([] {
        auto r = System::Diagnostics::SystemDiagnosticInfo::GetForCurrentSystem().CpuUsage().GetReport();
        return N(std::chrono::duration_cast<std::chrono::milliseconds>(r.IdleTime()).count());
    }) + ",";
    Save(folder, j);
    j += "\"device_family\":" + Timed([] { return "\"" + to_string(System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily()) + "\""; }) + ",";
    Save(folder, j);
    j += "\"app_diagnostics\":" + Timed([] { return N(System::AppDiagnosticInfo::RequestInfoAsync().get().Size()); }) + ",";
    Save(folder, j);
    j += "\"app_diagnostics_access\":" + Timed([] { return N(static_cast<int>(System::AppDiagnosticInfo::RequestAccessAsync().get())); }) + ",";
    Save(folder, j);
    j += "\"users\":" + Timed([] { return N(System::User::FindAllAsync().get().Size()); }) + ",";
    Save(folder, j);
    j += "\"gamepads\":" + Timed([] { return N(Gaming::Input::Gamepad::Gamepads().Size()); }) + ",";
    Save(folder, j);
    j += "\"raw_controllers\":" + Timed([] { return N(Gaming::Input::RawGameController::RawGameControllers().Size()); }) + ",";
    Save(folder, j);
    j += "\"devices\":" + Timed([] { return N(Devices::Enumeration::DeviceInformation::FindAllAsync().get().Size()); }) + ",";
    Save(folder, j);
    j += "\"host_names\":" + Timed([] { return N(Networking::Connectivity::NetworkInformation::GetHostNames().Size()); }) + ",";
    Save(folder, j);
    j += "\"connection_profiles\":" + Timed([] { return N(Networking::Connectivity::NetworkInformation::GetConnectionProfiles().Size()); }) + ",";
    Save(folder, j);
    j += "\"launcher_options\":" + Timed([] { return N(System::Launcher::QueryUriSupportAsync(Foundation::Uri(L"ms-settings:"), System::LaunchQuerySupportType::Uri).get() == System::LaunchQuerySupportStatus::Available ? 1 : 0); }) + "}";
    if (FILE* f = _wfopen((std::wstring(folder) + L"\\winrtcall.json").c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    if (out && size > j.size()) std::memcpy(out, j.c_str(), j.size() + 1);
    return 0;
}
