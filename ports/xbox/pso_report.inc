static std::string ReadBlob(const std::wstring& path) {
    std::string code;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) code.append(buf, n);
        std::fclose(f);
    }
    return code;
}

static bool EndsWith(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

static HRESULT GraphicsPso(ID3D12Device* device, ID3D12RootSignature* root, const std::wstring& base, const std::string& code, const std::string& gfx) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    for (auto& rt : pso.BlendState.RenderTarget) rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleDesc.Count = 1;
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
    com_ptr<ID3D12PipelineState> state;
    return device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(state.put()));
}

void WritePsoReport() {
    const std::wstring dir0 = std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path()) + L"\\dxbc";
    std::string j = "{";
    com_ptr<ID3D12Device> device = SharedDevice();
    if (!device) {
        j += "\"device\":false,\"hr\":" + Hex(static_cast<std::uint32_t>(g_deviceHr)) + "}";
    } else {
        int ok[2] = {0, 0}, total[2] = {0, 0};
        std::string fails, seen;
        const std::wstring local = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\dxbc";
        for (const std::wstring& dir : {dir0, local}) {
            WIN32_FIND_DATAW find{};
            HANDLE h = FindFirstFileExW((dir + L"\\*.dxbc").c_str(), FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
            while (h != INVALID_HANDLE_VALUE) {
                const std::wstring name = find.cFileName;
                {
                    char text[160]{};
                    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, text, sizeof text - 1, nullptr, nullptr);
                    seen += std::string(text) + "\n";
                }
                if (!EndsWith(name, L".gs.dxbc") && !EndsWith(name, L".vs.dxbc")) {
                    const std::wstring base = dir + L"\\" + name.substr(0, name.size() - 5);
                    const std::string code = ReadBlob(dir + L"\\" + name);
                    const std::string gfx = ReadBlob(base + L".gfx");
                    const int kind = gfx.empty() ? 0 : 1;
                    ++total[kind];
                    com_ptr<ID3D12RootSignature> root;
                    HRESULT hr = device->CreateRootSignature(0, code.data(), code.size(), IID_PPV_ARGS(root.put()));
                    if (SUCCEEDED(hr) && kind == 0) {
                        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
                        pso.pRootSignature = root.get();
                        pso.CS = {code.data(), code.size()};
                        com_ptr<ID3D12PipelineState> state;
                        hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(state.put()));
                    } else if (SUCCEEDED(hr)) {
                        hr = GraphicsPso(device.get(), root.get(), base, code, gfx);
                    }
                    if (SUCCEEDED(hr)) {
                        ++ok[kind];
                    } else if (fails.size() < 4000) {
                        char text[160]{};
                        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, text, sizeof text - 1, nullptr, nullptr);
                        fails += std::string(fails.empty() ? "" : ",") + "[\"" + text + "\"," + Hex(static_cast<std::uint32_t>(hr)) + "]";
                    }
                }
                if (!FindNextFileW(h, &find)) break;
            }
            if (h != INVALID_HANDLE_VALUE) FindClose(h);
        }
        const std::wstring names = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\pso-names.txt";
        if (FILE* f = _wfopen(names.c_str(), L"wb")) {
            std::fwrite(seen.data(), 1, seen.size(), f);
            std::fclose(f);
        }
        j +="\"ok\":" + std::to_string(ok[0]) + ",\"total\":" + std::to_string(total[0]) + ",\"graphics_ok\":" + std::to_string(ok[1]) + ",\"graphics_total\":" + std::to_string(total[1]) + ",\"fail\":[" + fails + "]}";
    }
    const std::wstring path = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\pso.json";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
}
