#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

std::wstring g_folder;
std::string g_json;

double Now() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

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

std::string Num(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

void WriteFileText(const std::wstring& path, const std::string& text) {
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

std::string ReadFileText(const std::wstring& path) {
    std::string s;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

void Section(const char* name, const std::string& body) {
    g_json += std::string(g_json.empty() ? "{" : ",") + Esc(name) + ":" + body;
    WriteFileText(g_folder + L"\\survey.json", g_json + "}");
}

std::string Os() {
    std::string j = "{";
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
        OSVERSIONINFOW v{sizeof v};
        fn(&v);
        j += "\"version\":\"" + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber) + "\",";
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    j += "\"page\":" + std::to_string(si.dwPageSize) + ",\"granularity\":" + std::to_string(si.dwAllocationGranularity) + ",\"logical\":" + std::to_string(si.dwNumberOfProcessors);
    return j + "}";
}

double SpinMops(unsigned long long iterations) {
    unsigned long long x = 88172645463325252ull;
    const double t0 = Now();
    for (unsigned long long i = 0; i < iterations; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    const double t = Now() - t0;
    static volatile unsigned long long sink;
    sink = x;
    return static_cast<double>(iterations) / t / 1e6;
}

std::string Cpu() {
    std::string j = "{";
    int regs[4]{};
    char brand[49]{};
    for (int i = 0; i < 3; ++i) {
        __cpuid(regs, 0x80000002 + i);
        std::memcpy(brand + i * 16, regs, 16);
    }
    j += "\"brand\":" + Esc(brand) + ",";
    __cpuid(regs, 1);
    const bool avx = (regs[2] >> 28) & 1, fma = (regs[2] >> 12) & 1, f16c = (regs[2] >> 29) & 1;
    __cpuidex(regs, 7, 0);
    const bool avx2 = (regs[1] >> 5) & 1, bmi2 = (regs[1] >> 8) & 1;
    j += "\"avx\":" + std::string(avx ? "true" : "false") + ",\"avx2\":" + (avx2 ? "true" : "false") + ",\"fma\":" + (fma ? "true" : "false") + ",\"f16c\":" + (f16c ? "true" : "false") + ",\"bmi2\":" + (bmi2 ? "true" : "false") + ",";
    DWORD_PTR process = 0, system = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
    j += "\"affinity\":" + Hex(process) + ",\"system_affinity\":" + Hex(system) + ",";
    using CpuSetFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    if (auto fn = reinterpret_cast<CpuSetFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetSystemCpuSetInformation"))) {
        ULONG size = 0;
        fn(nullptr, 0, &size, GetCurrentProcess(), 0);
        std::vector<unsigned char> buf(size ? size : 1);
        int sets = 0, allocated = 0, parked = 0, allocatedToMe = 0;
        if (size && fn(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), size, &size, GetCurrentProcess(), 0)) {
            for (ULONG at = 0; at < size;) {
                auto* e = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + at);
                if (e->Type == CpuSetInformation) {
                    ++sets;
                    if (e->CpuSet.Allocated) ++allocated;
                    if (e->CpuSet.Parked) ++parked;
                    if (e->CpuSet.AllocatedToTargetProcess) ++allocatedToMe;
                }
                at += e->Size;
            }
        }
        j += "\"cpu_sets\":{\"total\":" + std::to_string(sets) + ",\"allocated\":" + std::to_string(allocated) + ",\"parked\":" + std::to_string(parked) + ",\"mine\":" + std::to_string(allocatedToMe) + "},";
    }
    const unsigned long long iterations = 300000000ull;
    const double single = SpinMops(iterations);
    j += "\"single_mops\":" + Num(single) + ",";
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    std::string scaling;
    for (unsigned threads : {2u, 4u, 6u, 7u, 8u}) {
        if (threads > si.dwNumberOfProcessors) break;
        std::vector<std::thread> pool;
        const double t0 = Now();
        for (unsigned i = 0; i < threads; ++i) pool.emplace_back([&] { SpinMops(iterations / 2); });
        for (auto& t : pool) t.join();
        const double total = static_cast<double>(iterations / 2) * threads / (Now() - t0) / 1e6;
        scaling += std::string(scaling.empty() ? "" : ",") + "\"" + std::to_string(threads) + "\":" + Num(total / single);
    }
    j += "\"scaling_vs_one\":{" + scaling + "},";
    double slept = 0;
    for (int i = 0; i < 40; ++i) {
        const double t0 = Now();
        Sleep(1);
        slept += Now() - t0;
    }
    j += "\"sleep1_ms\":" + Num(slept / 40 * 1000);
    return j + "}";
}

std::string Memory() {
    MEMORYSTATUSEX m{sizeof m};
    GlobalMemoryStatusEx(&m);
    std::string j = "{\"total_phys_mb\":" + std::to_string(m.ullTotalPhys >> 20) + ",\"avail_phys_mb\":" + std::to_string(m.ullAvailPhys >> 20) + ",\"total_page_mb\":" + std::to_string(m.ullTotalPageFile >> 20) + ",\"avail_page_mb\":" + std::to_string(m.ullAvailPageFile >> 20) + ",";
    std::vector<void*> blocks;
    const size_t chunk = 128ull << 20;
    DWORD error = 0;
    while (blocks.size() < 128) {
        void* p = VirtualAlloc(nullptr, chunk, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) { error = GetLastError(); break; }
        blocks.push_back(p);
    }
    j += "\"max_commit_mb\":" + std::to_string(blocks.size() * (chunk >> 20)) + ",\"commit_stop_error\":" + std::to_string(error);
    for (void* p : blocks) VirtualFree(p, 0, MEM_RELEASE);
    return j + "}";
}

struct FormatName {
    DXGI_FORMAT format;
    const char* name;
};

std::string Gpu() {
    std::string j = "{";
    ComPtr<ID3D12Device> device;
    HRESULT created = E_FAIL;
    for (const auto level : {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}) {
        created = D3D12CreateDevice(nullptr, level, IID_PPV_ARGS(&device));
        if (SUCCEEDED(created)) { j += "\"feature_level\":" + Hex(level) + ","; break; }
    }
    if (!device) return j + "\"create\":" + Hex(static_cast<unsigned>(created)) + "}";
    D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o))) {
        j += "\"options\":{\"typed_uav_load_additional\":" + std::string(o.TypedUAVLoadAdditionalFormats ? "true" : "false") + ",\"rovs\":" + (o.ROVsSupported ? "true" : "false") + ",\"tiled_tier\":" + std::to_string(o.TiledResourcesTier) + ",\"binding_tier\":" + std::to_string(o.ResourceBindingTier) + ",\"conservative_raster\":" + std::to_string(o.ConservativeRasterizationTier) + ",\"min_precision\":" + std::to_string(o.MinPrecisionSupport) + ",\"double_ops\":" + (o.DoublePrecisionFloatShaderOps ? "true" : "false") + ",\"output_merger_logic\":" + (o.OutputMergerLogicOp ? "true" : "false") + ",\"vp_rt_index_any_stage\":" + (o.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation ? "true" : "false") + ",\"heap_tier\":" + std::to_string(o.ResourceHeapTier) + "},";
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
    const HRESULT h1 = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1);
    j += "\"options1\":" + (SUCCEEDED(h1) ? "{\"wave_ops\":" + std::string(o1.WaveOps ? "true" : "false") + ",\"lanes\":[" + std::to_string(o1.WaveLaneCountMin) + "," + std::to_string(o1.WaveLaneCountMax) + "],\"int64\":" + (o1.Int64ShaderOps ? "true" : "false") + "}" : Hex(static_cast<unsigned>(h1))) + ",";
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_0};
    const HRESULT hs = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm);
    j += "\"shader_model\":" + (SUCCEEDED(hs) ? Hex(sm.HighestShaderModel) : Hex(static_cast<unsigned>(hs))) + ",";
    D3D12_FEATURE_DATA_ARCHITECTURE arch{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof arch))) {
        j += "\"uma\":" + std::string(arch.UMA ? "true" : "false") + ",\"cache_coherent_uma\":" + (arch.CacheCoherentUMA ? "true" : "false") + ",\"tile_based\":" + (arch.TileBasedRenderer ? "true" : "false") + ",";
    }
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &va, sizeof va))) {
        j += "\"va_bits\":[" + std::to_string(va.MaxGPUVirtualAddressBitsPerResource) + "," + std::to_string(va.MaxGPUVirtualAddressBitsPerProcess) + "],";
    }
    static const FormatName formats[] = {
        {DXGI_FORMAT_R32_FLOAT, "R32_FLOAT"}, {DXGI_FORMAT_R32_UINT, "R32_UINT"}, {DXGI_FORMAT_R32_SINT, "R32_SINT"},
        {DXGI_FORMAT_R32G32_FLOAT, "R32G32_FLOAT"}, {DXGI_FORMAT_R32G32_UINT, "R32G32_UINT"},
        {DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT"}, {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT"}, {DXGI_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT"},
        {DXGI_FORMAT_R16_FLOAT, "R16_FLOAT"}, {DXGI_FORMAT_R16_UINT, "R16_UINT"}, {DXGI_FORMAT_R16_UNORM, "R16_UNORM"},
        {DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT"}, {DXGI_FORMAT_R16G16_UINT, "R16G16_UINT"}, {DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM"},
        {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT"}, {DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT"}, {DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM"}, {DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM"},
        {DXGI_FORMAT_R8_UNORM, "R8_UNORM"}, {DXGI_FORMAT_R8_UINT, "R8_UINT"}, {DXGI_FORMAT_R8G8_UNORM, "R8G8_UNORM"},
        {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM"}, {DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT"}, {DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM"}, {DXGI_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT"},
        {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM"}, {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM"}, {DXGI_FORMAT_R10G10B10A2_UINT, "R10G10B10A2_UINT"}, {DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT"},
    };
    std::string fmt;
    for (const auto& f : formats) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{f.format};
        const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs);
        std::string flags;
        if (SUCCEEDED(hr)) {
            if (fs.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) flags += "uav ";
            if (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) flags += "load ";
            if (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) flags += "store ";
            if (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD) flags += "atomic ";
            if (fs.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) flags += "sample ";
            if (fs.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) flags += "rt ";
            if (fs.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) flags += "blend ";
        } else {
            flags = "error";
        }
        if (!flags.empty() && flags.back() == ' ') flags.pop_back();
        fmt += std::string(fmt.empty() ? "" : ",") + Esc(f.name) + ":" + Esc(flags);
    }
    j += "\"formats\":{" + fmt + "},";
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> compute;
    const HRESULT hc = device->CreateCommandQueue(&qd, IID_PPV_ARGS(&compute));
    j += "\"compute_queue\":" + std::string(SUCCEEDED(hc) ? "true" : Hex(static_cast<unsigned>(hc))) + ",";
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        UINT64 freq = 0;
        queue->GetTimestampFrequency(&freq);
        j += "\"timestamp_hz\":" + std::to_string(freq) + ",";
        D3D12_QUERY_HEAP_DESC hd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
        ComPtr<ID3D12QueryHeap> heap;
        D3D12_HEAP_PROPERTIES props{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = 16;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        HRESULT step = device->CreateQueryHeap(&hd, IID_PPV_ARGS(&heap));
        if (SUCCEEDED(step)) step = device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
        if (SUCCEEDED(step)) step = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if (SUCCEEDED(step)) step = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
        if (SUCCEEDED(step)) step = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (SUCCEEDED(step)) {
            list->EndQuery(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            list->EndQuery(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list->ResolveQueryData(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
            list->Close();
            ID3D12CommandList* lists[] = {list.Get()};
            queue->ExecuteCommandLists(1, lists);
            queue->Signal(fence.Get(), 1);
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            fence->SetEventOnCompletion(1, event);
            const DWORD waited = WaitForSingleObject(event, 5000);
            CloseHandle(event);
            UINT64* ticks = nullptr;
            D3D12_RANGE range{0, 16};
            if (waited == WAIT_OBJECT_0 && SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&ticks)))) {
                j += "\"timestamp_query\":{\"ok\":" + std::string(ticks[1] >= ticks[0] && ticks[0] != 0 ? "true" : "false") + ",\"delta_ticks\":" + std::to_string(ticks[1] - ticks[0]) + "},";
                D3D12_RANGE none{0, 0};
                readback->Unmap(0, &none);
            } else {
                j += "\"timestamp_query\":\"wait " + std::to_string(waited) + "\",";
            }
            UINT64 gpu = 0, cpu = 0;
            j += "\"clock_calibration\":" + std::string(SUCCEEDED(queue->GetClockCalibration(&gpu, &cpu)) ? "true" : "false") + ",";
        } else {
            j += "\"timestamp_query\":" + Hex(static_cast<unsigned>(step)) + ",";
        }
    }
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE dxgi = LoadPackagedLibrary(L"dxgi.dll", 0);
    if (!dxgi) dxgi = GetModuleHandleW(L"dxgi.dll");
    auto createFactory = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (createFactory && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        j += "\"adapter\":{\"name\":" + Esc(Utf8(d.Description)) + ",\"vendor\":" + Hex(d.VendorId) + ",\"device\":" + Hex(d.DeviceId) + ",\"dedicated_mb\":" + std::to_string(d.DedicatedVideoMemory >> 20) + ",\"shared_mb\":" + std::to_string(d.SharedSystemMemory >> 20);
        ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(adapter.As(&adapter3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
            adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
            j += ",\"budget_local_mb\":" + std::to_string(local.Budget >> 20) + ",\"budget_nonlocal_mb\":" + std::to_string(shared.Budget >> 20);
        }
        j += "},";
    } else {
        j += "\"adapter\":" + std::string(dxgi ? "\"no factory\"" : "\"no dxgi\"") + ",";
    }
    j.pop_back();
    return j + "}";
}

std::string ListDir(const std::wstring& dir, int max = 30) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return "{\"error\":" + std::to_string(GetLastError()) + "}";
    int count = 0;
    std::string names;
    do {
        if (!std::wcscmp(fd.cFileName, L".") || !std::wcscmp(fd.cFileName, L"..")) continue;
        ++count;
        if (count <= max) names += std::string(names.empty() ? "" : ",") + Esc(Utf8(fd.cFileName) + ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "/" : ""));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return "{\"count\":" + std::to_string(count) + ",\"names\":[" + names + "]}";
}

std::string Storage() {
    std::string j = "{";
    wchar_t drives[512]{};
    const DWORD n = GetLogicalDriveStringsW(511, drives);
    std::string list;
    for (const wchar_t* d = drives; n && *d; d += std::wcslen(d) + 1) {
        ULARGE_INTEGER freeBytes{}, total{};
        const BOOL space = GetDiskFreeSpaceExW(d, &freeBytes, &total, nullptr);
        list += std::string(list.empty() ? "" : ",") + "[" + Esc(Utf8(d)) + "," + std::to_string(GetDriveTypeW(d)) + "," + (space ? std::to_string(total.QuadPart >> 30) + "," + std::to_string(freeBytes.QuadPart >> 30) : "null,null") + "]";
    }
    j += "\"drives\":[" + list + "],\"drives_error\":" + std::to_string(n ? 0 : GetLastError()) + ",";
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring install(exe);
    install = install.substr(0, install.find_last_of(L'\\'));
    const std::wstring parent = install.substr(0, install.find_last_of(L'\\'));
    const std::wstring grand = parent.substr(0, parent.find_last_of(L'\\'));
    std::vector<std::pair<std::string, std::wstring>> paths = {
        {"install", install}, {"install_parent", parent}, {"install_grandparent", grand},
        {"local_parent", g_folder.substr(0, g_folder.find_last_of(L'\\'))},
        {"C", L"C:"}, {"D", L"D:"}, {"E", L"E:"}, {"F", L"F:"}, {"G", L"G:"}, {"J", L"J:"}, {"S", L"S:"}, {"T", L"T:"}, {"U", L"U:"}, {"X", L"X:"}, {"Y", L"Y:"},
        {"windows_apps", L"C:\\Program Files\\WindowsApps"}, {"system32", L"C:\\Windows\\System32"}, {"u_users", L"U:\\Users"},
        {"globalroot_device", L"\\\\?\\GLOBALROOT\\Device"},
    };
    for (wchar_t letter = L'E'; letter <= L'Z'; ++letter) {
        const wchar_t root[] = {letter, L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_REMOVABLE) paths.push_back({std::string("removable_") + static_cast<char>(letter), std::wstring(root, 2)});
    }
    std::string listings;
    for (const auto& [name, path] : paths) listings += std::string(listings.empty() ? "" : ",") + Esc(name) + ":" + ListDir(path);
    j += "\"listings\":{" + listings + "},";
    ULARGE_INTEGER freeBytes{}, total{};
    if (GetDiskFreeSpaceExW(g_folder.c_str(), &freeBytes, &total, nullptr)) j += "\"local_free_gb\":" + std::to_string(freeBytes.QuadPart >> 30) + ",\"local_total_gb\":" + std::to_string(total.QuadPart >> 30) + ",";
    const std::wstring file = g_folder + L"\\speed.bin";
    const size_t chunk = 4u << 20;
    const int chunks = 64;
    void* buf = VirtualAlloc(nullptr, chunk, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    std::memset(buf, 0x5a, chunk);
    CREATEFILE2_EXTENDED_PARAMETERS ep{sizeof ep};
    ep.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    ep.dwFileFlags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    double write = -1, read = -1;
    HANDLE h = CreateFile2(file.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, &ep);
    if (h != INVALID_HANDLE_VALUE) {
        const double t0 = Now();
        DWORD done = 0;
        bool ok = true;
        for (int i = 0; i < chunks && ok; ++i) ok = WriteFile(h, buf, static_cast<DWORD>(chunk), &done, nullptr) && done == chunk;
        FlushFileBuffers(h);
        if (ok) write = static_cast<double>(chunk) * chunks / (1 << 20) / (Now() - t0);
        CloseHandle(h);
        ep.dwFileFlags = FILE_FLAG_NO_BUFFERING;
        h = CreateFile2(file.c_str(), GENERIC_READ, 0, OPEN_EXISTING, &ep);
        if (h != INVALID_HANDLE_VALUE) {
            const double t1 = Now();
            ok = true;
            for (int i = 0; i < chunks && ok; ++i) ok = ReadFile(h, buf, static_cast<DWORD>(chunk), &done, nullptr) && done == chunk;
            if (ok) read = static_cast<double>(chunk) * chunks / (1 << 20) / (Now() - t1);
            CloseHandle(h);
        }
        DeleteFileW(file.c_str());
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    j += "\"local_write_mb_s\":" + Num(write) + ",\"local_read_mb_s\":" + Num(read);
    return j + "}";
}

std::string ApiSurface() {
    using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
    auto load = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"));
    const std::wstring dir = g_folder + L"\\exports";
    CreateDirectoryW(dir.c_str(), nullptr);
    static const wchar_t* dlls[] = {
        L"kernelx.dll", L"kernel32.dll", L"kernelbase.dll", L"ntdll.dll", L"d3d12.dll", L"d3d12_x.dll", L"d3d12_xs.dll", L"d3d11.dll", L"d3d11_x.dll", L"d3d11_xs.dll", L"dxgi.dll",
        L"xg_x.dll", L"xg_xs.dll", L"xgameruntime.dll", L"xgameplatform.dll", L"gameinput.dll", L"xaudio2_9.dll", L"xaudio2_8.dll", L"xapu.dll", L"xmem.dll", L"xcrdapi.dll",
        L"combase.dll", L"ws2_32.dll", L"winhttp.dll", L"bcrypt.dll", L"d3dcompiler_47.dll", L"mfplat.dll", L"xboxgipsvc.dll", L"xboxlivecore.dll", L"microsoft.xbox.services.dll",
        L"toolhelpx.dll", L"pixevt.dll", L"ucrtbase.dll", L"vcruntime140.dll", L"msvcp140.dll",
    };
    std::string j = "{";
    for (const wchar_t* name : dlls) {
        HMODULE m = load ? load(name, nullptr, 0) : nullptr;
        if (!m) {
            j += Esc(Utf8(name)) + ":{\"error\":" + std::to_string(GetLastError()) + "},";
            continue;
        }
        const auto* base = reinterpret_cast<const unsigned char*>(m);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& entry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        std::string names;
        DWORD count = 0;
        if (entry.Size) {
            const auto* table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + entry.VirtualAddress);
            const auto* list = reinterpret_cast<const DWORD*>(base + table->AddressOfNames);
            count = table->NumberOfNames;
            for (DWORD i = 0; i < count; ++i) names += std::string(reinterpret_cast<const char*>(base + list[i])) + "\n";
        }
        WriteFileText(dir + L"\\" + name + L".txt", names);
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(m, path, MAX_PATH);
        j += Esc(Utf8(name)) + ":{\"exports\":" + std::to_string(count) + ",\"path\":" + Esc(Utf8(path)) + ",\"size\":" + std::to_string(nt->OptionalHeader.SizeOfImage) + "},";
    }
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(L"C:\\Windows\\System32\\*.dll", FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    std::string all;
    int count = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do { all += Utf8(fd.cFileName) + "\n"; ++count; } while (FindNextFileW(h, &fd));
        FindClose(h);
        WriteFileText(dir + L"\\_system32.txt", all);
    }
    j += "\"_system32_dlls\":" + std::to_string(h == INVALID_HANDLE_VALUE ? -static_cast<long long>(GetLastError()) : count);
    return j + "}";
}

std::string Network() {
    HMODULE ws = LoadPackagedLibrary(L"ws2_32.dll", 0);
    if (!ws) ws = GetModuleHandleW(L"ws2_32.dll");
    using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
    if (!ws) if (auto load = reinterpret_cast<LoadLibraryExWFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW"))) ws = load(L"ws2_32.dll", nullptr, 0);
    if (!ws) return "{\"ws2_32\":" + std::to_string(GetLastError()) + "}";
    auto pStartup = reinterpret_cast<decltype(&WSAStartup)>(GetProcAddress(ws, "WSAStartup"));
    auto pSocket = reinterpret_cast<decltype(&socket)>(GetProcAddress(ws, "socket"));
    auto pConnect = reinterpret_cast<decltype(&connect)>(GetProcAddress(ws, "connect"));
    auto pBind = reinterpret_cast<decltype(&bind)>(GetProcAddress(ws, "bind"));
    auto pListen = reinterpret_cast<decltype(&listen)>(GetProcAddress(ws, "listen"));
    auto pAccept = reinterpret_cast<decltype(&accept)>(GetProcAddress(ws, "accept"));
    auto pSelect = reinterpret_cast<decltype(&select)>(GetProcAddress(ws, "select"));
    auto pSend = reinterpret_cast<decltype(&send)>(GetProcAddress(ws, "send"));
    auto pClose = reinterpret_cast<decltype(&closesocket)>(GetProcAddress(ws, "closesocket"));
    auto pIoctl = reinterpret_cast<decltype(&ioctlsocket)>(GetProcAddress(ws, "ioctlsocket"));
    auto pError = reinterpret_cast<decltype(&WSAGetLastError)>(GetProcAddress(ws, "WSAGetLastError"));
    auto pPton = reinterpret_cast<decltype(&inet_pton)>(GetProcAddress(ws, "inet_pton"));
    auto pHtons = reinterpret_cast<decltype(&htons)>(GetProcAddress(ws, "htons"));
    WSADATA data{};
    if (pStartup(MAKEWORD(2, 2), &data) != 0) return "{\"startup\":" + std::to_string(pError()) + "}";
    std::string j = "{";
    const std::string config = ReadFileText(g_folder + L"\\net.txt");
    char ip[64]{};
    int port = 0, listenPort = 0;
    std::sscanf(config.c_str(), "%63s %d %d", ip, &port, &listenPort);
    if (ip[0] && port) {
        SOCKET s = pSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = pHtons(static_cast<u_short>(port));
        pPton(AF_INET, ip, &a.sin_addr);
        u_long nonblocking = 1;
        pIoctl(s, FIONBIO, &nonblocking);
        int rc = pConnect(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
        int err = rc == 0 ? 0 : pError();
        std::string outcome;
        if (rc == 0) outcome = "\"connected\"";
        else if (err == WSAEWOULDBLOCK) {
            fd_set w, e;
            FD_ZERO(&w); FD_ZERO(&e);
            FD_SET(s, &w); FD_SET(s, &e);
            timeval tv{5, 0};
            const int ready = pSelect(0, nullptr, &w, &e, &tv);
            bool writable = false;
            for (u_int i = 0; i < w.fd_count; ++i) writable = writable || w.fd_array[i] == s;
            outcome = ready == 0 ? "\"timeout\"" : (writable ? "\"connected\"" : "\"refused_or_error\"");
        } else outcome = std::to_string(err);
        j += "\"outbound_lan\":" + outcome + ",";
        pClose(s);
    }
    if (listenPort) {
        SOCKET s = pSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = pHtons(static_cast<u_short>(listenPort));
        int rc = pBind(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
        j += "\"bind\":" + std::to_string(rc == 0 ? 0 : pError()) + ",";
        if (rc == 0) {
            rc = pListen(s, 1);
            j += "\"listen\":" + std::to_string(rc == 0 ? 0 : pError()) + ",";
            if (rc == 0) {
                WriteFileText(g_folder + L"\\listening.txt", "1");
                fd_set r;
                FD_ZERO(&r);
                FD_SET(s, &r);
                timeval tv{40, 0};
                const int ready = pSelect(0, &r, nullptr, nullptr, &tv);
                if (ready > 0) {
                    SOCKET c = pAccept(s, nullptr, nullptr);
                    std::vector<char> block(1 << 20, 'x');
                    long long sent = 0;
                    const double t0 = Now();
                    while (sent < (64ll << 20)) {
                        const int k = pSend(c, block.data(), static_cast<int>(block.size()), 0);
                        if (k <= 0) break;
                        sent += k;
                    }
                    j += "\"inbound\":{\"sent_mb\":" + std::to_string(sent >> 20) + ",\"mb_s\":" + Num(static_cast<double>(sent) / (1 << 20) / (Now() - t0)) + "},";
                    pClose(c);
                } else {
                    j += "\"inbound\":\"no connection in 40 s\",";
                }
            }
        }
        pClose(s);
    }
    if (j.back() == ',') j.pop_back();
    return j + "}";
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder, char* out, size_t size) {
    g_folder = folder;
    g_json.clear();
    Section("os", Os());
    Section("gpu", Gpu());
    Section("storage", Storage());
    Section("api", ApiSurface());
    Section("cpu", Cpu());
    Section("memory", Memory());
    Section("network", Network());
    const std::string done = "{\"survey\":\"survey.json\",\"bytes\":" + std::to_string(g_json.size() + 1) + "}";
    if (out && size > done.size()) std::memcpy(out, done.c_str(), done.size() + 1);
    return 0;
}
