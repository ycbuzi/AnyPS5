#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

const char kBasic[] =
    "RWByteAddressBuffer o : register(u0);\n"
    "[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) { o.Store(id.x * 4, id.x * 3 + 64); }\n";

const char kGuest[] =
    "RWByteAddressBuffer o : register(u0);\n"
    "RWByteAddressBuffer g : register(u1);\n"
    "cbuffer C : register(b0) { uint base; uint salt; uint pad0; uint pad1; };\n"
    "[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint a = base + id.x * 4; g.Store(a, id.x ^ salt); DeviceMemoryBarrier(); o.Store(id.x * 4, g.Load(a)); }\n";

const char kBallot[] =
    "RWByteAddressBuffer o : register(u0);\n"
    "groupshared uint m[2];\n"
    "[numthreads(64,1,1)] void main(uint3 id : SV_GroupThreadID) {\n"
    "  if (id.x < 2) m[id.x] = 0; GroupMemoryBarrierWithGroupSync();\n"
    "  if ((id.x % 3) == 0) InterlockedOr(m[id.x >> 5], 1u << (id.x & 31)); GroupMemoryBarrierWithGroupSync();\n"
    "  o.Store(id.x * 4, m[id.x >> 5]); }\n";

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;
    HANDLE event = nullptr;
    UINT64 value = 0;
};

std::string Hex(HRESULT hr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08lx", static_cast<unsigned long>(hr));
    return text;
}

HRESULT Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&out));
}

HRESULT Wait(Gpu& gpu) {
    HRESULT hr = gpu.queue->Signal(gpu.fence.Get(), ++gpu.value);
    if (FAILED(hr)) return hr;
    if (gpu.fence->GetCompletedValue() < gpu.value) {
        gpu.fence->SetEventOnCompletion(gpu.value, gpu.event);
        if (WaitForSingleObject(gpu.event, 10000) != WAIT_OBJECT_0) return E_ABORT;
    }
    return gpu.device->GetDeviceRemovedReason();
}

std::string Run(Gpu& gpu, const char* source, size_t length, ID3D12Resource* guest, UINT base, UINT salt, std::vector<UINT>& values) {
    values.clear();
    ComPtr<ID3DBlob> code, errors;
    const DWORD start = GetTickCount();
    HRESULT hr = D3DCompile(source, length, "payload", nullptr, nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    const DWORD compileMs = GetTickCount() - start;
    if (FAILED(hr)) return "{\"compile\":\"" + Hex(hr) + "\"}";
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = gpu.root.Get();
    pso.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> state;
    const DWORD psoStart = GetTickCount();
    if (FAILED(hr = gpu.device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&state)))) return "{\"pso\":\"" + Hex(hr) + "\"}";
    const DWORD psoMs = GetTickCount() - psoStart;
    ComPtr<ID3D12Resource> output, readback;
    if (FAILED(hr = Buffer(gpu.device.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output))) return "{\"output\":\"" + Hex(hr) + "\"}";
    if (FAILED(hr = Buffer(gpu.device.Get(), 256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, readback))) return "{\"readback\":\"" + Hex(hr) + "\"}";
    gpu.allocator->Reset();
    gpu.list->Reset(gpu.allocator.Get(), state.Get());
    gpu.list->SetComputeRootSignature(gpu.root.Get());
    gpu.list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
    gpu.list->SetComputeRootUnorderedAccessView(1, guest ? guest->GetGPUVirtualAddress() : output->GetGPUVirtualAddress());
    const UINT constants[4] = {base, salt, 0, 0};
    gpu.list->SetComputeRoot32BitConstants(2, 4, constants, 0);
    gpu.list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    gpu.list->ResourceBarrier(1, &barrier);
    gpu.list->CopyResource(readback.Get(), output.Get());
    gpu.list->Close();
    ID3D12CommandList* lists[] = {gpu.list.Get()};
    const DWORD runStart = GetTickCount();
    gpu.queue->ExecuteCommandLists(1, lists);
    if (FAILED(hr = Wait(gpu))) return "{\"wait\":\"" + Hex(hr) + "\"}";
    const DWORD runMs = GetTickCount() - runStart;
    void* mapped = nullptr;
    D3D12_RANGE range{0, 256};
    if (FAILED(hr = readback->Map(0, &range, &mapped))) return "{\"map\":\"" + Hex(hr) + "\"}";
    values.assign(static_cast<const UINT*>(mapped), static_cast<const UINT*>(mapped) + 64);
    readback->Unmap(0, nullptr);
    char text[128];
    std::snprintf(text, sizeof text, "{\"compile_ms\":%lu,\"pso_ms\":%lu,\"run_ms\":%lu,\"dxbc\":%zu}", compileMs, psoMs, runMs, code->GetBufferSize());
    return text;
}

std::string Main() {
    Gpu gpu;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device));
    if (FAILED(hr)) return "{\"device\":\"" + Hex(hr) + "\",\"hint\":\"App mode\"}";
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    if (FAILED(hr = gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)))) return "{\"queue\":\"" + Hex(hr) + "\"}";
    gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&gpu.allocator));
    gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.list));
    gpu.list->Close();
    gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence));
    gpu.event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].Descriptor.ShaderRegister = 1;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.Num32BitValues = 4;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))) return "{\"rootsig\":\"" + Hex(hr) + "\"}";
    if (FAILED(hr = gpu.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gpu.root)))) return "{\"rootsig2\":\"" + Hex(hr) + "\"}";
    std::string json = "{\"device\":\"ok\"";
    std::vector<UINT> values;
    std::string info = Run(gpu, kBasic, sizeof kBasic - 1, nullptr, 0, 0, values);
    bool ok = values.size() == 64;
    for (UINT i = 0; ok && i < 64; ++i) ok = values[i] == i * 3 + 64;
    json += ",\"basic\":" + info + ",\"basic_ok\":" + (ok ? "true" : "false");
    info = Run(gpu, kBallot, sizeof kBallot - 1, nullptr, 0, 0, values);
    UINT expect[2] = {0, 0};
    for (UINT i = 0; i < 64; ++i) {
        if (i % 3 == 0) expect[i >> 5] |= 1u << (i & 31);
    }
    ok = values.size() == 64;
    for (UINT i = 0; ok && i < 64; ++i) ok = values[i] == expect[i >> 5];
    json += ",\"ballot\":" + info + ",\"ballot_ok\":" + (ok ? "true" : "false");
    const UINT64 sizes[] = {0xFFFF0000ull, 0xC0000000ull, 0x80000000ull, 0x40000000ull, 0x20000000ull};
    json += ",\"guest\":[";
    bool first = true;
    bool done = false;
    for (UINT64 size : sizes) {
        if (done) break;
        ComPtr<ID3D12Resource> guest;
        const DWORD start = GetTickCount();
        hr = Buffer(gpu.device.Get(), size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, guest);
        char head[128];
        std::snprintf(head, sizeof head, "%s{\"mb\":%llu,\"create\":\"%s\",\"create_ms\":%lu", first ? "" : ",", size >> 20, Hex(hr).c_str(), GetTickCount() - start);
        first = false;
        json += head;
        if (SUCCEEDED(hr)) {
            const UINT base = static_cast<UINT>(size - 256);
            info = Run(gpu, kGuest, sizeof kGuest - 1, guest.Get(), base, 0xA5A5u, values);
            ok = values.size() == 64;
            for (UINT i = 0; ok && i < 64; ++i) ok = values[i] == (i ^ 0xA5A5u);
            json += ",\"run\":" + info + ",\"ok\":" + (ok ? "true" : "false");
            done = true;
        }
        json += "}";
    }
    json += "],\"removed\":\"" + Hex(gpu.device->GetDeviceRemovedReason()) + "\"}";
    CloseHandle(gpu.event);
    return json;
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t*, char* out, size_t capacity) {
    std::string json;
    try {
        json = Main();
    } catch (...) {
        json = "{\"threw\":true}";
    }
    std::snprintf(out, capacity, "%s", json.c_str());
    return 1;
}
