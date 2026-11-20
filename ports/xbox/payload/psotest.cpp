#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

std::string Hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "\"0x%08x\"", v);
    return b;
}

std::string Utf8(const std::wstring& w) {
    char t[512]{};
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, t, sizeof t - 1, nullptr, nullptr);
    return t;
}

std::string ReadBlob(const std::wstring& path) {
    std::string code;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) code.append(buf, n);
        std::fclose(f);
    }
    return code;
}

bool EndsWith(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

HRESULT GraphicsPso(ID3D12Device* device, ID3D12RootSignature* root, const std::wstring& base, const std::string& code, const std::string& gfx) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    for (auto& rt : pso.BlendState.RenderTarget) rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleDesc.Count = 1;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::string> names;
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    bool pixel = false, gs = false;
    size_t at = 0;
    while (at < gfx.size()) {
        size_t end = gfx.find('\n', at);
        if (end == std::string::npos) end = gfx.size();
        const std::string line = gfx.substr(at, end - at);
        at = end + 1;
        char word[64]{};
        int a = 0, b = 0;
        if (std::sscanf(line.c_str(), "kind %63s", word) == 1) pixel = std::strcmp(word, "pixel") == 0;
        else if (std::sscanf(line.c_str(), "gs %d", &a) == 1) gs = a != 0;
        else if (std::sscanf(line.c_str(), "dsv %d", &a) == 1) {
            pso.DSVFormat = static_cast<DXGI_FORMAT>(a);
            if (a != 0) {
                pso.DepthStencilState.DepthEnable = TRUE;
                pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            }
        } else if (line.rfind("rt ", 0) == 0) {
            int consumed = 0;
            const char* p = line.c_str() + 3;
            if (std::sscanf(p, "%d%n", &a, &consumed) == 1) {
                pso.NumRenderTargets = static_cast<UINT>(a);
                p += consumed;
                for (int i = 0; i < a && i < 8 && std::sscanf(p, "%d%n", &b, &consumed) == 1; ++i, p += consumed) pso.RTVFormats[i] = static_cast<DXGI_FORMAT>(b);
            }
        } else if (std::sscanf(line.c_str(), "in %63s %d %d", word, &a, &b) == 3) {
            names.push_back(word);
            layout.push_back({nullptr, static_cast<UINT>(a), static_cast<DXGI_FORMAT>(b), 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        }
    }
    for (size_t i = 0; i < layout.size(); ++i) layout[i].SemanticName = names[i].c_str();
    std::string vs, geometry;
    if (pixel) {
        vs = ReadBlob(base + L".vs.dxbc");
        if (vs.empty()) return E_FAIL;
        if (gs) geometry = ReadBlob(base + L".gs.dxbc");
        pso.VS = {vs.data(), vs.size()};
        if (!geometry.empty()) pso.GS = {geometry.data(), geometry.size()};
        pso.PS = {code.data(), code.size()};
    } else {
        pso.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
        pso.VS = {code.data(), code.size()};
    }
    ComPtr<ID3D12PipelineState> state;
    return device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&state));
}

}

extern "C" __declspec(dllexport) int __cdecl PayloadMain(const wchar_t* folder) {
    std::string j = "{";
    ComPtr<ID3D12Device> device;
    HRESULT created = E_FAIL;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    std::string tried;
    for (const auto level : levels) {
        created = D3D12CreateDevice(nullptr, level, IID_PPV_ARGS(&device));
        tried += (tried.empty() ? "" : ",") + Hex(static_cast<unsigned>(created));
        if (SUCCEEDED(created)) {
            j += "\"feature_level\":" + Hex(level) + ",";
            break;
        }
    }
    j += "\"tried\":[" + tried + "],";
    int result = 0;
    if (!device) {
        j += "\"device\":false}";
        result = 1;
    } else {
        int ok[2] = {0, 0}, total[2] = {0, 0};
        std::string fails;
        const std::wstring dir = std::wstring(folder) + L"\\dxbc";
        WIN32_FIND_DATAW find{};
        HANDLE h = FindFirstFileExW((dir + L"\\*.dxbc").c_str(), FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
        while (h != INVALID_HANDLE_VALUE) {
            const std::wstring name = find.cFileName;
            if (!EndsWith(name, L".gs.dxbc") && !EndsWith(name, L".vs.dxbc")) {
                const std::wstring base = dir + L"\\" + name.substr(0, name.size() - 5);
                const std::string code = ReadBlob(dir + L"\\" + name);
                const std::string gfx = ReadBlob(base + L".gfx");
                const int kind = gfx.empty() ? 0 : 1;
                ++total[kind];
                ComPtr<ID3D12RootSignature> root;
                HRESULT hr = device->CreateRootSignature(0, code.data(), code.size(), IID_PPV_ARGS(&root));
                const char* stage = "root";
                if (SUCCEEDED(hr) && kind == 0) {
                    stage = "cs";
                    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
                    pso.pRootSignature = root.Get();
                    pso.CS = {code.data(), code.size()};
                    ComPtr<ID3D12PipelineState> state;
                    hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&state));
                } else if (SUCCEEDED(hr)) {
                    stage = "gfx";
                    hr = GraphicsPso(device.Get(), root.Get(), base, code, gfx);
                }
                if (SUCCEEDED(hr)) {
                    ++ok[kind];
                } else if (fails.size() < 4000) {
                    fails += std::string(fails.empty() ? "" : ",") + "[\"" + Utf8(name) + "\",\"" + stage + "\"," + Hex(static_cast<unsigned>(hr)) + "]";
                }
            }
            if (!FindNextFileW(h, &find)) break;
        }
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        j += "\"ok\":" + std::to_string(ok[0]) + ",\"total\":" + std::to_string(total[0]) + ",\"graphics_ok\":" + std::to_string(ok[1]) + ",\"graphics_total\":" + std::to_string(total[1]) + ",\"fail\":[" + fails + "]}";
    }
    const std::wstring path = std::wstring(folder) + L"\\payload.json";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    return result;
}
