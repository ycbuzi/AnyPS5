#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const char kSource[] =
    "RWByteAddressBuffer output : register(u0);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { output.Store(id.x * 4, id.x * 3 + 64); }\n";

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t*, char* out, size_t capacity) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const DWORD start = GetTickCount();
    const HRESULT hr = D3DCompile(kSource, sizeof kSource - 1, "payload", nullptr, nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    const DWORD elapsed = GetTickCount() - start;
    char json[256];
    std::snprintf(json, sizeof json, "{\"d3dcompile_hr\":\"0x%08lx\",\"bytes\":%zu,\"ms\":%lu}", static_cast<unsigned long>(hr), code ? code->GetBufferSize() : 0, elapsed);
    if (code) code->Release();
    if (errors) errors->Release();
    std::snprintf(out, capacity, "%s", json);
    return SUCCEEDED(hr) ? 1 : 0;
}
