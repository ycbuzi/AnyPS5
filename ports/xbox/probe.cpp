#include <mutex>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.UI.Core.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>
#include "shaders/shaders.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

namespace {

std::string Hex(std::uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "\"0x%llx\"", static_cast<unsigned long long>(v));
    return b;
}

std::string Bool(bool v) { return v ? "true" : "false"; }

std::string Cpu() {
    int r[4]{};
    char brand[49]{};
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, 0x80000002 + i);
        std::memcpy(brand + i * 16, r, 16);
    }
    __cpuid(r, 1);
    const int ecx1 = r[2];
    __cpuidex(r, 7, 0);
    const int ebx7 = r[1];
    const bool osAvx = (ecx1 & (1 << 27)) && ((_xgetbv(0) & 6) == 6);
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    std::string s = "{\"brand\":\"" + std::string(brand) + "\",\"threads\":" + std::to_string(si.dwNumberOfProcessors);
    s += ",\"sse42\":" + Bool(ecx1 & (1 << 20)) + ",\"popcnt\":" + Bool(ecx1 & (1 << 23)) + ",\"avx\":" + Bool(osAvx);
    s += ",\"f16c\":" + Bool(ecx1 & (1 << 29)) + ",\"fma\":" + Bool(ecx1 & (1 << 12)) + ",\"movbe\":" + Bool(ecx1 & (1 << 22));
    s += ",\"avx2\":" + Bool(osAvx && (ebx7 & (1 << 5))) + ",\"bmi1\":" + Bool(ebx7 & (1 << 3)) + ",\"bmi2\":" + Bool(ebx7 & (1 << 8));
    s += ",\"lowest_address\":" + Hex(reinterpret_cast<std::uint64_t>(si.lpMinimumApplicationAddress));
    s += ",\"highest_address\":" + Hex(reinterpret_cast<std::uint64_t>(si.lpMaximumApplicationAddress)) + "}";
    return s;
}

std::string Memory() {
    using winrt::Windows::System::MemoryManager;
    MEMORYSTATUSEX ms{sizeof ms};
    GlobalMemoryStatusEx(&ms);
    std::string s = "{\"app_limit\":" + std::to_string(MemoryManager::AppMemoryUsageLimit());
    s += ",\"app_usage\":" + std::to_string(MemoryManager::AppMemoryUsage());
    s += ",\"total_phys\":" + std::to_string(ms.ullTotalPhys) + ",\"avail_phys\":" + std::to_string(ms.ullAvailPhys);
    s += ",\"total_virtual\":" + std::to_string(ms.ullTotalVirtual) + "}";
    return s;
}

std::string Reserve(std::uint64_t base, std::uint64_t size) {
    void* p = VirtualAlloc2FromApp(GetCurrentProcess(), reinterpret_cast<void*>(base), size, MEM_RESERVE, PAGE_NOACCESS, nullptr, 0);
    const DWORD err = p ? 0 : GetLastError();
    std::string s = "{\"base\":" + Hex(base) + ",\"size\":" + Hex(size) + ",\"ok\":" + Bool(p == reinterpret_cast<void*>(base));
    s += ",\"got\":" + Hex(reinterpret_cast<std::uint64_t>(p)) + ",\"error\":" + std::to_string(err) + "}";
    if (p) VirtualFree(p, 0, MEM_RELEASE);
    return s;
}

std::string Executable() {
    const std::uint8_t code[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    void* p = VirtualAlloc2FromApp(GetCurrentProcess(), nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, nullptr, 0);
    if (!p) return "{\"ok\":false,\"step\":\"alloc\",\"error\":" + std::to_string(GetLastError()) + "}";
    std::memcpy(p, code, sizeof code);
    ULONG old = 0;
    if (!VirtualProtectFromApp(p, 4096, PAGE_EXECUTE_READ, &old)) {
        const DWORD err = GetLastError();
        VirtualFree(p, 0, MEM_RELEASE);
        return "{\"ok\":false,\"step\":\"protect\",\"error\":" + std::to_string(err) + "}";
    }
    FlushInstructionCache(GetCurrentProcess(), p, sizeof code);
    const int v = reinterpret_cast<int (*)()>(p)();
    VirtualFree(p, 0, MEM_RELEASE);
    return "{\"ok\":" + Bool(v == 42) + ",\"value\":" + std::to_string(v) + "}";
}

std::string ComputeTest(ID3D12Device* device, const unsigned char* code, size_t size) {
    char buffer[160];
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    com_ptr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), error.put());
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"rootsig\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    com_ptr<ID3D12RootSignature> root;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"create_rootsig\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.get();
    psoDesc.CS = {code, size};
    com_ptr<ID3D12PipelineState> pso;
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pso.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"pso\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_HEAP_PROPERTIES defaultHeap{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_HEAP_PROPERTIES readbackHeap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    com_ptr<ID3D12Resource> target, readback;
    hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(target.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"buffer\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"readback\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_COMPUTE};
    com_ptr<ID3D12CommandQueue> queue;
    com_ptr<ID3D12CommandAllocator> allocator;
    com_ptr<ID3D12GraphicsCommandList> list;
    com_ptr<ID3D12Fence> fence;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.put()));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(allocator.put()));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.get(), pso.get(), IID_PPV_ARGS(list.put()));
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    if (!queue || !list || !fence) return "{\"stage\":\"queue\"}";
    list->SetComputeRootSignature(root.get());
    list->SetComputeRootUnorderedAccessView(0, target->GetGPUVirtualAddress());
    list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.get(), target.get());
    list->Close();
    ID3D12CommandList* lists[] = {list.get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    const DWORD wait = WaitForSingleObject(event, 5000);
    CloseHandle(event);
    if (wait != WAIT_OBJECT_0) return "{\"stage\":\"wait\"}";
    std::uint32_t* values = nullptr;
    D3D12_RANGE range{0, 256};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&values)))) return "{\"stage\":\"map\"}";
    std::snprintf(buffer, sizeof buffer, "{\"stage\":\"ok\",\"v0\":%u,\"v1\":%u,\"v63\":%u}", values[0], values[1], values[63]);
    readback->Unmap(0, nullptr);
    return buffer;
}

std::mutex g_deviceLock;
com_ptr<ID3D12Device> g_device;
HRESULT g_deviceHr = E_FAIL;

com_ptr<ID3D12Device> SharedDevice() {
    std::lock_guard<std::mutex> lock(g_deviceLock);
    if (g_device) return g_device;
    com_ptr<IDXGIFactory4> factory;
    com_ptr<IDXGIAdapter1> adapter;
    g_deviceHr = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put()));
    if (SUCCEEDED(g_deviceHr)) g_deviceHr = factory->EnumAdapters1(0, adapter.put());
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    if (FAILED(g_deviceHr)) return g_device;
    for (const auto level : levels) {
        g_deviceHr = D3D12CreateDevice(adapter.get(), level, IID_PPV_ARGS(g_device.put()));
        if (SUCCEEDED(g_deviceHr)) break;
    }
    return g_device;
}

std::string Gpu() {
    com_ptr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())))) return "{\"error\":\"no dxgi factory\"}";
    com_ptr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, adapter.put()))) return "{\"error\":\"no adapter\"}";
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    char name[128]{};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name - 1, nullptr, nullptr);
    std::string s = "{\"adapter\":\"" + std::string(name) + "\",\"vram\":" + std::to_string(desc.DedicatedVideoMemory);
    s += ",\"shared\":" + std::to_string(desc.SharedSystemMemory);
    com_ptr<ID3D12Device> device = SharedDevice();
    if (!device) return s + ",\"device\":false,\"hr\":" + Hex(static_cast<std::uint32_t>(g_deviceHr)) + "}";
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
    const D3D_FEATURE_LEVEL asked[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    fl.NumFeatureLevels = 5;
    fl.pFeatureLevelsRequested = asked;
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof fl))) s += ",\"feature_level\":" + Hex(fl.MaxSupportedFeatureLevel);
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_5_1};
    for (const auto model : {D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0}) {
        D3D12_FEATURE_DATA_SHADER_MODEL query{model};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &query, sizeof query))) {
            sm = query;
            break;
        }
    }
    s += ",\"shader_model\":" + Hex(sm.HighestShaderModel);
    D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o);
    s += ",\"resource_binding_tier\":" + std::to_string(o.ResourceBindingTier) + ",\"tiled_resources_tier\":" + std::to_string(o.TiledResourcesTier);
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1);
    s += ",\"wave_ops\":" + Bool(o1.WaveOps) + ",\"wave_lanes_min\":" + std::to_string(o1.WaveLaneCountMin) + ",\"wave_lanes_max\":" + std::to_string(o1.WaveLaneCountMax);
    s += ",\"int64_shader_ops\":" + Bool(o1.Int64ShaderOps);
    s += ",\"cs_6_0\":" + ComputeTest(device.get(), kCs60, sizeof kCs60) + ",\"cs_5_1\":" + ComputeTest(device.get(), kCs51, sizeof kCs51) + "}";
    return s;
}

void Marker(const wchar_t* name) {
    try {
        const auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        const std::wstring path = std::wstring(folder) + L"\\" + name;
        if (FILE* f = _wfopen(path.c_str(), L"wb")) {
            std::fprintf(f, "%llu\n", static_cast<unsigned long long>(GetTickCount64()));
            std::fclose(f);
        }
    } catch (...) {
    }
}

struct Presenter {
    com_ptr<ID3D12Device> device;
    com_ptr<ID3D12CommandQueue> queue;
    com_ptr<IDXGISwapChain1> swapChain;
    bool ok = false;

    explicit Presenter(CoreWindow const& window) {
        com_ptr<IDXGIFactory2> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())))) return;
        device = SharedDevice();
        if (!device) return;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.put())))) return;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 1920;
        desc.Height = 1080;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        ok = SUCCEEDED(factory->CreateSwapChainForCoreWindow(queue.get(), winrt::get_unknown(window), &desc, nullptr, swapChain.put()));
    }

    void Present() {
        if (ok) swapChain->Present(1, 0);
        else Sleep(16);
    }
};

#include "pso_report.inc"

void WriteReport() {
    const auto family = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily();
    const auto version = std::stoull(winrt::to_string(winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamilyVersion()));
    std::string j = "{\"device_family\":\"" + winrt::to_string(family) + "\"";
    j += ",\"os\":\"" + std::to_string(version >> 48) + "." + std::to_string((version >> 32) & 0xFFFF) + "." + std::to_string((version >> 16) & 0xFFFF) + "." + std::to_string(version & 0xFFFF) + "\"";
    j += ",\"cpu\":" + Cpu() + ",\"memory\":" + Memory();
    j += ",\"reserve\":[" + Reserve(0x1000000000ull, 0x10000000ull) + "," + Reserve(0x1000000000ull, 0x7FC0000000ull) + "," + Reserve(0x400000ull, 0x10000000ull) + "]";
    j += ",\"executable\":" + Executable() + ",\"gpu\":" + Gpu() + "}";
    const auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
    const std::wstring path = std::wstring(folder) + L"\\probe.json";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
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

std::string RunPayload(const std::wstring& folder) {
    const std::wstring path = folder + L"\\payload.dll";
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return "\"no payload\"";
    std::string image;
    char chunk[65536];
    for (size_t got; (got = std::fread(chunk, 1, sizeof chunk, file)) != 0;) image.append(chunk, got);
    std::fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (image.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) return "\"bad dos header\"";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return "\"bad nt header\"";
    const SIZE_T size = nt->OptionalHeader.SizeOfImage;
    auto* base = static_cast<unsigned char*>(VirtualAllocFromApp(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base) return "\"alloc failed " + std::to_string(GetLastError()) + "\"";
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
    if (!missing.empty()) return "{\"missing_imports\":\"" + missing + "\"}";
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD c = section->Characteristics;
        ULONG protect = PAGE_READONLY;
        if (c & IMAGE_SCN_MEM_EXECUTE) protect = (c & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (c & IMAGE_SCN_MEM_WRITE) protect = PAGE_READWRITE;
        ULONG old = 0;
        const SIZE_T length = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        if (length && !VirtualProtectFromApp(base + section->VirtualAddress, length, protect, &old)) return "\"protect failed " + std::to_string(GetLastError()) + "\"";
    }
    const auto& exceptions = directories[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.Size) {
        RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(base + exceptions.VirtualAddress), exceptions.Size / sizeof(RUNTIME_FUNCTION), reinterpret_cast<DWORD64>(base));
    }
    using DllMainFn = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    if (nt->OptionalHeader.AddressOfEntryPoint) {
        auto entry = reinterpret_cast<DllMainFn>(base + nt->OptionalHeader.AddressOfEntryPoint);
        if (!entry(reinterpret_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr)) return "\"dllmain failed\"";
    }
    const auto& exports = directories[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exports.Size) return "\"no exports\"";
    const auto* table = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exports.VirtualAddress);
    const auto* names = reinterpret_cast<const DWORD*>(base + table->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + table->AddressOfNameOrdinals);
    const auto* functions = reinterpret_cast<const DWORD*>(base + table->AddressOfFunctions);
    for (DWORD i = 0; i < table->NumberOfNames; ++i) {
        if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), "PayloadMain") == 0) {
            auto main = reinterpret_cast<PayloadMain>(base + functions[ordinals[i]]);
            std::string output(1 << 20, static_cast<char>(0));
            const int code = main(folder.c_str(), output.data(), output.size());
            output.resize(std::strlen(output.c_str()));
            return "{\"result\":" + std::to_string(code) + ",\"output\":" + (output.empty() ? std::string("null") : output) + "}";
        }
    }
    return "\"no PayloadMain\"";
}

void WritePayloadReport() {
    const std::wstring folder = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
    std::string result;
    try {
        result = RunPayload(folder);
    } catch (...) {
        result = "\"payload threw\"";
    }
    const std::wstring path = folder + L"\\loader.json";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(result.data(), 1, result.size(), f);
        std::fclose(f);
    }
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const& view) {
        view.Activated([](CoreApplicationView const&, winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs const&) {
            Marker(L"activated.txt");
            CoreWindow::GetForCurrentThread().Activate();
        });
    }
    void Load(hstring const&) {}
    void Uninitialize() {}
    void SetWindow(CoreWindow const&) { Marker(L"setwindow.txt"); }
    void Run() {
        Marker(L"run.txt");
        auto window = CoreWindow::GetForCurrentThread();
        window.Activate();
        Presenter presenter(window);
        presenter.Present();
        Marker(presenter.ok ? L"present-ok.txt" : L"present-failed.txt");
        std::atomic<bool> done{false};
        std::thread worker([&] { WriteReport(); WritePsoReport(); WritePayloadReport(); done = true; });
        while (!done) {
            window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            presenter.Present();
        }
        worker.join();
        for (int i = 0; i < 300; ++i) {
            window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            presenter.Present();
        }
        CoreApplication::Exit();
    }
};

}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment();
    Marker(L"main.txt");
    CoreApplication::Run(make<App>());
}
