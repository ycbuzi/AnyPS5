#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

static std::vector<char> ReadAll(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::uint32_t ProgramType(const std::vector<char>& code) {
    for (size_t at = 0; at + 12 <= code.size(); ++at) {
        if (std::memcmp(code.data() + at, "SHEX", 4) == 0 || std::memcmp(code.data() + at, "SHDR", 4) == 0) {
            std::uint32_t version;
            std::memcpy(&version, code.data() + at + 8, 4);
            return version >> 16;
        }
    }
    return 0xFFFFu;
}

static int Width(BYTE mask) {
    int top = 0;
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i)) top = i + 1;
    return top;
}

static const char* TypeName(D3D_REGISTER_COMPONENT_TYPE type) {
    return type == D3D_REGISTER_COMPONENT_UINT32 ? "uint" : type == D3D_REGISTER_COMPONENT_SINT32 ? "int" : "float";
}

static DXGI_FORMAT Format(D3D_REGISTER_COMPONENT_TYPE type, int n) {
    static const DXGI_FORMAT f[3][4] = {
        {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT},
        {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32A32_UINT},
        {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT}};
    int t = type == D3D_REGISTER_COMPONENT_UINT32 ? 1 : type == D3D_REGISTER_COMPONENT_SINT32 ? 2 : 0;
    return f[t][(n < 1 ? 1 : n > 4 ? 4 : n) - 1];
}

static bool Rasterized(D3D_NAME name) {
    return name == D3D_NAME_IS_FRONT_FACE || name == D3D_NAME_SAMPLE_INDEX || name == D3D_NAME_PRIMITIVE_ID || name == D3D_NAME_COVERAGE;
}

static ID3DBlob* StubVertexShader(ID3D12ShaderReflection* consumer, std::string& error) {
    D3D12_SHADER_DESC desc{};
    consumer->GetDesc(&desc);
    std::string fields;
    bool position = false;
    for (UINT i = 0; i < desc.InputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC p{};
        consumer->GetInputParameterDesc(i, &p);
        if (Rasterized(p.SystemValueType)) continue;
        int n = Width(p.Mask);
        if (p.SystemValueType == D3D_NAME_POSITION) {
            position = true;
            fields += "float4 f" + std::to_string(i) + " : SV_Position; ";
            continue;
        }
        if (p.SystemValueType == D3D_NAME_CLIP_DISTANCE || p.SystemValueType == D3D_NAME_CULL_DISTANCE) {
            fields += std::string("float") + (n > 1 ? std::to_string(n) : "") + " f" + std::to_string(i) + " : " + (p.SystemValueType == D3D_NAME_CLIP_DISTANCE ? "SV_ClipDistance" : "SV_CullDistance") + std::to_string(p.SemanticIndex) + "; ";
            continue;
        }
        fields += std::string(TypeName(p.ComponentType)) + (n > 1 ? std::to_string(n) : "") + " f" + std::to_string(i) + " : " + p.SemanticName + std::to_string(p.SemanticIndex) + "; ";
    }
    if (!position) fields += "float4 pos : SV_Position; ";
    std::string source = "struct O { " + fields + "}; O main() { O o = (O)0; return o; }";
    ID3DBlob* code = nullptr;
    ID3DBlob* messages = nullptr;
    HRESULT hr = D3DCompile(source.data(), source.size(), "stub", nullptr, nullptr, "main", "vs_5_1", 0, 0, &code, &messages);
    if (FAILED(hr)) {
        error = messages ? std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize()) : "compile failed";
        error += " | " + source;
    }
    if (messages) messages->Release();
    return code;
}

static D3D12_GRAPHICS_PIPELINE_STATE_DESC BaseGraphics(ID3D12RootSignature* root) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    for (auto& rt : pso.BlendState.RenderTarget) rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleDesc.Count = 1;
    return pso;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"usage: psocheck <folder> [adapter]\n");
        return 2;
    }
    IDXGIFactory4* factory = nullptr;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    IDXGIAdapter1* adapter = nullptr;
    factory->EnumAdapters1(argc > 2 ? _wtoi(argv[2]) : 0, &adapter);
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    ID3D12Device* device = nullptr;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::fwprintf(stderr, L"no device\n");
        return 1;
    }
    std::wprintf(L"adapter %s\n", adapterDesc.Description);
    std::wstring folder = argv[1];
    WIN32_FIND_DATAW find{};
    HANDLE h = FindFirstFileW((folder + L"\\*.dxbc").c_str(), &find);
    int ok[3] = {0, 0, 0}, total[3] = {0, 0, 0};
    while (h != INVALID_HANDLE_VALUE) {
        std::wstring name = find.cFileName;
        bool isGs = name.size() > 8 && (name.compare(name.size() - 8, 8, L".gs.dxbc") == 0 || name.compare(name.size() - 8, 8, L".vs.dxbc") == 0);
        if (!isGs) {
            std::vector<char> code = ReadAll(folder + L"\\" + name);
            std::uint32_t type = ProgramType(code);
            int kind = type == 5u ? 0 : type == 1u ? 1 : type == 0u ? 2 : -1;
            std::string stage = "rootsig", detail;
            ID3D12RootSignature* root = nullptr;
            ID3D12PipelineState* state = nullptr;
            ID3D12ShaderReflection* reflect = nullptr;
            ID3DBlob* stub = nullptr;
            std::vector<char> gs;
            std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
            std::vector<std::string> names;
            if (kind >= 0) ++total[kind];
            HRESULT hr = kind < 0 ? E_NOTIMPL : device->CreateRootSignature(0, code.data(), code.size(), IID_PPV_ARGS(&root));
            if (kind < 0) stage = "unsupported stage " + std::to_string(type);
            if (SUCCEEDED(hr) && kind == 0) {
                stage = "compute";
                D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
                pso.pRootSignature = root;
                pso.CS = {code.data(), code.size()};
                hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&state));
            } else if (SUCCEEDED(hr)) {
                hr = D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflect));
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = BaseGraphics(root);
                if (SUCCEEDED(hr) && kind == 1) {
                    stage = "vertex";
                    D3D12_SHADER_DESC desc{};
                    reflect->GetDesc(&desc);
                    names.reserve(desc.InputParameters);
                    for (UINT i = 0; i < desc.InputParameters; ++i) {
                        D3D12_SIGNATURE_PARAMETER_DESC p{};
                        reflect->GetInputParameterDesc(i, &p);
                        if (p.SystemValueType != D3D_NAME_UNDEFINED) continue;
                        names.push_back(p.SemanticName);
                        layout.push_back({nullptr, p.SemanticIndex, Format(p.ComponentType, Width(p.Mask)), 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
                    }
                    for (size_t i = 0; i < layout.size(); ++i) layout[i].SemanticName = names[i].c_str();
                    pso.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
                    pso.VS = {code.data(), code.size()};
                } else if (SUCCEEDED(hr)) {
                    stage = "pixel";
                    std::wstring gsName = name.substr(0, name.size() - 5) + L".gs.dxbc";
                    if (GetFileAttributesW((folder + L"\\" + gsName).c_str()) != INVALID_FILE_ATTRIBUTES) gs = ReadAll(folder + L"\\" + gsName);
                    ID3D12ShaderReflection* front = reflect;
                    if (!gs.empty()) {
                        stage = "pixel+gs";
                        hr = D3DReflect(gs.data(), gs.size(), IID_PPV_ARGS(&front));
                    }
                    if (SUCCEEDED(hr)) stub = StubVertexShader(front, detail);
                    if (front != reflect && front) front->Release();
                    if (!stub) {
                        hr = E_FAIL;
                        stage += " stub";
                    } else {
                        pso.VS = {stub->GetBufferPointer(), stub->GetBufferSize()};
                        if (!gs.empty()) pso.GS = {gs.data(), gs.size()};
                        pso.PS = {code.data(), code.size()};
                        D3D12_SHADER_DESC desc{};
                        reflect->GetDesc(&desc);
                        for (UINT i = 0; i < desc.OutputParameters; ++i) {
                            D3D12_SIGNATURE_PARAMETER_DESC p{};
                            reflect->GetOutputParameterDesc(i, &p);
                            if (p.SystemValueType == D3D_NAME_TARGET && p.SemanticIndex < 8) {
                                pso.RTVFormats[p.SemanticIndex] = Format(p.ComponentType, 4);
                                if (p.SemanticIndex + 1 > pso.NumRenderTargets) pso.NumRenderTargets = p.SemanticIndex + 1;
                            } else if (p.SystemValueType == D3D_NAME_DEPTH || p.SystemValueType == D3D_NAME_DEPTH_GREATER_EQUAL || p.SystemValueType == D3D_NAME_DEPTH_LESS_EQUAL) {
                                pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
                                pso.DepthStencilState.DepthEnable = TRUE;
                                pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                                pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                            }
                        }
                        for (UINT i = 0; i < pso.NumRenderTargets; ++i)
                            if (pso.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) pso.RTVFormats[i] = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    }
                }
                if (SUCCEEDED(hr)) {
                    std::wstring base = folder + L"\\" + name.substr(0, name.size() - 5);
                    std::ofstream gfx(base + L".gfx");
                    gfx << "kind " << (kind == 1 ? "vertex" : "pixel") << "\n";
                    gfx << "gs " << (gs.empty() ? 0 : 1) << "\n";
                    gfx << "rt " << pso.NumRenderTargets;
                    for (UINT i = 0; i < pso.NumRenderTargets; ++i) gfx << " " << static_cast<int>(pso.RTVFormats[i]);
                    gfx << "\ndsv " << static_cast<int>(pso.DSVFormat) << "\n";
                    for (const auto& e : layout) gfx << "in " << e.SemanticName << " " << e.SemanticIndex << " " << static_cast<int>(e.Format) << "\n";
                    if (stub) {
                        std::ofstream vs(base + L".vs.dxbc", std::ios::binary);
                        vs.write(static_cast<const char*>(stub->GetBufferPointer()), stub->GetBufferSize());
                    }
                    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&state));
                }
            }
            if (SUCCEEDED(hr)) {
                ++ok[kind];
            } else {
                std::wprintf(L"fail %S 0x%08lx %s %S\n", stage.c_str(), static_cast<unsigned long>(hr), name.c_str(), detail.substr(0, 300).c_str());
            }
            if (stub) stub->Release();
            if (reflect) reflect->Release();
            if (state) state->Release();
            if (root) root->Release();
        }
        if (!FindNextFileW(h, &find)) break;
    }
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    std::wprintf(L"pso ok %d / %d compute, %d / %d vertex, %d / %d pixel\n", ok[0], total[0], ok[1], total[1], ok[2], total[2]);
    return ok[0] == total[0] && ok[1] == total[1] && ok[2] == total[2] ? 0 : 1;
}
