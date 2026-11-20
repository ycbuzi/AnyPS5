namespace {

struct XSampler { D3D12_SAMPLER_DESC d{}; D3D12_SAMPLER_DESC plain{}; };
struct XSetLayoutBinding { uint32_t binding = 0; VkDescriptorType type{}; uint32_t count = 0; };
struct XSetLayout { std::vector<XSetLayoutBinding> bindings; };
struct XDescriptor { VkDescriptorType type{}; XBuffer* buffer = nullptr; uint64_t offset = 0, range = 0; XImageView* view = nullptr; XSampler* sampler = nullptr; };
struct XSet { XSetLayout* layout = nullptr; std::map<uint32_t, std::vector<XDescriptor>> slots; };
struct XPool { std::vector<XSet*> sets; };
struct XPipelineLayout { std::vector<XSetLayout*> sets; uint32_t push = 0; };
struct XShaderModule { std::vector<uint32_t> words; };
struct XRenderPass { std::vector<VkAttachmentDescription> att; std::vector<uint32_t> color, resolve; uint32_t depth = VK_ATTACHMENT_UNUSED; };
struct XFramebuffer { std::vector<XImageView*> views; uint32_t w = 0, h = 0, layers = 1; };
struct XEntry { std::string name, type; char kind = 't'; uint32_t space = 0, binding = 0, count = 1, reg = 0, offset = 0; bool unbounded = false; };
struct XStageTable { D3D12_SHADER_VISIBILITY vis = D3D12_SHADER_VISIBILITY_ALL; std::vector<XEntry> views, samplers, mem; UINT viewCount = 0, samplerCount = 0; int viewParam = -1, samplerParam = -1, memParam = -1; bool memSrv = false; };
struct XPipeline {
    ID3D12PipelineState* pso = nullptr;
    ID3D12RootSignature* root = nullptr;
    bool compute = false;
    std::vector<XStageTable> stages;
    XPipelineLayout* layout = nullptr;
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32_t strides[16]{};
    float blend[4]{};
    bool dynamicBlend = false;
    XPipeline* mesh = nullptr;
    int meshParam = -1;
    uint32_t meshPrims = 0, meshBlock = 0;
    std::atomic<bool> ready{true};
    uint64_t hash = 0;
    std::atomic<uint32_t> deferred{0};
};
struct XQueryPool { ID3D12QueryHeap* heap = nullptr; D3D12_QUERY_HEAP_TYPE type{}; VkQueryType vk{}; uint32_t count = 0; ID3D12Resource* readback = nullptr; uint64_t* mapped = nullptr; uint64_t freq = 0; };
struct XBound { XSet* set = nullptr; std::vector<uint32_t> dynamic; };

struct XState {
    XPipeline* graphics = nullptr;
    XPipeline* compute = nullptr;
    XBound sets[2][8];
    uint8_t push[256]{};
    D3D12_VERTEX_BUFFER_VIEW vb[16]{};
    XMemory* vbMem[16]{};
    uint32_t vbCount = 0;
    D3D12_INDEX_BUFFER_VIEW ib{};
    XMemory* ibMem = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    bool hasDsv = false;
    XRenderPass* pass = nullptr;
    XFramebuffer* fb = nullptr;
    ID3D12Resource* upload = nullptr;
    uint8_t* uploadCpu = nullptr;
    uint64_t uploadUsed = 0, uploadSize = 0;
};

std::mutex g_convertLock;
std::map<std::vector<XSampler*>, UINT> g_samplerTables;
std::map<uint64_t, ID3D12CommandSignature*> g_signatures;
std::map<std::string, int> g_warned;

void Warn(const std::string& what) {
    if (g_warned[what]++ < 3) Log("%s", what.c_str());
}

bool Deferred(XPipeline* p, const char* what) {
    if (!p || p->ready.load(std::memory_order_acquire)) return false;
    if (p->deferred++ == 0) Log("%s deferred: pipeline %016llx still converting", what, (unsigned long long)p->hash);
    return true;
}

void MarkReady(XPipeline* p) {
    p->ready.store(true, std::memory_order_release);
    Log("pipeline %016llx ready after %u deferred commands", (unsigned long long)p->hash, p->deferred.load());
}

XState& St(XCommandBuffer* cb) {
    if (!cb->state) cb->state = new XState;
    if (cb->stateEpoch != cb->epoch) {
        *cb->state = XState{};
        cb->stateEpoch = cb->epoch;
    }
    return *cb->state;
}

uint64_t Fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

std::vector<uint32_t> Specialize(std::vector<uint32_t> w, const VkSpecializationInfo* spec) {
    std::map<uint32_t, uint32_t> specId;
    for (size_t i = 5; i < w.size();) {
        const uint32_t op = w[i] & 0xFFFF, n = w[i] >> 16;
        if (n == 0) break;
        if (op == 71 && n >= 4 && w[i + 2] == 1) specId[w[i + 1]] = w[i + 3];
        i += n;
    }
    for (size_t i = 5; i < w.size();) {
        const uint32_t op = w[i] & 0xFFFF, n = w[i] >> 16;
        if (n == 0) break;
        if (op == 32 && n >= 4 && w[i + 2] == 9) w[i + 2] = 12;
        if (op == 59 && n >= 4 && w[i + 3] == 9) w[i + 3] = 12;
        if (spec && (op == 48 || op == 49 || op == 50) && n >= 3) {
            auto it = specId.find(w[i + 2]);
            if (it != specId.end()) {
                for (uint32_t e = 0; e < spec->mapEntryCount; ++e) {
                    const auto& m = spec->pMapEntries[e];
                    if (m.constantID != it->second) continue;
                    uint32_t v = 0;
                    std::memcpy(&v, static_cast<const uint8_t*>(spec->pData) + m.offset, std::min<size_t>(m.size, 4));
                    if (op == 50 && n >= 4) w[i + 3] = v;
                    else w[i] = (n << 16) | (v ? 48u : 49u);
                }
            }
        }
        i += n;
    }
    std::vector<uint32_t> pushVars;
    size_t firstDecorate = 0;
    for (size_t i = 5; i < w.size();) {
        const uint32_t op = w[i] & 0xFFFF, n = w[i] >> 16;
        if (n == 0) break;
        if (op == 59 && n >= 4 && w[i + 3] == 12) {
            bool hasSet = false;
            for (size_t j = 5; j < w.size();) {
                const uint32_t op2 = w[j] & 0xFFFF, n2 = w[j] >> 16;
                if (n2 == 0) break;
                if (op2 == 71 && n2 >= 3 && w[j + 1] == w[i + 2] && w[j + 2] == 34) hasSet = true;
                j += n2;
            }
            if (!hasSet) pushVars.push_back(w[i + 2]);
        }
        if (op == 71 && !firstDecorate) firstDecorate = i;
        i += n;
    }
    std::vector<uint32_t> extra;
    for (uint32_t v : pushVars) extra.insert(extra.end(), {(4u << 16) | 71u, v, 34u, 7u, (4u << 16) | 71u, v, 33u, 15u, (3u << 16) | 71u, v, 24u});
    if (firstDecorate && !extra.empty()) w.insert(w.begin() + firstDecorate, extra.begin(), extra.end());
    return w;
}

std::string ExeDir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s = p;
    return s.substr(0, s.find_last_of("\\/"));
}

std::string EnvOr(const char* name, const std::string& fallback) {
    char v[MAX_PATH];
    return GetEnvironmentVariableA(name, v, MAX_PATH) ? std::string(v) : fallback;
}

bool ReadFile(const std::string& path, std::vector<char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    out.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

std::string JsonString(const std::string& obj, const char* key) {
    auto k = obj.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return {};
    auto a = obj.find('"', obj.find(':', k) + 1);
    auto b = obj.find('"', a + 1);
    return obj.substr(a + 1, b - a - 1);
}

uint32_t JsonNumber(const std::string& obj, const char* key) {
    auto k = obj.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return 0;
    return uint32_t(std::strtoul(obj.c_str() + obj.find(':', k) + 1, nullptr, 10));
}

struct Converted { bool ok = false; std::vector<char> dxbc, gs, meshVs; std::vector<XEntry> entries; std::string hlsl, gsHlsl; uint32_t meshPrims = 0, meshBlock = 0; };

std::string RectListGsSource(const std::string& vs) {
    const size_t a = vs.find("struct SPIRV_Cross_Output");
    if (a == std::string::npos) return {};
    const size_t open = vs.find('{', a), close = vs.find("};", a);
    if (open == std::string::npos || close == std::string::npos) return {};
    const std::string body = vs.substr(open + 1, close - open - 1);
    std::string blend, copy;
    bool position = false;
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find(';', start);
        if (end == std::string::npos) break;
        std::string line = body.substr(start, end - start);
        start = end + 1;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string decl = line.substr(0, colon);
        while (!decl.empty() && (decl.back() == ' ' || decl.back() == '\t')) decl.pop_back();
        const bool flat = decl.find("nointerpolation") != std::string::npos;
        size_t nameEnd = decl.size(), count = 0;
        const size_t bracket = decl.find('[');
        if (bracket != std::string::npos) { count = std::strtoul(decl.c_str() + bracket + 1, nullptr, 10); nameEnd = bracket; }
        const size_t nameStart = decl.find_last_of(" \t", nameEnd - 1) + 1;
        const std::string name = decl.substr(nameStart, nameEnd - nameStart);
        const size_t typeStart = decl.find_last_of(" \t", nameStart - 2) == std::string::npos ? 0 : decl.find_last_of(" \t", nameStart - 2) + 1;
        const std::string type = decl.substr(typeStart, nameStart - 1 - typeStart);
        const bool real = type.rfind("float", 0) == 0 || type.rfind("half", 0) == 0 || type.rfind("min16float", 0) == 0;
        if (line.find("SV_Position") != std::string::npos) position = true;
        std::string& target = (real && !flat) ? blend : copy;
        if (count) {
            for (size_t i = 0; i < count; ++i) {
                const std::string m = name + "[" + std::to_string(i) + "]";
                target += (real && !flat) ? "    d." + m + " = v[i1]." + m + " + v[i2]." + m + " - v[c]." + m + ";\n" : "    d." + m + " = v[c]." + m + ";\n";
            }
        } else {
            target += (real && !flat) ? "    d." + name + " = v[i1]." + name + " + v[i2]." + name + " - v[c]." + name + ";\n" : "    d." + name + " = v[c]." + name + ";\n";
        }
    }
    if (!position) return {};
    std::string s = "struct V" + vs.substr(a + std::strlen("struct SPIRV_Cross_Output"), close - a - std::strlen("struct SPIRV_Cross_Output") + 2) + "\n";
    s += "bool Corner(float4 p[3], int i) { int n = (i + 1) % 3, q = (i + 2) % 3; bool2 en = p[i].xy == p[n].xy; bool2 ep = p[q].xy == p[i].xy; return (en.x && ep.y) || (en.y && ep.x); }\n";
    s += "bool Finite(float4 x) { return all(!isinf(x) && !isnan(x)); }\n";
    s += "[maxvertexcount(4)]\nvoid main(triangle V v[3], inout TriangleStream<V> s) {\n";
    s += "    float4 p[3] = { v[0].gl_Position, v[1].gl_Position, v[2].gl_Position };\n";
    s += "    int corners = 0, c = 0;\n    [unroll] for (int i = 0; i < 3; ++i) if (Corner(p, i)) { corners++; c = i; }\n";
    s += "    if (corners != 1 || !(p[0].w > 0.0) || p[1].w != p[0].w || p[2].w != p[0].w || !Finite(p[0]) || !Finite(p[1]) || !Finite(p[2])) return;\n";
    s += "    int i1 = (c + 1) % 3, i2 = (c + 2) % 3;\n    V d;\n" + blend + copy;
    s += "    if (!Finite(d.gl_Position)) return;\n";
    s += "    s.Append(v[c]); s.Append(v[i1]); s.Append(v[i2]); s.Append(d);\n}\n";
    return s;
}

std::map<std::string, std::string> StructFields(const std::string& body, bool bySemantic) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find(';', start);
        if (end == std::string::npos) break;
        const std::string line = body.substr(start, end - start);
        start = end + 1;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string decl = line.substr(0, colon), semantic = line.substr(colon + 1);
        while (!decl.empty() && (decl.back() == ' ' || decl.back() == '\t')) decl.pop_back();
        semantic.erase(0, semantic.find_first_not_of(" \t"));
        while (!semantic.empty() && (semantic.back() == ' ' || semantic.back() == '\t')) semantic.pop_back();
        size_t nameEnd = decl.size();
        const size_t bracket = decl.find('[');
        if (bracket != std::string::npos) nameEnd = bracket;
        const size_t nameStart = decl.find_last_of(" \t", nameEnd - 1) + 1;
        const std::string name = decl.substr(nameStart, nameEnd - nameStart);
        std::string key = semantic;
        for (auto& ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (bySemantic) out[key] = name;
        else out[name] = key;
    }
    return out;
}

std::string BaryGsForVs(const std::string& gs, const std::string& vs) {
    const size_t a = vs.find("struct SPIRV_Cross_Output");
    if (a == std::string::npos) return {};
    const size_t open = vs.find('{', a), close = vs.find("};", a);
    const size_t g = gs.find("struct GsIn");
    if (open == std::string::npos || close == std::string::npos || g == std::string::npos) return {};
    const size_t gopen = gs.find('{', g), gclose = gs.find("};", g);
    if (gopen == std::string::npos || gclose == std::string::npos) return {};
    const std::string vsBody = vs.substr(open + 1, close - open - 1);
    const auto vsBySemantic = StructFields(vsBody, true);
    const auto gsSemantic = StructFields(gs.substr(gopen + 1, gclose - gopen - 1), false);
    if (!vsBySemantic.count("SV_POSITION")) return {};
    std::string rest = gs.substr(gclose + 2), out;
    size_t pos = 0;
    while (true) {
        const size_t v = rest.find("v[", pos);
        if (v == std::string::npos) break;
        const size_t rb = rest.find("].", v);
        if (rb == std::string::npos) break;
        size_t ne = rb + 2;
        while (ne < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[ne])) || rest[ne] == '_')) ++ne;
        const bool word = v == 0 || !(std::isalnum(static_cast<unsigned char>(rest[v - 1])) || rest[v - 1] == '_' || rest[v - 1] == '.');
        out += rest.substr(pos, v - pos);
        const std::string field = rest.substr(rb + 2, ne - rb - 2);
        auto sem = gsSemantic.find(field);
        if (!word || sem == gsSemantic.end()) { out += rest.substr(v, ne - v); pos = ne; continue; }
        auto vsField = vsBySemantic.find(sem->second);
        out += vsField == vsBySemantic.end() ? std::string("0") : rest.substr(v, rb + 2 - v) + vsField->second;
        pos = ne;
    }
    out += rest.substr(pos);
    return gs.substr(0, g) + "struct GsIn\n{" + vsBody + "};" + out;
}

std::vector<char> CompileGs(const std::string& src, const char* what) {
    static std::mutex lock;
    static std::map<uint64_t, std::vector<char>> cache;
    const uint64_t h = Fnv(src.data(), src.size());
    std::lock_guard<std::mutex> g(lock);
    auto it = cache.find(h);
    if (it != cache.end()) return it->second;
    std::vector<char> out;
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    if (SUCCEEDED(D3DCompile(src.data(), src.size(), what, nullptr, nullptr, "main", "gs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        out.assign(static_cast<char*>(code->GetBufferPointer()), static_cast<char*>(code->GetBufferPointer()) + code->GetBufferSize());
        code->Release();
    } else {
        Log("%s gs: compile failed: %s", what, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
    }
    if (err) err->Release();
    cache[h] = out;
    return out;
}

std::vector<char> RectListGs(const std::string& vs) {
    static std::mutex lock;
    static std::map<uint64_t, std::vector<char>> cache;
    const uint64_t h = Fnv(vs.data(), vs.size());
    std::lock_guard<std::mutex> g(lock);
    auto it = cache.find(h);
    if (it != cache.end()) return it->second;
    std::vector<char> out;
    const std::string src = RectListGsSource(vs);
    if (src.empty()) { Log("rect-list gs: vertex shader output struct not found"); return out; }
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    if (SUCCEEDED(D3DCompile(src.data(), src.size(), "rectlist", nullptr, nullptr, "main", "gs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        out.assign(static_cast<char*>(code->GetBufferPointer()), static_cast<char*>(code->GetBufferPointer()) + code->GetBufferSize());
        code->Release();
    } else {
        Log("rect-list gs: compile failed: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
    }
    if (err) err->Release();
    cache[h] = out;
    return out;
}

bool LocalShaders() {
    const std::string tool = EnvOr("XVK_TOOLS", "C:\\AnyPS5-work\\xbox-probe\\hlsl51");
    return std::getenv("XVK_LOCAL_SHADERS") || GetFileAttributesA((tool + "\\dxbc.py").c_str()) == INVALID_FILE_ATTRIBUTES;
}

std::string ShaderBase(const std::vector<uint32_t>& words, bool fixupClip, std::string& name) {
    const uint64_t h = Fnv(words.data(), words.size() * 4) ^ (fixupClip ? 0x6e31315f636c6970ull : 0);
    char n[32];
    std::snprintf(n, sizeof n, "%016llx", (unsigned long long)h);
    name = n;
    return EnvOr("XVK_SHADERS", ExeDir() + "\\xvk-shaders") + "\\" + name + "\\" + name;
}

bool FileExists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::mutex g_failLock;
std::set<std::string> g_failed;
std::condition_variable g_flightCv;
std::set<std::string> g_inFlight;

bool ShaderReady(const std::vector<uint32_t>& words, bool fixupClip) {
    std::string name;
    const std::string base = ShaderBase(words, fixupClip, name);
    if (FileExists(base + ".dxbc")) return true;
    std::lock_guard<std::mutex> guard(g_failLock);
    return g_failed.count(base) != 0;
}

bool ConvertOnDevice(const std::vector<uint32_t>& words, const std::string& base, const char* name, bool fixupClip) {
    {
        std::lock_guard<std::mutex> guard(g_failLock);
        if (g_failed.count(base)) return false;
    }
    const xcross::Result r = xcross::Convert(words, true, fixupClip);
    auto put = [](const std::string& path, const std::string& data, bool crlf) {
        std::string s = data;
        if (crlf) xcross::ReplaceAll(s, "\n", "\r\n");
        FILE* o = std::fopen(path.c_str(), "wb");
        if (!o) return;
        std::fwrite(s.data(), 1, s.size(), o);
        std::fclose(o);
    };
    if (!r.hlsl.empty()) put(base + ".hlsl", r.hlsl, true);
    if (!r.gsHlsl.empty()) { put(base + ".gs.hlsl", r.gsHlsl, true); put(base + ".gs.dxbc", std::string(r.gsDxbc.begin(), r.gsDxbc.end()), false); }
    if (!r.ok) {
        Log("shader %s: on-device conversion failed: %s", name, r.error.c_str());
        std::lock_guard<std::mutex> guard(g_failLock);
        g_failed.insert(base);
        return false;
    }
    if (r.explicitLod) Log("shader %s: X3662 fallback, %d gradient sample(s) made explicit level 0", name, r.explicitLod);
    put(base + ".layout.json", r.layout, false);
    if (!r.vsHlsl.empty()) {
        put(base + ".vs.hlsl", r.vsHlsl, true);
        put(base + ".vs.dxbc", std::string(r.vsDxbc.begin(), r.vsDxbc.end()), false);
        put(base + ".mesh.json", r.meshJson, false);
    }
    put(base + ".dxbc.tmp", std::string(r.dxbc.begin(), r.dxbc.end()), false);
    MoveFileExA((base + ".dxbc.tmp").c_str(), (base + ".dxbc").c_str(), MOVEFILE_REPLACE_EXISTING);
    Log("shader %s: converted on device (spv %.0f ms, cross %.0f ms, passes %.0f ms, fxc %.0f ms)", name, r.msSpv, r.msCross, r.msPasses, r.msFxc);
    return true;
}

void PrepareShader(const std::vector<uint32_t>& words, bool fixupClip) {
    std::string name;
    const std::string base = ShaderBase(words, fixupClip, name);
    {
        std::unique_lock<std::mutex> lock(g_failLock);
        g_flightCv.wait(lock, [&] { return !g_inFlight.count(base); });
        if (g_failed.count(base) || FileExists(base + ".dxbc")) return;
        g_inFlight.insert(base);
    }
    const std::string root = EnvOr("XVK_SHADERS", ExeDir() + "\\xvk-shaders");
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\" + name).c_str(), nullptr);
    if (FILE* f = std::fopen((base + ".spv").c_str(), "wb")) {
        std::fwrite(words.data(), 4, words.size(), f);
        std::fclose(f);
    }
    ConvertOnDevice(words, base, name.c_str(), fixupClip);
    {
        std::lock_guard<std::mutex> guard(g_failLock);
        g_inFlight.erase(base);
    }
    g_flightCv.notify_all();
}

std::mutex g_jobLock;
std::condition_variable g_jobCv;
std::deque<std::function<void()>> g_jobs;
int g_workers = 0;

void Enqueue(std::function<void()> job) {
    std::lock_guard<std::mutex> guard(g_jobLock);
    g_jobs.push_back(std::move(job));
    if (g_workers < 3) {
        ++g_workers;
        std::thread([] {
            for (;;) {
                std::function<void()> f;
                {
                    std::unique_lock<std::mutex> lock(g_jobLock);
                    g_jobCv.wait(lock, [] { return !g_jobs.empty(); });
                    f = std::move(g_jobs.front());
                    g_jobs.pop_front();
                }
                f();
            }
        }).detach();
    }
    g_jobCv.notify_one();
}

Converted Convert(const std::vector<uint32_t>& words, bool fixupClip = false) {
    Converted c;
    const uint64_t h = Fnv(words.data(), words.size() * 4) ^ (fixupClip ? 0x6e31315f636c6970ull : 0);
    char name[32];
    std::snprintf(name, sizeof name, "%016llx", (unsigned long long)h);
    const std::string dir = EnvOr("XVK_SHADERS", ExeDir() + "\\xvk-shaders") + "\\" + name;
    const std::string base = dir + "\\" + name;
    std::lock_guard<std::mutex> guard(g_convertLock);
    if (!ReadFile(base + ".dxbc", c.dxbc)) {
        CreateDirectoryA(EnvOr("XVK_SHADERS", ExeDir() + "\\xvk-shaders").c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        FILE* f = std::fopen((base + ".spv").c_str(), "wb");
        if (!f) return c;
        std::fwrite(words.data(), 4, words.size(), f);
        std::fclose(f);
        const std::string tool = EnvOr("XVK_TOOLS", "C:\\AnyPS5-work\\xbox-probe\\hlsl51");
        const bool haveTool = GetFileAttributesA((tool + "\\dxbc.py").c_str()) != INVALID_FILE_ATTRIBUTES;
        bool done = false;
        if (std::getenv("XVK_LOCAL_SHADERS") || !haveTool) {
            if (!ConvertOnDevice(words, base, name, fixupClip)) return c;
            done = true;
        }
        if (!done) {
            std::string cmd = "python \"" + tool + "\\dxbc.py\" \"" + dir + "\" \"" + dir + "\"";
            SetEnvironmentVariableA("XVK_FLIP", "1");
            SetEnvironmentVariableA("XVK_FIXUP_CLIP", fixupClip ? "1" : nullptr);
            STARTUPINFOA si{sizeof si};
            PROCESS_INFORMATION pi{};
            if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { Log("shader %s: converter did not start", name); return c; }
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        if (!ReadFile(base + ".dxbc", c.dxbc)) { Log("shader %s: conversion failed", name); return c; }
    }
    ReadFile(base + ".gs.dxbc", c.gs);
    if (!c.gs.empty()) { std::vector<char> gh; ReadFile(base + ".gs.hlsl", gh); c.gsHlsl.assign(gh.begin(), gh.end()); }
    if (ReadFile(base + ".vs.dxbc", c.meshVs)) {
        std::vector<char> mj;
        ReadFile(base + ".mesh.json", mj);
        const std::string ms(mj.begin(), mj.end());
        c.meshPrims = JsonNumber(ms, "maxp");
        c.meshBlock = JsonNumber(ms, "block");
        if (!c.meshPrims || !c.meshBlock) { Log("shader %s: mesh layout missing", name); return c; }
    }
    std::vector<char> json, hlsl;
    ReadFile(base + ".layout.json", json);
    ReadFile(base + ".hlsl", hlsl);
    const std::string js(json.begin(), json.end()), hs(hlsl.begin(), hlsl.end());
    c.hlsl = hs;
    for (size_t a = js.find('{'); a != std::string::npos; a = js.find('{', a + 1)) {
        const std::string obj = js.substr(a, js.find('}', a) - a + 1);
        if (obj.find("\"sampler_info\"") != std::string::npos) { Warn(std::string("shader ") + name + ": integer texture sampling needs sampler info (not bound yet)"); continue; }
        XEntry e;
        e.name = JsonString(obj, "name");
        const std::string kind = JsonString(obj, "kind");
        e.kind = kind == "SRV" ? 't' : kind == "UAV" ? 'u' : kind == "CBV" ? 'b' : 's';
        e.space = JsonNumber(obj, "space");
        e.binding = JsonNumber(obj, "binding");
        const uint32_t count = JsonNumber(obj, "count");
        e.unbounded = count == 0;
        e.count = std::max(1u, count);
        e.reg = JsonNumber(obj, "register");
        const std::string tail = " " + e.name;
        for (size_t p = hs.find(tail); p != std::string::npos; p = hs.find(tail, p + 1)) {
            const char next = p + tail.size() < hs.size() ? hs[p + tail.size()] : 0;
            if (next != ' ' && next != '[') continue;
            size_t lineStart = hs.rfind('\n', p);
            lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            const size_t lineEnd = hs.find('\n', p);
            const std::string line = hs.substr(lineStart, lineEnd - lineStart);
            if (line.find(": register(") == std::string::npos) continue;
            std::string type = hs.substr(lineStart, p - lineStart);
            if (type.rfind("globallycoherent ", 0) == 0) type = type.substr(17);
            e.type = type;
            break;
        }
        c.entries.push_back(e);
    }
    c.ok = true;
    return c;
}

DXGI_FORMAT ViewFormat(VkFormat f) {
    switch (f) {
    case VK_FORMAT_D32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
    case VK_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
    case VK_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: { auto* fi = Format(f); return fi ? fi->dx : DXGI_FORMAT_R8G8B8A8_UNORM; }
    }
}

DXGI_FORMAT VertexFormat(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R32G32B32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VK_FORMAT_R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
    case VK_FORMAT_R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
    case VK_FORMAT_R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
    case VK_FORMAT_R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
    case VK_FORMAT_R32_SINT: return DXGI_FORMAT_R32_SINT;
    case VK_FORMAT_R8G8B8A8_USCALED: return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
    case VK_FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
    case VK_FORMAT_R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
    default: { auto* fi = Format(f); return fi ? fi->dx : DXGI_FORMAT_UNKNOWN; }
    }
}

void StateFor(XCommandBuffer* cb, Tracked& t, D3D12_RESOURCE_STATES want, std::vector<ID3D12Resource*>& uav) {
    if (std::find(uav.begin(), uav.end(), t.res) != uav.end()) return;
    if (want == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) uav.push_back(t.res);
    Use(cb->list, t, want);
}

const XDescriptor* Lookup(XState& st, int bp, uint32_t space, uint32_t binding, uint32_t index, uint64_t* dynamicOffset) {
    if (space >= 8 || !st.sets[bp][space].set) return nullptr;
    auto& bound = st.sets[bp][space];
    auto it = bound.set->slots.find(binding);
    if (it == bound.set->slots.end() || index >= it->second.size()) return nullptr;
    *dynamicOffset = 0;
    const auto& d = it->second[index];
    if (d.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || d.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
        uint32_t ordinal = 0;
        for (const auto& b : bound.set->layout->bindings) {
            if (b.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC && b.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) continue;
            if (b.binding == binding) { ordinal += index; break; }
            ordinal += b.count;
        }
        if (ordinal < bound.dynamic.size()) *dynamicOffset = bound.dynamic[ordinal];
    }
    return &d;
}

bool UploadPush(XCommandBuffer* cb, XState& st, uint32_t size, D3D12_GPU_VIRTUAL_ADDRESS* va, ID3D12Resource** res, uint64_t* offset) {
    size = uint32_t(AlignUp(std::max(size, 16u), 256));
    if (!st.upload || st.uploadUsed + size > st.uploadSize) {
        void* cpu = nullptr;
        st.upload = Staging(cb, 1 << 20, &cpu);
        if (!st.upload) return false;
        st.uploadCpu = static_cast<uint8_t*>(cpu);
        st.uploadUsed = 0;
        st.uploadSize = 1 << 20;
    }
    std::memcpy(st.uploadCpu + st.uploadUsed, st.push, std::min<size_t>(size, sizeof st.push));
    *res = st.upload;
    *offset = st.uploadUsed;
    *va = st.upload->GetGPUVirtualAddress() + st.uploadUsed;
    st.uploadUsed += size;
    return true;
}

UINT TakeRange(DescriptorRing& r, UINT n) {
    if (r.next + n > r.count) r.next = 0;
    const UINT i = r.next;
    r.next += n;
    return i;
}

void WriteView(XCommandBuffer* cb, XState& st, XPipeline* p, int bp, const XEntry& e, D3D12_CPU_DESCRIPTOR_HANDLE dst, uint32_t index, std::vector<ID3D12Resource*>& uav) {
    auto* dev = cb->device->device;
    uint64_t dyn = 0;
    const XDescriptor* d = Lookup(st, bp, e.space, e.binding, index, &dyn);
    const bool push = e.space == 7 && e.binding == 15 && (!d) && p->layout && p->layout->push;
    const std::string& t = e.type;
    const bool raw = t.find("ByteAddressBuffer") != std::string::npos;
    if (e.kind == 'b') {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cv{};
        if (d && d->buffer && d->buffer->memory && EnsureWhole(d->buffer->memory)) {
            const uint64_t range = d->range == VK_WHOLE_SIZE ? d->buffer->size - d->offset : d->range;
            cv.BufferLocation = d->buffer->memory->whole.res->GetGPUVirtualAddress() + d->buffer->offset + d->offset + dyn;
            cv.SizeInBytes = UINT(AlignUp(std::min<uint64_t>(range, 65536), 256));
            StateFor(cb, d->buffer->memory->whole, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, uav);
        }
        dev->CreateConstantBufferView(cv.BufferLocation ? &cv : nullptr, dst);
        return;
    }
    if (raw || push) {
        ID3D12Resource* res = nullptr;
        uint64_t first = 0, bytes = 0;
        if (push) {
            D3D12_GPU_VIRTUAL_ADDRESS va;
            if (!UploadPush(cb, st, p->layout->push, &va, &res, &first)) res = nullptr;
            bytes = AlignUp(std::max(p->layout->push, 16u), 16);
        } else if (d && d->buffer && d->buffer->memory && EnsureWhole(d->buffer->memory)) {
            res = d->buffer->memory->whole.res;
            first = d->buffer->offset + d->offset + dyn;
            bytes = d->range == VK_WHOLE_SIZE ? d->buffer->size - d->offset : d->range;
            StateFor(cb, d->buffer->memory->whole, e.kind == 'u' ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, uav);
        }
        if (first % 16) Warn("raw buffer view offset not 16-byte aligned");
        if (e.kind == 'u') {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
            uv.Format = DXGI_FORMAT_R32_TYPELESS;
            uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uv.Buffer.FirstElement = first / 4;
            uv.Buffer.NumElements = UINT(std::max<uint64_t>(bytes / 4, res ? 1 : 0));
            uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            dev->CreateUnorderedAccessView(res, nullptr, &uv, dst);
        } else {
            D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.Format = DXGI_FORMAT_R32_TYPELESS;
            sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.Buffer.FirstElement = first / 4;
            sv.Buffer.NumElements = UINT(std::max<uint64_t>(bytes / 4, res ? 1 : 0));
            sv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            dev->CreateShaderResourceView(res, &sv, dst);
        }
        return;
    }
    XImageView* v = d ? d->view : nullptr;
    XImage* img = v ? v->image : nullptr;
    if (img && !img->t.res) img = nullptr;
    const DXGI_FORMAT fmt = v ? ViewFormat(v->format) : DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint32_t mip = v ? v->range.baseMipLevel : 0;
    const uint32_t mips = v ? (v->range.levelCount == VK_REMAINING_MIP_LEVELS ? img ? img->mips - mip : 1 : v->range.levelCount) : 1;
    const uint32_t layer = v ? v->range.baseArrayLayer : 0;
    const uint32_t layers = v ? (v->range.layerCount == VK_REMAINING_ARRAY_LAYERS ? img ? img->layers - layer : 1 : v->range.layerCount) : 1;
    if (e.kind == 'u') {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = fmt;
        if (t.find("RWTexture2DArray") == 0) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; uv.Texture2DArray = {mip, layer, layers, 0}; }
        else if (t.find("RWTexture3D") == 0) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; uv.Texture3D = {mip, 0, UINT(-1)}; }
        else if (t.find("RWTexture1D") == 0) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; uv.Texture1D = {mip}; }
        else { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; uv.Texture2D = {mip, 0}; if (img && img->layers > 1) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; uv.Texture2DArray = {mip, layer, 1, 0}; } }
        if (img) StateFor(cb, TrackFor(img, fmt), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uav);
        dev->CreateUnorderedAccessView(img ? TrackFor(img, fmt).res : nullptr, nullptr, &uv, dst);
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = fmt;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (t.find("TextureCubeArray") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; sv.TextureCubeArray = {mip, mips, layer, std::max(1u, layers / 6), 0.f}; }
    else if (t.find("TextureCube") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; sv.TextureCube = {mip, mips, 0.f}; }
    else if (t.find("Texture2DMSArray") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY; sv.Texture2DMSArray = {layer, layers}; }
    else if (t.find("Texture2DMS") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; }
    else if (t.find("Texture2DArray") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray = {mip, mips, layer, layers, 0, 0.f}; }
    else if (t.find("Texture3D") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; sv.Texture3D = {mip, mips, 0.f}; }
    else if (t.find("Texture1D") == 0) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; sv.Texture1D = {mip, mips, 0.f}; }
    else {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D = {mip, mips, 0, 0.f};
        if (img && img->layers > 1) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray = {mip, mips, layer, 1, 0, 0.f}; }
        if (t.find("Texture2D") != 0) Warn("unhandled SRV type " + t);
    }
    if (img) StateFor(cb, TrackFor(img, fmt), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, uav);
    dev->CreateShaderResourceView(img ? TrackFor(img, fmt).res : nullptr, &sv, dst);
}

void Flush(XCommandBuffer* cb, XPipeline* p, int bp, int setBp = -1) {
    XState& st = St(cb);
    const int sbp = setBp >= 0 ? setBp : bp;
    XDevice* dev = cb->device;
    ID3D12DescriptorHeap* heaps[2] = {dev->srv.heap, dev->smp.heap};
    cb->list->SetDescriptorHeaps(2, heaps);
    if (bp == 1) cb->list->SetComputeRootSignature(p->root); else cb->list->SetGraphicsRootSignature(p->root);
    cb->list->SetPipelineState(p->pso);
    std::vector<ID3D12Resource*> uav;
    for (auto& stage : p->stages) {
        if (stage.viewParam >= 0) {
            UINT base;
            { std::lock_guard<std::mutex> g(dev->lock); base = TakeRange(dev->srv, stage.viewCount); }
            for (const auto& e : stage.views)
                for (uint32_t i = 0; i < e.count; ++i) WriteView(cb, st, p, sbp, e, dev->srv.Cpu(base + e.offset + i), i, uav);
            if (bp == 1) cb->list->SetComputeRootDescriptorTable(UINT(stage.viewParam), dev->srv.Gpu(base));
            else cb->list->SetGraphicsRootDescriptorTable(UINT(stage.viewParam), dev->srv.Gpu(base));
        }
        if (stage.samplerParam >= 0) {
            std::vector<XSampler*> key(stage.samplerCount * 2, nullptr);
            for (const auto& e : stage.samplers) {
                const bool plain = e.name.size() > 6 && e.name.compare(e.name.size() - 6, 6, "_plain") == 0;
                for (uint32_t i = 0; i < e.count; ++i) {
                    uint64_t dyn;
                    const XDescriptor* d = Lookup(st, sbp, plain ? e.space - 1 : e.space, e.binding, i, &dyn);
                    key[(e.offset + i) * 2] = d ? d->sampler : nullptr;
                    key[(e.offset + i) * 2 + 1] = reinterpret_cast<XSampler*>(uintptr_t(plain));
                }
            }
            UINT base;
            {
                std::lock_guard<std::mutex> g(dev->lock);
                auto it = g_samplerTables.find(key);
                if (it == g_samplerTables.end()) {
                    if (dev->smp.next + stage.samplerCount > dev->smp.count) { Warn("sampler heap full, recycling"); g_samplerTables.clear(); dev->smp.next = 0; }
                    base = dev->smp.next;
                    dev->smp.next += stage.samplerCount;
                    for (UINT i = 0; i < stage.samplerCount; ++i) {
                        XSampler* s = key[i * 2];
                        D3D12_SAMPLER_DESC fallback{D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0, 1, D3D12_COMPARISON_FUNC_NEVER, {}, 0, D3D12_FLOAT32_MAX};
                        dev->device->CreateSampler(s ? (key[i * 2 + 1] ? &s->plain : &s->d) : &fallback, dev->smp.Cpu(base + i));
                    }
                    g_samplerTables[key] = base;
                } else base = it->second;
            }
            if (bp == 1) cb->list->SetComputeRootDescriptorTable(UINT(stage.samplerParam), dev->smp.Gpu(base));
            else cb->list->SetGraphicsRootDescriptorTable(UINT(stage.samplerParam), dev->smp.Gpu(base));
        }
        if (stage.memParam >= 0) {
            const UINT memBase = dev->bdaBase + (stage.memSrv ? dev->bdaSlots : 0);
            if (bp == 1) cb->list->SetComputeRootDescriptorTable(UINT(stage.memParam), dev->srv.Gpu(memBase));
            else cb->list->SetGraphicsRootDescriptorTable(UINT(stage.memParam), dev->srv.Gpu(memBase));
        }
    }
    if (bp == 0) {
        cb->list->IASetPrimitiveTopology(p->topology);
        if (st.vbCount) {
            D3D12_VERTEX_BUFFER_VIEW views[16];
            for (uint32_t i = 0; i < st.vbCount; ++i) {
                views[i] = st.vb[i];
                views[i].StrideInBytes = p->strides[i];
                if (st.vbMem[i]) StateFor(cb, st.vbMem[i]->whole, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, uav);
            }
            cb->list->IASetVertexBuffers(0, st.vbCount, views);
        }
        if (st.ibMem) { StateFor(cb, st.ibMem->whole, D3D12_RESOURCE_STATE_INDEX_BUFFER, uav); cb->list->IASetIndexBuffer(&st.ib); }
        if (!p->dynamicBlend) cb->list->OMSetBlendFactor(p->blend);
    }
}

D3D12_BLEND Blend(VkBlendFactor f, bool alpha) {
    static const D3D12_BLEND map[] = {D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_DEST_COLOR,
                                      D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_BLEND_FACTOR, D3D12_BLEND_INV_BLEND_FACTOR,
                                      D3D12_BLEND_BLEND_FACTOR, D3D12_BLEND_INV_BLEND_FACTOR, D3D12_BLEND_SRC_ALPHA_SAT, D3D12_BLEND_SRC1_COLOR, D3D12_BLEND_INV_SRC1_COLOR, D3D12_BLEND_SRC1_ALPHA, D3D12_BLEND_INV_SRC1_ALPHA};
    D3D12_BLEND b = unsigned(f) < 19 ? map[f] : D3D12_BLEND_ONE;
    if (alpha) {
        if (b == D3D12_BLEND_SRC_COLOR) b = D3D12_BLEND_SRC_ALPHA;
        else if (b == D3D12_BLEND_INV_SRC_COLOR) b = D3D12_BLEND_INV_SRC_ALPHA;
        else if (b == D3D12_BLEND_DEST_COLOR) b = D3D12_BLEND_DEST_ALPHA;
        else if (b == D3D12_BLEND_INV_DEST_COLOR) b = D3D12_BLEND_INV_DEST_ALPHA;
        else if (b == D3D12_BLEND_SRC1_COLOR) b = D3D12_BLEND_SRC1_ALPHA;
        else if (b == D3D12_BLEND_INV_SRC1_COLOR) b = D3D12_BLEND_INV_SRC1_ALPHA;
    }
    return b;
}

D3D12_DEPTH_STENCILOP_DESC StencilOp(const VkStencilOpState& s) {
    return {D3D12_STENCIL_OP(s.failOp + 1), D3D12_STENCIL_OP(s.depthFailOp + 1), D3D12_STENCIL_OP(s.passOp + 1), D3D12_COMPARISON_FUNC(s.compareOp + 1)};
}

bool BuildTables(XDevice* dev, XPipeline* p, std::vector<D3D12_ROOT_PARAMETER>& params, std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& ranges, const std::vector<D3D12_ROOT_PARAMETER>& extra = {}) {
    ranges.reserve(p->stages.size() * 3);
    for (auto& s : p->stages) {
        for (int kind = 0; kind < 2; ++kind) {
            auto& list = kind ? s.samplers : s.views;
            if (list.empty()) continue;
            ranges.emplace_back();
            UINT offset = 0;
            for (auto& e : list) {
                D3D12_DESCRIPTOR_RANGE r{};
                r.RangeType = e.kind == 't' ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : e.kind == 'u' ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : e.kind == 'b' ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                r.NumDescriptors = e.count;
                r.BaseShaderRegister = e.reg;
                r.RegisterSpace = e.space;
                r.OffsetInDescriptorsFromTableStart = offset;
                e.offset = offset;
                offset += e.count;
                ranges.back().push_back(r);
            }
            D3D12_ROOT_PARAMETER rp{};
            rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rp.DescriptorTable = {UINT(ranges.back().size()), ranges.back().data()};
            rp.ShaderVisibility = s.vis;
            (kind ? s.samplerParam : s.viewParam) = int(params.size());
            (kind ? s.samplerCount : s.viewCount) = offset;
            params.push_back(rp);
        }
        for (auto& e : s.mem) {
            ranges.emplace_back();
            D3D12_DESCRIPTOR_RANGE r{};
            r.RangeType = e.kind == 't' ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            s.memSrv = e.kind == 't';
            r.NumDescriptors = UINT(-1);
            r.BaseShaderRegister = e.reg;
            r.RegisterSpace = e.space;
            ranges.back().push_back(r);
            D3D12_ROOT_PARAMETER rp{};
            rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rp.DescriptorTable = {1, ranges.back().data()};
            rp.ShaderVisibility = s.vis;
            s.memParam = int(params.size());
            params.push_back(rp);
            break;
        }
    }
    if (!extra.empty()) {
        p->meshParam = int(params.size());
        params.insert(params.end(), extra.begin(), extra.end());
    }
    D3D12_ROOT_SIGNATURE_DESC rs{UINT(params.size()), params.data(), 0, nullptr, p->compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        Log("root signature: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        Release(err);
        return false;
    }
    const HRESULT hr = dev->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&p->root));
    Release(blob);
    return SUCCEEDED(hr);
}

void SplitEntries(XStageTable& s, const std::vector<XEntry>& entries) {
    for (const auto& e : entries) (e.unbounded ? s.mem : e.kind == 's' ? s.samplers : s.views).push_back(e);
}

}

XVK VkResult VKAPI_CALL vkCreateSampler(VkDevice, const VkSamplerCreateInfo* info, const VkAllocationCallbacks*, VkSampler* out) {
    auto* s = new XSampler;
    UINT filter = (info->minFilter == VK_FILTER_LINEAR ? 0x10 : 0) | (info->magFilter == VK_FILTER_LINEAR ? 0x4 : 0) | (info->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? 0x1 : 0);
    if (info->anisotropyEnable) filter = 0x55;
    auto& d = s->d;
    d.AddressU = D3D12_TEXTURE_ADDRESS_MODE(info->addressModeU + 1);
    d.AddressV = D3D12_TEXTURE_ADDRESS_MODE(info->addressModeV + 1);
    d.AddressW = D3D12_TEXTURE_ADDRESS_MODE(info->addressModeW + 1);
    d.MipLODBias = info->mipLodBias;
    d.MaxAnisotropy = info->anisotropyEnable ? UINT(std::clamp(info->maxAnisotropy, 1.f, 16.f)) : 1;
    d.ComparisonFunc = info->compareEnable ? D3D12_COMPARISON_FUNC(info->compareOp + 1) : D3D12_COMPARISON_FUNC_NEVER;
    d.MinLOD = info->minLod;
    d.MaxLOD = info->maxLod;
    const bool white = info->borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE || info->borderColor == VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    const bool opaque = white || info->borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK || info->borderColor == VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = white ? 1.f : 0.f;
    d.BorderColor[3] = opaque ? 1.f : 0.f;
    s->plain = d;
    s->plain.Filter = D3D12_FILTER(filter);
    s->plain.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    d.Filter = D3D12_FILTER(filter | (info->compareEnable ? 0x80 : 0));
    *out = VkSampler(Handle(s));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroySampler(VkDevice, VkSampler s, const VkAllocationCallbacks*) { delete Obj<XSampler>(uint64_t(s)); }

XVK VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo* info, const VkAllocationCallbacks*, VkDescriptorSetLayout* out) {
    auto* l = new XSetLayout;
    for (uint32_t i = 0; i < info->bindingCount; ++i) l->bindings.push_back({info->pBindings[i].binding, info->pBindings[i].descriptorType, info->pBindings[i].descriptorCount});
    std::sort(l->bindings.begin(), l->bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
    *out = VkDescriptorSetLayout(Handle(l));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout l, const VkAllocationCallbacks*) { delete Obj<XSetLayout>(uint64_t(l)); }

XVK void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayoutSupport* s) { s->supported = VK_TRUE; }

XVK VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* out) {
    *out = VkDescriptorPool(Handle(new XPool));
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice, VkDescriptorPool pool, VkDescriptorPoolResetFlags) {
    auto* p = Obj<XPool>(uint64_t(pool));
    for (auto* s : p->sets) delete s;
    p->sets.clear();
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks*) {
    if (!pool) return;
    vkResetDescriptorPool(device, pool, 0);
    delete Obj<XPool>(uint64_t(pool));
}

XVK VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* out) {
    auto* p = Obj<XPool>(uint64_t(info->descriptorPool));
    for (uint32_t i = 0; i < info->descriptorSetCount; ++i) {
        auto* s = new XSet;
        s->layout = Obj<XSetLayout>(uint64_t(info->pSetLayouts[i]));
        for (const auto& b : s->layout->bindings) {
            auto& v = s->slots[b.binding];
            v.resize(b.count);
            for (auto& d : v) d.type = b.type;
        }
        p->sets.push_back(s);
        out[i] = VkDescriptorSet(Handle(s));
    }
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice, VkDescriptorPool pool, uint32_t n, const VkDescriptorSet* sets) {
    auto* p = Obj<XPool>(uint64_t(pool));
    for (uint32_t i = 0; i < n; ++i) {
        auto* s = Obj<XSet>(uint64_t(sets[i]));
        if (!s) continue;
        p->sets.erase(std::remove(p->sets.begin(), p->sets.end(), s), p->sets.end());
        delete s;
    }
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkUpdateDescriptorSets(VkDevice, uint32_t writeCount, const VkWriteDescriptorSet* writes, uint32_t copyCount, const VkCopyDescriptorSet* copies) {
    for (uint32_t i = 0; i < writeCount; ++i) {
        const auto& w = writes[i];
        auto* s = Obj<XSet>(uint64_t(w.dstSet));
        uint32_t binding = w.dstBinding, element = w.dstArrayElement;
        for (uint32_t j = 0; j < w.descriptorCount; ++j) {
            while (s->slots.count(binding) && element >= s->slots[binding].size()) { ++binding; element = 0; }
            auto it = s->slots.find(binding);
            if (it == s->slots.end()) break;
            XDescriptor& d = it->second[element++];
            d.type = w.descriptorType;
            if (w.pBufferInfo) {
                d.buffer = Obj<XBuffer>(uint64_t(w.pBufferInfo[j].buffer));
                d.offset = w.pBufferInfo[j].offset;
                d.range = w.pBufferInfo[j].range;
            }
            if (w.pImageInfo) {
                d.view = Obj<XImageView>(uint64_t(w.pImageInfo[j].imageView));
                if (w.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || w.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) d.sampler = Obj<XSampler>(uint64_t(w.pImageInfo[j].sampler));
            }
            if (w.pTexelBufferView) Warn("texel buffer views are not implemented");
        }
    }
    for (uint32_t i = 0; i < copyCount; ++i) {
        const auto& c = copies[i];
        auto* src = Obj<XSet>(uint64_t(c.srcSet));
        auto* dst = Obj<XSet>(uint64_t(c.dstSet));
        for (uint32_t j = 0; j < c.descriptorCount; ++j) dst->slots[c.dstBinding].at(c.dstArrayElement + j) = src->slots[c.srcBinding].at(c.srcArrayElement + j);
    }
}

XVK VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo* info, const VkAllocationCallbacks*, VkPipelineLayout* out) {
    auto* l = new XPipelineLayout;
    for (uint32_t i = 0; i < info->setLayoutCount; ++i) l->sets.push_back(Obj<XSetLayout>(uint64_t(info->pSetLayouts[i])));
    for (uint32_t i = 0; i < info->pushConstantRangeCount; ++i) l->push = std::max(l->push, info->pPushConstantRanges[i].offset + info->pPushConstantRanges[i].size);
    *out = VkPipelineLayout(Handle(l));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyPipelineLayout(VkDevice, VkPipelineLayout l, const VkAllocationCallbacks*) { delete Obj<XPipelineLayout>(uint64_t(l)); }

XVK VkResult VKAPI_CALL vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks*, VkShaderModule* out) {
    auto* m = new XShaderModule;
    m->words.assign(info->pCode, info->pCode + info->codeSize / 4);
    *out = VkShaderModule(Handle(m));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyShaderModule(VkDevice, VkShaderModule m, const VkAllocationCallbacks*) { delete Obj<XShaderModule>(uint64_t(m)); }

XVK VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache* out) {
    *out = VkPipelineCache(Handle(new int(0)));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyPipelineCache(VkDevice, VkPipelineCache c, const VkAllocationCallbacks*) { delete Obj<int>(uint64_t(c)); }
XVK VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice, VkPipelineCache, size_t* size, void*) { *size = 0; return VK_SUCCESS; }
XVK VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*) { return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass* out) {
    auto* rp = new XRenderPass;
    rp->att.assign(info->pAttachments, info->pAttachments + info->attachmentCount);
    if (info->subpassCount != 1) Log("render pass with %u subpasses: only the first is used", info->subpassCount);
    if (info->subpassCount) {
        const auto& s = info->pSubpasses[0];
        for (uint32_t i = 0; i < s.colorAttachmentCount; ++i) {
            rp->color.push_back(s.pColorAttachments[i].attachment);
            rp->resolve.push_back(s.pResolveAttachments ? s.pResolveAttachments[i].attachment : VK_ATTACHMENT_UNUSED);
        }
        if (s.pDepthStencilAttachment) rp->depth = s.pDepthStencilAttachment->attachment;
    }
    *out = VkRenderPass(Handle(rp));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyRenderPass(VkDevice, VkRenderPass rp, const VkAllocationCallbacks*) { delete Obj<XRenderPass>(uint64_t(rp)); }

XVK VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks*, VkFramebuffer* out) {
    auto* fb = new XFramebuffer;
    for (uint32_t i = 0; i < info->attachmentCount; ++i) fb->views.push_back(Obj<XImageView>(uint64_t(info->pAttachments[i])));
    fb->w = info->width;
    fb->h = info->height;
    fb->layers = info->layers;
    *out = VkFramebuffer(Handle(fb));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyFramebuffer(VkDevice, VkFramebuffer fb, const VkAllocationCallbacks*) { delete Obj<XFramebuffer>(uint64_t(fb)); }

struct GfxCopy {
    VkGraphicsPipelineCreateInfo info{};
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<std::string> names;
    VkPipelineViewportStateCreateInfo vp{};
    VkPipelineViewportDepthClipControlCreateInfoEXT clip{};
    VkPipelineVertexInputStateCreateInfo vi{};
    std::vector<VkVertexInputBindingDescription> vib;
    std::vector<VkVertexInputAttributeDescription> via;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    VkPipelineTessellationStateCreateInfo ts{};
    VkPipelineRasterizationStateCreateInfo rs{};
    VkPipelineMultisampleStateCreateInfo ms{};
    std::vector<VkSampleMask> mask;
    VkPipelineColorBlendStateCreateInfo cb{};
    std::vector<VkPipelineColorBlendAttachmentState> cba;
    VkPipelineDepthStencilStateCreateInfo ds{};
    VkPipelineDynamicStateCreateInfo dy{};
    std::vector<VkDynamicState> dyn;
    XRenderPass rp;
    GfxCopy(const GfxCopy&) = delete;
    GfxCopy& operator=(const GfxCopy&) = delete;
    explicit GfxCopy(const VkGraphicsPipelineCreateInfo& src) : info(src) {
        info.pNext = nullptr;
        stages.assign(src.pStages, src.pStages + src.stageCount);
        for (auto& st : stages) names.push_back(st.pName ? st.pName : "main");
        for (size_t i = 0; i < stages.size(); ++i) {
            stages[i].pNext = nullptr;
            stages[i].pSpecializationInfo = nullptr;
            stages[i].pName = names[i].c_str();
        }
        info.pStages = stages.data();
        if (src.pViewportState) {
            vp = *src.pViewportState;
            vp.pNext = nullptr;
            vp.pViewports = nullptr;
            vp.pScissors = nullptr;
            for (auto* e = static_cast<const VkBaseInStructure*>(src.pViewportState->pNext); e; e = e->pNext)
                if (e->sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT) {
                    clip = *reinterpret_cast<const VkPipelineViewportDepthClipControlCreateInfoEXT*>(e);
                    clip.pNext = nullptr;
                    vp.pNext = &clip;
                }
            info.pViewportState = &vp;
        }
        if (src.pVertexInputState) {
            vi = *src.pVertexInputState;
            vi.pNext = nullptr;
            if (vi.vertexBindingDescriptionCount) vib.assign(vi.pVertexBindingDescriptions, vi.pVertexBindingDescriptions + vi.vertexBindingDescriptionCount);
            if (vi.vertexAttributeDescriptionCount) via.assign(vi.pVertexAttributeDescriptions, vi.pVertexAttributeDescriptions + vi.vertexAttributeDescriptionCount);
            vi.pVertexBindingDescriptions = vib.data();
            vi.pVertexAttributeDescriptions = via.data();
            info.pVertexInputState = &vi;
        }
        if (src.pInputAssemblyState) { ia = *src.pInputAssemblyState; ia.pNext = nullptr; info.pInputAssemblyState = &ia; }
        if (src.pTessellationState) { ts = *src.pTessellationState; ts.pNext = nullptr; info.pTessellationState = &ts; }
        if (src.pRasterizationState) { rs = *src.pRasterizationState; rs.pNext = nullptr; info.pRasterizationState = &rs; }
        if (src.pMultisampleState) {
            ms = *src.pMultisampleState;
            ms.pNext = nullptr;
            if (ms.pSampleMask) {
                mask.assign(ms.pSampleMask, ms.pSampleMask + (std::max)(1u, (uint32_t(ms.rasterizationSamples) + 31) / 32));
                ms.pSampleMask = mask.data();
            }
            info.pMultisampleState = &ms;
        }
        if (src.pColorBlendState) {
            cb = *src.pColorBlendState;
            cb.pNext = nullptr;
            if (cb.attachmentCount && cb.pAttachments) cba.assign(cb.pAttachments, cb.pAttachments + cb.attachmentCount);
            cb.pAttachments = cba.empty() ? nullptr : cba.data();
            info.pColorBlendState = &cb;
        }
        if (src.pDepthStencilState) { ds = *src.pDepthStencilState; ds.pNext = nullptr; info.pDepthStencilState = &ds; }
        if (src.pDynamicState) {
            dy = *src.pDynamicState;
            dy.pNext = nullptr;
            if (dy.dynamicStateCount) dyn.assign(dy.pDynamicStates, dy.pDynamicStates + dy.dynamicStateCount);
            dy.pDynamicStates = dyn.data();
            info.pDynamicState = &dy;
        }
        if (auto* r = Obj<XRenderPass>(uint64_t(src.renderPass))) {
            rp = *r;
            info.renderPass = VkRenderPass(Handle(&rp));
        }
    }
};

bool ClipFix(const VkGraphicsPipelineCreateInfo& info) {
    if (info.pViewportState)
        for (auto* e = static_cast<const VkBaseInStructure*>(info.pViewportState->pNext); e; e = e->pNext)
            if (e->sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT) return reinterpret_cast<const VkPipelineViewportDepthClipControlCreateInfoEXT*>(e)->negativeOneToOne == VK_TRUE;
    return false;
}

bool IsTess(VkShaderStageFlagBits s) { return s == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || s == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; }

void BuildGraphics(XDevice* dev, XPipeline* p, const VkGraphicsPipelineCreateInfo& info, const std::vector<std::vector<uint32_t>>& words) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    std::vector<Converted> code;
    code.reserve(info.stageCount + 1);
    bool ok = true, tess = false;
    std::string vsHlsl, baryGsHlsl;
    bool clipFix = false;
    if (info.pViewportState)
        for (auto* e = static_cast<const VkBaseInStructure*>(info.pViewportState->pNext); e; e = e->pNext)
            if (e->sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT) clipFix = reinterpret_cast<const VkPipelineViewportDepthClipControlCreateInfoEXT*>(e)->negativeOneToOne == VK_TRUE;
    for (uint32_t i = 0; i < info.stageCount; ++i) {
        const auto& s = info.pStages[i];
        if (s.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || s.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) { tess = true; continue; }
        code.push_back(Convert(words[i], clipFix && s.stage == VK_SHADER_STAGE_VERTEX_BIT));
        const auto& c = code.back();
        if (!c.ok) { ok = false; continue; }
        if (s.stage == VK_SHADER_STAGE_MESH_BIT_EXT) {
            if (c.meshVs.empty()) { Log("mesh shader without an emulation vertex shader"); ok = false; continue; }
            auto* m = new XPipeline;
            m->compute = true;
            m->layout = p->layout;
            XStageTable mt;
            SplitEntries(mt, c.entries);
            m->stages.push_back(mt);
            std::vector<D3D12_ROOT_PARAMETER> mparams, extra(2);
            std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> mranges;
            extra[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            extra[0].Constants = {0, 11, 4};
            extra[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            extra[1].Descriptor = {0, 11};
            p->mesh = m;
            if (!BuildTables(dev, m, mparams, mranges, extra)) { Log("mesh compute root signature failed"); ok = false; continue; }
            D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
            cd.pRootSignature = m->root;
            cd.CS = {c.dxbc.data(), c.dxbc.size()};
            const HRESULT mhr = dev->device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&m->pso));
            if (FAILED(mhr)) { Log("mesh compute pipeline state failed %08x", unsigned(mhr)); ok = false; continue; }
            p->meshPrims = c.meshPrims;
            p->meshBlock = c.meshBlock;
            pd.VS = {c.meshVs.data(), c.meshVs.size()};
            continue;
        }
        XStageTable t;
        D3D12_SHADER_BYTECODE bc{c.dxbc.data(), c.dxbc.size()};
        switch (s.stage) {
        case VK_SHADER_STAGE_VERTEX_BIT: t.vis = D3D12_SHADER_VISIBILITY_VERTEX; pd.VS = bc; vsHlsl = c.hlsl; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT: t.vis = D3D12_SHADER_VISIBILITY_PIXEL; pd.PS = bc; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT: t.vis = D3D12_SHADER_VISIBILITY_GEOMETRY; pd.GS = bc; break;
        default: Log("graphics stage %x is not supported", unsigned(s.stage)); ok = false; break;
        }
        if (!c.gs.empty() && s.stage == VK_SHADER_STAGE_FRAGMENT_BIT && !pd.GS.pShaderBytecode) { pd.GS = {c.gs.data(), c.gs.size()}; baryGsHlsl = c.gsHlsl; }
        SplitEntries(t, c.entries);
        p->stages.push_back(t);
    }
    std::vector<char> rectGs, baryGs;
    if (ok && !baryGsHlsl.empty() && !vsHlsl.empty()) {
        const std::string src = BaryGsForVs(baryGsHlsl, vsHlsl);
        if (!src.empty()) baryGs = CompileGs(src, "barycentric");
        if (!baryGs.empty()) pd.GS = {baryGs.data(), baryGs.size()};
    }
    if (tess && ok) {
        const bool rect = info.pInputAssemblyState && info.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST && info.pTessellationState && info.pTessellationState->patchControlPoints == 3;
        if (!rect) { Log("tessellation pipeline that is not a rect list is not supported"); ok = false; }
        else if (pd.GS.pShaderBytecode) { Log("rect-list pipeline with a barycentric geometry shader is not supported"); ok = false; }
        else {
            rectGs = RectListGs(vsHlsl);
            if (rectGs.empty()) ok = false;
            else pd.GS = {rectGs.data(), rectGs.size()};
        }
    }
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;
    std::vector<D3D12_ROOT_PARAMETER> gextra;
    if (p->mesh) {
        gextra.resize(1);
        gextra[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gextra[0].Descriptor = {0, 11};
        gextra[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (!ok || !BuildTables(dev, p, params, ranges, gextra)) { Log("graphics pipeline skipped"); return; }
    pd.pRootSignature = p->root;
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    if (info.pVertexInputState && !p->mesh) {
        const auto& vi = *info.pVertexInputState;
        for (uint32_t i = 0; i < vi.vertexBindingDescriptionCount; ++i) if (vi.pVertexBindingDescriptions[i].binding < 16) p->strides[vi.pVertexBindingDescriptions[i].binding] = vi.pVertexBindingDescriptions[i].stride;
        for (uint32_t i = 0; i < vi.vertexAttributeDescriptionCount; ++i) {
            const auto& a = vi.pVertexAttributeDescriptions[i];
            bool instance = false;
            for (uint32_t j = 0; j < vi.vertexBindingDescriptionCount; ++j) if (vi.pVertexBindingDescriptions[j].binding == a.binding) instance = vi.pVertexBindingDescriptions[j].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE;
            elements.push_back({"TEXCOORD", a.location, VertexFormat(a.format), a.binding, a.offset, instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, instance ? 1u : 0u});
            if (elements.back().Format == DXGI_FORMAT_UNKNOWN) Log("vertex format %d unsupported", int(a.format));
        }
    }
    pd.InputLayout = {elements.data(), UINT(elements.size())};
    const auto topo = info.pInputAssemblyState && !p->mesh ? info.pInputAssemblyState->topology : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    switch (topo) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: p->topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT; break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: p->topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: p->topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: p->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: p->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: p->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
    default: Log("topology %d unsupported", int(topo)); p->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
    }
    if (info.pInputAssemblyState && info.pInputAssemblyState->primitiveRestartEnable) pd.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    auto& rs = pd.RasterizerState;
    rs.FillMode = D3D12_FILL_MODE_SOLID;
    rs.CullMode = D3D12_CULL_MODE_NONE;
    rs.DepthClipEnable = TRUE;
    if (info.pRasterizationState) {
        const auto& r = *info.pRasterizationState;
        rs.FillMode = r.polygonMode == VK_POLYGON_MODE_LINE ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        rs.CullMode = r.cullMode == VK_CULL_MODE_FRONT_BIT ? D3D12_CULL_MODE_FRONT : r.cullMode == VK_CULL_MODE_BACK_BIT ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        if (r.cullMode == VK_CULL_MODE_FRONT_AND_BACK) Warn("cull front and back is drawn without culling");
        rs.FrontCounterClockwise = r.frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.DepthClipEnable = !r.depthClampEnable;
        if (r.depthBiasEnable) { rs.DepthBias = INT(r.depthBiasConstantFactor); rs.DepthBiasClamp = r.depthBiasClamp; rs.SlopeScaledDepthBias = r.depthBiasSlopeFactor; }
        if (r.rasterizerDiscardEnable) pd.PS = {};
    }
    pd.SampleMask = UINT_MAX;
    pd.SampleDesc.Count = info.pMultisampleState ? UINT(info.pMultisampleState->rasterizationSamples) : 1;
    if (info.pMultisampleState && info.pMultisampleState->pSampleMask) pd.SampleMask = info.pMultisampleState->pSampleMask[0];
    if (info.pMultisampleState) pd.BlendState.AlphaToCoverageEnable = info.pMultisampleState->alphaToCoverageEnable;
    auto* rp = Obj<XRenderPass>(uint64_t(info.renderPass));
    if (rp) {
        pd.NumRenderTargets = UINT(rp->color.size());
        for (size_t i = 0; i < rp->color.size() && i < 8; ++i) pd.RTVFormats[i] = rp->color[i] == VK_ATTACHMENT_UNUSED ? DXGI_FORMAT_UNKNOWN : ViewFormat(rp->att[rp->color[i]].format);
        if (rp->depth != VK_ATTACHMENT_UNUSED) { auto* fi = Format(rp->att[rp->depth].format); pd.DSVFormat = fi ? fi->dx : DXGI_FORMAT_UNKNOWN; }
    }
    pd.BlendState.IndependentBlendEnable = TRUE;
    for (int i = 0; i < 8; ++i) pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (info.pColorBlendState) {
        const auto& cbs = *info.pColorBlendState;
        std::memcpy(p->blend, cbs.blendConstants, sizeof p->blend);
        for (uint32_t i = 0; i < cbs.attachmentCount && i < 8; ++i) {
            const auto& a = cbs.pAttachments[i];
            auto& t = pd.BlendState.RenderTarget[i];
            t.BlendEnable = a.blendEnable;
            t.SrcBlend = Blend(a.srcColorBlendFactor, false);
            t.DestBlend = Blend(a.dstColorBlendFactor, false);
            t.BlendOp = D3D12_BLEND_OP(a.colorBlendOp + 1);
            t.SrcBlendAlpha = Blend(a.srcAlphaBlendFactor, true);
            t.DestBlendAlpha = Blend(a.dstAlphaBlendFactor, true);
            t.BlendOpAlpha = D3D12_BLEND_OP(a.alphaBlendOp + 1);
            t.RenderTargetWriteMask = UINT8(a.colorWriteMask);
            t.LogicOp = D3D12_LOGIC_OP_NOOP;
        }
    }
    if (info.pDepthStencilState && pd.DSVFormat != DXGI_FORMAT_UNKNOWN) {
        const auto& ds = *info.pDepthStencilState;
        pd.DepthStencilState.DepthEnable = ds.depthTestEnable || ds.depthWriteEnable;
        pd.DepthStencilState.DepthWriteMask = ds.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = ds.depthTestEnable ? D3D12_COMPARISON_FUNC(ds.depthCompareOp + 1) : D3D12_COMPARISON_FUNC_ALWAYS;
        pd.DepthStencilState.StencilEnable = ds.stencilTestEnable;
        pd.DepthStencilState.StencilReadMask = UINT8(ds.front.compareMask);
        pd.DepthStencilState.StencilWriteMask = UINT8(ds.front.writeMask);
        pd.DepthStencilState.FrontFace = StencilOp(ds.front);
        pd.DepthStencilState.BackFace = StencilOp(ds.back);
    }
    if (info.pDynamicState)
        for (uint32_t i = 0; i < info.pDynamicState->dynamicStateCount; ++i) {
            const auto ds = info.pDynamicState->pDynamicStates[i];
            if (ds == VK_DYNAMIC_STATE_BLEND_CONSTANTS) p->dynamicBlend = true;
            else if (ds != VK_DYNAMIC_STATE_VIEWPORT && ds != VK_DYNAMIC_STATE_SCISSOR && ds != VK_DYNAMIC_STATE_STENCIL_REFERENCE) Warn("dynamic state " + std::to_string(int(ds)) + " uses the pipeline value");
        }
    const HRESULT hr = dev->device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&p->pso));
    if (FAILED(hr)) Log("graphics pipeline state failed %08x (vs %zu ps %zu bytes, %u rt)", unsigned(hr), pd.VS.BytecodeLength, pd.PS.BytecodeLength, pd.NumRenderTargets);
}

XVK VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache, uint32_t n, const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks*, VkPipeline* out) {
    PerfTimer perfTimer(g_perf.pipeUs); g_perf.pipes += n;
    auto* dev = reinterpret_cast<XDevice*>(device);
    const bool local = LocalShaders() && !std::getenv("XVK_SYNC_PIPELINES");
    for (uint32_t k = 0; k < n; ++k) {
        const auto& info = infos[k];
        auto* p = new XPipeline;
        out[k] = VkPipeline(Handle(p));
        p->layout = Obj<XPipelineLayout>(uint64_t(info.layout));
        const bool clipFix = ClipFix(info);
        std::vector<std::vector<uint32_t>> words(info.stageCount);
        std::vector<bool> fix(info.stageCount, false);
        bool ready = true;
        for (uint32_t i = 0; i < info.stageCount; ++i) {
            const auto& s = info.pStages[i];
            if (IsTess(s.stage)) continue;
            words[i] = Specialize(Obj<XShaderModule>(uint64_t(s.module))->words, s.pSpecializationInfo);
            fix[i] = clipFix && s.stage == VK_SHADER_STAGE_VERTEX_BIT;
            ready = ready && ShaderReady(words[i], fix[i]);
        }
        if (ready || !local) { BuildGraphics(dev, p, info, words); continue; }
        for (size_t i = 0; i < words.size(); ++i)
            if (!words[i].empty()) p->hash ^= Fnv(words[i].data(), words[i].size() * 4) ^ (fix[i] ? 0x6e31315f636c6970ull : 0);
        p->ready.store(false);
        auto copy = std::make_shared<GfxCopy>(info);
        Enqueue([dev, p, copy, words, fix] {
            for (size_t i = 0; i < words.size(); ++i)
                if (!words[i].empty()) PrepareShader(words[i], fix[i]);
            BuildGraphics(dev, p, copy->info, words);
            MarkReady(p);
        });
    }
    return VK_SUCCESS;
}

void BuildCompute(XDevice* dev, XPipeline* p, const std::vector<uint32_t>& words) {
    Converted c = Convert(words);
    if (!c.ok) { Log("compute pipeline skipped"); return; }
    XStageTable t;
    SplitEntries(t, c.entries);
    p->stages.push_back(t);
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;
    if (!BuildTables(dev, p, params, ranges)) { Log("compute root signature failed"); return; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
    cd.pRootSignature = p->root;
    cd.CS = {c.dxbc.data(), c.dxbc.size()};
    const HRESULT hr = dev->device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&p->pso));
    if (FAILED(hr)) Log("compute pipeline state failed %08x", unsigned(hr));
}

XVK VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache, uint32_t n, const VkComputePipelineCreateInfo* infos, const VkAllocationCallbacks*, VkPipeline* out) {
    PerfTimer perfTimer(g_perf.pipeUs); g_perf.pipes += n;
    auto* dev = reinterpret_cast<XDevice*>(device);
    const bool local = LocalShaders() && !std::getenv("XVK_SYNC_PIPELINES");
    for (uint32_t k = 0; k < n; ++k) {
        auto* p = new XPipeline;
        p->compute = true;
        out[k] = VkPipeline(Handle(p));
        p->layout = Obj<XPipelineLayout>(uint64_t(infos[k].layout));
        const auto& s = infos[k].stage;
        std::vector<uint32_t> words = Specialize(Obj<XShaderModule>(uint64_t(s.module))->words, s.pSpecializationInfo);
        if (!local || ShaderReady(words, false)) { BuildCompute(dev, p, words); continue; }
        p->hash = Fnv(words.data(), words.size() * 4);
        p->ready.store(false);
        Enqueue([dev, p, words] {
            PrepareShader(words, false);
            BuildCompute(dev, p, words);
            MarkReady(p);
        });
    }
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyPipeline(VkDevice, VkPipeline pipeline, const VkAllocationCallbacks*) {
    auto* p = Obj<XPipeline>(uint64_t(pipeline));
    if (!p) return;
    while (!p->ready.load(std::memory_order_acquire)) Sleep(1);
    if (p->mesh) {
        Release(p->mesh->pso);
        Release(p->mesh->root);
        delete p->mesh;
    }
    Release(p->pso);
    Release(p->root);
    delete p;
}

XVK void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer buffer, VkPipelineBindPoint bp, VkPipeline pipeline) {
    auto& st = St(reinterpret_cast<XCommandBuffer*>(buffer));
    (bp == VK_PIPELINE_BIND_POINT_COMPUTE ? st.compute : st.graphics) = Obj<XPipeline>(uint64_t(pipeline));
}

XVK void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer buffer, VkPipelineBindPoint bp, VkPipelineLayout, uint32_t first, uint32_t n, const VkDescriptorSet* sets, uint32_t dynamicCount, const uint32_t* dynamic) {
    auto& st = St(reinterpret_cast<XCommandBuffer*>(buffer));
    const int b = bp == VK_PIPELINE_BIND_POINT_COMPUTE ? 1 : 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < n && first + i < 8; ++i) {
        auto& bound = st.sets[b][first + i];
        bound.set = Obj<XSet>(uint64_t(sets[i]));
        bound.dynamic.clear();
        if (!bound.set) continue;
        for (const auto& lb : bound.set->layout->bindings)
            if (lb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || lb.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                for (uint32_t j = 0; j < lb.count && used < dynamicCount; ++j) bound.dynamic.push_back(dynamic[used++]);
    }
}

XVK void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer buffer, VkPipelineLayout, VkShaderStageFlags, uint32_t offset, uint32_t size, const void* data) {
    auto& st = St(reinterpret_cast<XCommandBuffer*>(buffer));
    if (offset + size <= sizeof st.push) std::memcpy(st.push + offset, data, size);
}

XVK void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer buffer, uint32_t first, uint32_t n, const VkBuffer* buffers, const VkDeviceSize* offsets) {
    auto& st = St(reinterpret_cast<XCommandBuffer*>(buffer));
    for (uint32_t i = 0; i < n && first + i < 16; ++i) {
        auto* b = Obj<XBuffer>(uint64_t(buffers[i]));
        auto& v = st.vb[first + i];
        v = {};
        st.vbMem[first + i] = nullptr;
        if (b && b->memory && EnsureWhole(b->memory)) {
            v.BufferLocation = b->memory->whole.res->GetGPUVirtualAddress() + b->offset + offsets[i];
            v.SizeInBytes = UINT(b->size - offsets[i]);
            st.vbMem[first + i] = b->memory;
        }
    }
    st.vbCount = std::max(st.vbCount, std::min(16u, first + n));
}

XVK void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer buffer, VkBuffer ib, VkDeviceSize offset, VkIndexType type) {
    auto& st = St(reinterpret_cast<XCommandBuffer*>(buffer));
    auto* b = Obj<XBuffer>(uint64_t(ib));
    st.ibMem = nullptr;
    if (!b || !b->memory || !EnsureWhole(b->memory)) return;
    st.ib.BufferLocation = b->memory->whole.res->GetGPUVirtualAddress() + b->offset + offset;
    st.ib.SizeInBytes = UINT(b->size - offset);
    st.ib.Format = type == VK_INDEX_TYPE_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    st.ibMem = b->memory;
}

XVK void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer buffer, uint32_t first, uint32_t n, const VkViewport* v) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    D3D12_VIEWPORT vp[16];
    for (uint32_t i = 0; i < n && i < 16; ++i) {
        vp[i] = {v[i].x, v[i].y, v[i].width, v[i].height, v[i].minDepth, v[i].maxDepth};
        if (vp[i].Height < 0) { vp[i].TopLeftY += vp[i].Height; vp[i].Height = -vp[i].Height; Warn("negative viewport height drawn upside down"); }
    }
    if (first == 0) cb->list->RSSetViewports(std::min(n, 16u), vp);
}

XVK void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer buffer, uint32_t first, uint32_t n, const VkRect2D* r) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    D3D12_RECT rc[16];
    for (uint32_t i = 0; i < n && i < 16; ++i) rc[i] = {r[i].offset.x, r[i].offset.y, LONG(r[i].offset.x + int64_t(r[i].extent.width)), LONG(r[i].offset.y + int64_t(r[i].extent.height))};
    if (first == 0) cb->list->RSSetScissorRects(std::min(n, 16u), rc);
}

XVK void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer buffer, VkStencilFaceFlags, uint32_t ref) { reinterpret_cast<XCommandBuffer*>(buffer)->list->OMSetStencilRef(ref); }
XVK void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer buffer, const float c[4]) { reinterpret_cast<XCommandBuffer*>(buffer)->list->OMSetBlendFactor(c); }
XVK void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer, float, float, float) { Warn("dynamic depth bias uses the pipeline value"); }
XVK void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer, float, float) {}
XVK void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice, VkImage, uint32_t* count, VkSparseImageMemoryRequirements*) { *count = 0; }
XVK void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t* count, VkSparseImageFormatProperties*) { *count = 0; }
XVK void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t) { Warn("dynamic stencil compare mask uses the pipeline value"); }
XVK void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t) { Warn("dynamic stencil write mask uses the pipeline value"); }

XVK void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer buffer, const VkRenderPassBeginInfo* info, VkSubpassContents) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    auto* dev = cb->device;
    st.pass = Obj<XRenderPass>(uint64_t(info->renderPass));
    st.fb = Obj<XFramebuffer>(uint64_t(info->framebuffer));
    st.rtvs.clear();
    st.hasDsv = false;
    const D3D12_RECT area{info->renderArea.offset.x, info->renderArea.offset.y, LONG(info->renderArea.offset.x + int64_t(info->renderArea.extent.width)), LONG(info->renderArea.offset.y + int64_t(info->renderArea.extent.height))};
    for (uint32_t a : st.pass->color) {
        D3D12_CPU_DESCRIPTOR_HANDLE h{};
        if (a != VK_ATTACHMENT_UNUSED && a < st.fb->views.size()) {
            XImageView* v = st.fb->views[a];
            UINT idx;
            { std::lock_guard<std::mutex> g(dev->lock); idx = dev->rtv.Take(); }
            h = dev->rtv.Cpu(idx);
            D3D12_RENDER_TARGET_VIEW_DESC rv = RtvFor(v->image->desc, ViewFormat(v->format), v->range.baseMipLevel, v->range.baseArrayLayer, std::max(1u, st.fb->layers));
            Tracked& target = TrackFor(v->image, rv.Format);
            dev->device->CreateRenderTargetView(target.res, &rv, h);
            Use(cb->list, target, D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (v->image->fresh && &target == &v->image->t) { cb->list->DiscardResource(v->image->t.res, nullptr); v->image->fresh = false; }
            if (st.pass->att[a].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && a < info->clearValueCount) cb->list->ClearRenderTargetView(h, info->pClearValues[a].color.float32, 1, &area);
        }
        st.rtvs.push_back(h);
    }
    const uint32_t d = st.pass->depth;
    if (d != VK_ATTACHMENT_UNUSED && d < st.fb->views.size()) {
        XImageView* v = st.fb->views[d];
        UINT idx;
        { std::lock_guard<std::mutex> g(dev->lock); idx = dev->dsv.Take(); }
        st.dsv = dev->dsv.Cpu(idx);
        st.hasDsv = true;
        D3D12_DEPTH_STENCIL_VIEW_DESC dv = DsvFor(v->image->desc, Format(v->format)->dx, v->range.baseMipLevel, v->range.baseArrayLayer, std::max(1u, st.fb->layers));
        dev->device->CreateDepthStencilView(v->image->t.res, &dv, st.dsv);
        Use(cb->list, v->image->t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        const auto& at = st.pass->att[d];
        UINT flags = (at.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? D3D12_CLEAR_FLAG_DEPTH : 0) | (at.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && Format(v->format)->stencil ? D3D12_CLEAR_FLAG_STENCIL : 0);
        if (flags && d < info->clearValueCount) cb->list->ClearDepthStencilView(st.dsv, D3D12_CLEAR_FLAGS(flags), info->pClearValues[d].depthStencil.depth, UINT8(info->pClearValues[d].depthStencil.stencil), 1, &area);
    }
    cb->list->OMSetRenderTargets(UINT(st.rtvs.size()), st.rtvs.data(), FALSE, st.hasDsv ? &st.dsv : nullptr);
}

XVK void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer buffer) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    if (!st.pass) return;
    for (size_t i = 0; i < st.pass->resolve.size(); ++i) {
        const uint32_t r = st.pass->resolve[i], c = st.pass->color[i];
        if (r == VK_ATTACHMENT_UNUSED || c == VK_ATTACHMENT_UNUSED) continue;
        XImageView* src = st.fb->views[c];
        XImageView* dst = st.fb->views[r];
        Use(cb->list, src->image->t, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        Use(cb->list, dst->image->t, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cb->list->ResolveSubresource(dst->image->t.res, Subresource(dst->image, dst->range.baseMipLevel, dst->range.baseArrayLayer, 0), src->image->t.res, 0, ViewFormat(src->format));
    }
    for (size_t a = 0; a < st.pass->att.size() && a < st.fb->views.size(); ++a)
        if (st.pass->att[a].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) Use(cb->list, st.fb->views[a]->image->t, D3D12_RESOURCE_STATE_PRESENT);
    st.pass = nullptr;
    st.rtvs.clear();
    st.hasDsv = false;
}

XVK void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer buffer, uint32_t n, const VkClearAttachment* atts, uint32_t rectCount, const VkClearRect* rects) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    std::vector<D3D12_RECT> rc;
    for (uint32_t i = 0; i < rectCount; ++i) rc.push_back({rects[i].rect.offset.x, rects[i].rect.offset.y, LONG(rects[i].rect.offset.x + int64_t(rects[i].rect.extent.width)), LONG(rects[i].rect.offset.y + int64_t(rects[i].rect.extent.height))});
    for (uint32_t i = 0; i < n; ++i) {
        const auto& a = atts[i];
        if (a.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            if (a.colorAttachment < st.rtvs.size() && st.rtvs[a.colorAttachment].ptr) cb->list->ClearRenderTargetView(st.rtvs[a.colorAttachment], a.clearValue.color.float32, UINT(rc.size()), rc.data());
        } else if (st.hasDsv) {
            UINT flags = ((a.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) ? D3D12_CLEAR_FLAG_DEPTH : 0) | ((a.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) ? D3D12_CLEAR_FLAG_STENCIL : 0);
            cb->list->ClearDepthStencilView(st.dsv, D3D12_CLEAR_FLAGS(flags), a.clearValue.depthStencil.depth, UINT8(a.clearValue.depthStencil.stencil), UINT(rc.size()), rc.data());
        }
    }
}

XVK void VKAPI_CALL vkCmdDraw(VkCommandBuffer buffer, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    ++g_perf.draws;
    if (SkipWork('d')) return;
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    if (Deferred(st.graphics, "draw")) return;
    if (!st.graphics || !st.graphics->pso) { Warn("draw skipped: pipeline unavailable"); return; }
    Flush(cb, st.graphics, 0);
    cb->list->DrawInstanced(vertices, instances, firstVertex, firstInstance);
}

XVK void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer buffer, uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    ++g_perf.draws;
    if (SkipWork('d')) return;
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    if (Deferred(st.graphics, "draw")) return;
    if (!st.graphics || !st.graphics->pso) { Warn("draw skipped: pipeline unavailable"); return; }
    Flush(cb, st.graphics, 0);
    cb->list->DrawIndexedInstanced(indices, instances, firstIndex, vertexOffset, firstInstance);
}

XVK void VKAPI_CALL vkCmdDispatch(VkCommandBuffer buffer, uint32_t x, uint32_t y, uint32_t z) {
    ++g_perf.draws;
    if (SkipWork('x')) return;
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    if (Deferred(st.compute, "dispatch")) return;
    if (!st.compute || !st.compute->pso) { Warn("dispatch skipped: pipeline unavailable"); return; }
    Flush(cb, st.compute, 1);
    cb->list->Dispatch(x, y, z);
}

std::mutex g_meshLock;
ID3D12Resource* g_meshScratch = nullptr;
uint64_t g_meshScratchSize = 0;
std::vector<ID3D12Resource*> g_meshRetired;

ID3D12Resource* MeshScratch(XDevice* dev, uint64_t need) {
    std::lock_guard<std::mutex> g(g_meshLock);
    if (g_meshScratch && g_meshScratchSize >= need) return g_meshScratch;
    const uint64_t size = std::max<uint64_t>(std::max<uint64_t>(need, g_meshScratchSize * 2), 16ull << 20);
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    if (g_meshScratch) g_meshRetired.push_back(g_meshScratch);
    g_meshScratch = r;
    g_meshScratchSize = size;
    return r;
}

XVK void VKAPI_CALL vkCmdDrawMeshTasksEXT(VkCommandBuffer buffer, uint32_t x, uint32_t y, uint32_t z) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    XPipeline* p = st.graphics;
    if (Deferred(p, "mesh draw")) return;
    if (!p || !p->pso || !p->mesh || !p->mesh->pso) { Warn("mesh draw skipped: pipeline unavailable"); return; }
    const uint64_t groups = uint64_t(x) * y * z;
    if (!groups) return;
    ID3D12Resource* scratch = MeshScratch(cb->device, groups * p->meshBlock * 4);
    if (!scratch) { Warn("mesh draw skipped: scratch buffer allocation failed"); return; }
    const D3D12_GPU_VIRTUAL_ADDRESS va = scratch->GetGPUVirtualAddress();
    Flush(cb, p->mesh, 1, 0);
    const UINT counts[4] = {x, y, z, 0};
    cb->list->SetComputeRoot32BitConstants(UINT(p->mesh->meshParam), 4, counts, 0);
    cb->list->SetComputeRootUnorderedAccessView(UINT(p->mesh->meshParam + 1), va);
    cb->list->Dispatch(x, y, z);
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b[0].UAV.pResource = scratch;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition = {scratch, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    cb->list->ResourceBarrier(2, b);
    Flush(cb, p, 0);
    cb->list->SetGraphicsRootShaderResourceView(UINT(p->meshParam), va);
    cb->list->DrawInstanced(UINT(groups * p->meshPrims * 3), 1, 0, 0);
    std::swap(b[1].Transition.StateBefore, b[1].Transition.StateAfter);
    cb->list->ResourceBarrier(1, &b[1]);
}
XVK void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t) { Warn("mesh indirect draw skipped: not emulated yet"); }

ID3D12CommandSignature* Signature(XDevice* dev, D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride) {
    const UINT natural = type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ? 16 : type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ? 20 : 12;
    stride = std::max(stride, natural);
    std::lock_guard<std::mutex> g(dev->lock);
    auto& sig = g_signatures[(uint64_t(type) << 32) | stride];
    if (!sig) {
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = type;
        D3D12_COMMAND_SIGNATURE_DESC d{stride, 1, &arg, 0};
        dev->device->CreateCommandSignature(&d, nullptr, IID_PPV_ARGS(&sig));
    }
    return sig;
}

void Indirect(VkCommandBuffer buffer, VkBuffer args, VkDeviceSize offset, uint32_t count, uint32_t stride, D3D12_INDIRECT_ARGUMENT_TYPE type) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto& st = St(cb);
    XPipeline* p = type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH ? st.compute : st.graphics;
    auto* b = Obj<XBuffer>(uint64_t(args));
    if (Deferred(p, "indirect call")) return;
    if (!p || !p->pso || !b || !b->memory || !EnsureWhole(b->memory)) { Warn("indirect call skipped"); return; }
    Flush(cb, p, type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH ? 1 : 0);
    Use(cb->list, b->memory->whole, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    cb->list->ExecuteIndirect(Signature(cb->device, type, stride), count, b->memory->whole.res, b->offset + offset, nullptr, 0);
}

XVK void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer b, VkBuffer args, VkDeviceSize offset, uint32_t count, uint32_t stride) { Indirect(b, args, offset, count, stride, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW); }
XVK void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer b, VkBuffer args, VkDeviceSize offset, uint32_t count, uint32_t stride) { Indirect(b, args, offset, count, stride, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED); }
XVK void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer b, VkBuffer args, VkDeviceSize offset) { Indirect(b, args, offset, 1, 12, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH); }

XVK VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* info, const VkAllocationCallbacks*, VkQueryPool* out) {
    auto* dev = reinterpret_cast<XDevice*>(device);
    auto* q = new XQueryPool;
    q->vk = info->queryType;
    q->count = info->queryCount;
    q->type = info->queryType == VK_QUERY_TYPE_TIMESTAMP ? D3D12_QUERY_HEAP_TYPE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    if (info->queryType != VK_QUERY_TYPE_TIMESTAMP && info->queryType != VK_QUERY_TYPE_OCCLUSION) Log("query type %d is not supported", int(info->queryType));
    D3D12_QUERY_HEAP_DESC hd{q->type, q->count, 0};
    dev->device->CreateQueryHeap(&hd, IID_PPV_ARGS(&q->heap));
    if (q->vk == VK_QUERY_TYPE_TIMESTAMP && FAILED(dev->queue.queue->GetTimestampFrequency(&q->freq))) q->freq = 0;
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = AlignUp(uint64_t(q->count) * 8, 256);
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    dev->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&q->readback));
    if (q->readback) q->readback->Map(0, nullptr, reinterpret_cast<void**>(&q->mapped));
    *out = VkQueryPool(Handle(q));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyQueryPool(VkDevice, VkQueryPool pool, const VkAllocationCallbacks*) {
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    if (!q) return;
    Release(q->heap);
    Release(q->readback);
    delete q;
}

XVK void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t) {}

XVK void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer buffer, VkQueryPool pool, uint32_t query, VkQueryControlFlags) {
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    reinterpret_cast<XCommandBuffer*>(buffer)->list->BeginQuery(q->heap, D3D12_QUERY_TYPE_OCCLUSION, query);
}

XVK void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer buffer, VkQueryPool pool, uint32_t query) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    cb->list->EndQuery(q->heap, D3D12_QUERY_TYPE_OCCLUSION, query);
    cb->list->ResolveQueryData(q->heap, D3D12_QUERY_TYPE_OCCLUSION, query, 1, q->readback, uint64_t(query) * 8);
}

XVK void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer buffer, VkPipelineStageFlagBits, VkQueryPool pool, uint32_t query) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    cb->list->EndQuery(q->heap, D3D12_QUERY_TYPE_TIMESTAMP, query);
    cb->list->ResolveQueryData(q->heap, D3D12_QUERY_TYPE_TIMESTAMP, query, 1, q->readback, uint64_t(query) * 8);
}

XVK void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer buffer, VkQueryPool pool, uint32_t first, uint32_t count, VkBuffer dst, VkDeviceSize offset, VkDeviceSize stride, VkQueryResultFlags flags) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    auto* b = Obj<XBuffer>(uint64_t(dst));
    if (!b || !b->memory || !EnsureWhole(b->memory)) return;
    if (!(flags & VK_QUERY_RESULT_64_BIT) || stride != 8 || (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)) Warn("query copy layout other than packed 64-bit is approximated");
    Use(cb->list, b->memory->whole, D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_QUERY_TYPE type = q->vk == VK_QUERY_TYPE_TIMESTAMP ? D3D12_QUERY_TYPE_TIMESTAMP : D3D12_QUERY_TYPE_OCCLUSION;
    for (uint32_t i = 0; i < count; ++i) cb->list->ResolveQueryData(q->heap, type, first + i, 1, b->memory->whole.res, b->offset + offset + i * stride);
}

XVK VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t first, uint32_t count, size_t, void* data, VkDeviceSize stride, VkQueryResultFlags flags) {
    auto* q = Obj<XQueryPool>(uint64_t(pool));
    if (flags & VK_QUERY_RESULT_WAIT_BIT) WaitIdle(reinterpret_cast<XDevice*>(device)->queue);
    for (uint32_t i = 0; i < count; ++i) {
        auto* out = static_cast<uint8_t*>(data) + i * stride;
        const uint64_t raw = q->mapped ? q->mapped[first + i] : 0;
        const uint64_t v = q->freq ? raw / q->freq * 1000000000ull + raw % q->freq * 1000000000ull / q->freq : raw;
        if (flags & VK_QUERY_RESULT_64_BIT) {
            std::memcpy(out, &v, 8);
            if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) { const uint64_t one = 1; std::memcpy(out + 8, &one, 8); }
        } else {
            const uint32_t v32 = uint32_t(v);
            std::memcpy(out, &v32, 4);
            if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) { const uint32_t one = 1; std::memcpy(out + 4, &one, 4); }
        }
    }
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* r) {
    vkGetImageMemoryRequirements(device, info->image, &r->memoryRequirements);
    for (auto* s = static_cast<VkBaseOutStructure*>(r->pNext); s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
            auto* d = reinterpret_cast<VkMemoryDedicatedRequirements*>(s);
            d->prefersDedicatedAllocation = d->requiresDedicatedAllocation = VK_FALSE;
        }
}

namespace {

void Layer2(std::map<std::string, PFN_vkVoidFunction>& t) {
#define F(name) t[#name] = reinterpret_cast<PFN_vkVoidFunction>(&name)
    F(vkCreateSampler); F(vkDestroySampler); F(vkCreateDescriptorSetLayout); F(vkDestroyDescriptorSetLayout); F(vkGetDescriptorSetLayoutSupport);
    F(vkCreateDescriptorPool); F(vkResetDescriptorPool); F(vkDestroyDescriptorPool); F(vkAllocateDescriptorSets); F(vkFreeDescriptorSets); F(vkUpdateDescriptorSets);
    F(vkCreatePipelineLayout); F(vkDestroyPipelineLayout); F(vkCreateShaderModule); F(vkDestroyShaderModule); F(vkCreatePipelineCache); F(vkDestroyPipelineCache);
    F(vkGetPipelineCacheData); F(vkMergePipelineCaches); F(vkCreateRenderPass); F(vkDestroyRenderPass); F(vkCreateFramebuffer); F(vkDestroyFramebuffer);
    F(vkCreateGraphicsPipelines); F(vkCreateComputePipelines); F(vkDestroyPipeline); F(vkCmdBindPipeline); F(vkCmdBindDescriptorSets); F(vkCmdPushConstants);
    F(vkCmdBindVertexBuffers); F(vkCmdBindIndexBuffer); F(vkCmdSetViewport); F(vkCmdSetScissor); F(vkCmdSetStencilReference); F(vkCmdSetBlendConstants);
    F(vkCmdSetDepthBias); F(vkCmdSetDepthBounds); F(vkGetImageSparseMemoryRequirements); F(vkGetPhysicalDeviceSparseImageFormatProperties); F(vkCmdSetStencilCompareMask); F(vkCmdSetStencilWriteMask); F(vkCmdBeginRenderPass); F(vkCmdEndRenderPass); F(vkCmdClearAttachments);
    F(vkCmdDraw); F(vkCmdDrawIndexed); F(vkCmdDrawMeshTasksEXT); F(vkCmdDrawMeshTasksIndirectEXT); F(vkCmdDispatch); F(vkCmdDrawIndirect); F(vkCmdDrawIndexedIndirect); F(vkCmdDispatchIndirect); F(vkCreateQueryPool);
    F(vkDestroyQueryPool); F(vkCmdResetQueryPool); F(vkCmdBeginQuery); F(vkCmdEndQuery); F(vkCmdWriteTimestamp); F(vkCmdCopyQueryPoolResults); F(vkGetQueryPoolResults);
    F(vkGetImageMemoryRequirements2);
#undef F
}

}
