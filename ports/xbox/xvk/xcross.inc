#include <future>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <d3dcompiler.h>
#include "spirv_hlsl.hpp"
#include "spirv_parser.hpp"

namespace xcross {

struct Result {
    bool ok = false;
    std::string error, hlsl, layout, gsHlsl, profile, vsHlsl, meshJson;
    std::vector<char> dxbc, gsDxbc, vsDxbc;
    double msSpv = 0, msCross = 0, msPasses = 0, msFxc = 0;
    int explicitLod = 0;
};

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline bool IsWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

inline std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) { out.push_back(s.substr(a)); return out; }
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
}

inline std::string Join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
    return out;
}

inline void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}

inline std::string Strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline size_t Word(const std::string& s, size_t p) {
    size_t q = p;
    while (q < s.size() && IsWord(s[q])) ++q;
    return q;
}

inline bool Eat(const std::string& s, size_t& p, const char* lit) {
    const size_t n = std::strlen(lit);
    if (s.compare(p, n, lit) != 0) return false;
    p += n;
    return true;
}

inline bool Digits(const std::string& s, size_t& p, std::string& out) {
    size_t q = p;
    while (q < s.size() && IsDigit(s[q])) ++q;
    if (q == p) return false;
    out = s.substr(p, q - p);
    p = q;
    return true;
}

struct Decl {
    std::string head, name, arr, reg, slot, space, tail;
    bool hasArr = false;
};

inline bool ParseDeclFrom(const std::string& l, size_t p, Decl& d, bool requireSemicolonOptional) {
    size_t q = Word(l, p);
    if (q == p) return false;
    p = q;
    if (p < l.size() && l[p] == '<') {
        size_t e = l.find('>', p);
        if (e == std::string::npos) return false;
        p = e + 1;
    }
    if (!Eat(l, p, " ")) return false;
    q = Word(l, p);
    if (q == p) return false;
    d.name = l.substr(p, q - p);
    p = q;
    d.hasArr = false;
    d.arr.clear();
    if (p < l.size() && l[p] == '[') {
        size_t r = p + 1;
        while (r < l.size() && IsDigit(l[r])) ++r;
        if (r < l.size() && l[r] == ']') {
            d.hasArr = true;
            d.arr = l.substr(p + 1, r - p - 1);
            p = r + 1;
        }
    }
    if (!Eat(l, p, " : register(")) return false;
    d.head = l.substr(0, p);
    if (p >= l.size() || std::string("tusb").find(l[p]) == std::string::npos) return false;
    d.reg = std::string(1, l[p++]);
    if (!Digits(l, p, d.slot)) return false;
    if (!Eat(l, p, ", space")) return false;
    if (!Digits(l, p, d.space)) return false;
    const size_t tailStart = p;
    if (!Eat(l, p, ")")) return false;
    Eat(l, p, ";");
    (void)requireSemicolonOptional;
    if (p != l.size()) return false;
    d.tail = l.substr(tailStart);
    return true;
}

inline bool ParseDecl(const std::string& l, Decl& d) {
    if (l.compare(0, 17, "globallycoherent ") == 0 && ParseDeclFrom(l, 17, d, true)) return true;
    return ParseDeclFrom(l, 0, d, true);
}

inline const char* Kind(const std::string& r) {
    return r == "t" ? "SRV" : r == "u" ? "UAV" : r == "b" ? "CBV" : "Sampler";
}

inline std::string Pack(const std::string& text, std::string& layout) {
    std::vector<std::string> lines = Split(text);
    struct Item { size_t line; Decl d; };
    std::vector<Item> decls;
    for (size_t i = 0; i < lines.size(); ++i) {
        Decl d;
        if (ParseDecl(lines[i], d) && std::stoul(d.space) != 11) decls.push_back({i, d});
    }
    std::stable_sort(decls.begin(), decls.end(), [](const Item& a, const Item& b) {
        if (a.d.reg != b.d.reg) return a.d.reg < b.d.reg;
        const unsigned long sa = std::stoul(a.d.space), sb = std::stoul(b.d.space);
        if (sa != sb) return sa < sb;
        return std::stoul(a.d.slot) < std::stoul(b.d.slot);
    });
    std::map<std::pair<std::string, unsigned long>, unsigned long> nxt;
    std::string js = "[";
    bool first = true;
    for (const Item& it : decls) {
        const auto key = std::make_pair(it.d.reg, std::stoul(it.d.space));
        const unsigned long count = !it.d.hasArr ? 1 : it.d.arr.empty() ? 0 : std::stoul(it.d.arr);
        const unsigned long reg = nxt[key];
        js += std::string(first ? "" : ", ") + "{\"name\": \"" + it.d.name + "\", \"kind\": \"" + Kind(it.d.reg) + "\", \"space\": " + std::to_string(key.second) +
              ", \"binding\": " + std::to_string(std::stoul(it.d.slot)) + ", \"count\": " + std::to_string(count) + ", \"register\": " + std::to_string(reg) + "}";
        first = false;
        const std::string& l = lines[it.line];
        lines[it.line] = it.d.head + it.d.reg + std::to_string(reg) + ", space" + std::to_string(key.second) + it.d.tail;
        (void)l;
        nxt[key] += count ? count : 1;
    }
    layout = js;
    return Join(lines, "\n");
}

inline std::string RootSig(const std::string& text, const std::string& profile) {
    std::map<std::pair<std::string, unsigned long>, std::vector<std::pair<unsigned long, unsigned long>>> spans;
    std::vector<std::string> unbounded;
    for (const std::string& l : Split(text)) {
        Decl d;
        if (!ParseDecl(l, d)) continue;
        if (d.tail != ");" && d.tail != ")") continue;
        const unsigned long slot = std::stoul(d.slot), space = std::stoul(d.space);
        if (space == 11) continue;
        if (d.hasArr && d.arr.empty()) {
            unbounded.push_back(std::string(Kind(d.reg)) + "(" + d.reg + std::to_string(slot) + ", space=" + std::to_string(space) + ", numDescriptors=unbounded)");
            continue;
        }
        const unsigned long count = d.hasArr ? std::stoul(d.arr) : 1;
        spans[{d.reg, space}].push_back({slot, slot + count});
    }
    std::vector<std::string> views, samplers;
    for (auto& kv : spans) {
        std::vector<std::pair<unsigned long, unsigned long>> items = kv.second, merged;
        std::sort(items.begin(), items.end());
        for (auto& it : items) {
            if (!merged.empty() && it.first <= merged.back().second) merged.back().second = (std::max)(merged.back().second, it.second);
            else merged.push_back(it);
        }
        for (auto& m : merged) {
            const std::string e = std::string(Kind(kv.first.first)) + "(" + kv.first.first + std::to_string(m.first) + ", space=" + std::to_string(kv.first.second) +
                                  ", numDescriptors=" + std::to_string(m.second - m.first) + ")";
            (kv.first.first == "s" ? samplers : views).push_back(e);
        }
    }
    std::vector<std::string> parts{std::string("RootFlags(") + (profile.compare(0, 2, "vs") == 0 ? "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT" : "0") + ")"};
    if (!views.empty()) parts.push_back("DescriptorTable(" + Join(views, ", ") + ")");
    if (!samplers.empty()) parts.push_back("DescriptorTable(" + Join(samplers, ", ") + ")");
    for (auto& u : unbounded) parts.push_back("DescriptorTable(" + u + ")");
    if (text.find("register(u0, space11)") != std::string::npos) {
        parts.push_back("RootConstants(num32BitConstants=4, b0, space=11)");
        parts.push_back("UAV(u0, space=11)");
    }
    return Join(parts, ", ");
}

inline bool ParseField(const std::string& line, std::string& quals, std::string& kind, std::string& name, std::string& semantic) {
    static const char* Q[] = {"nointerpolation", "noperspective", "linear", "centroid", "sample"};
    size_t p = 0;
    while (p < line.size() && IsSpace(line[p])) ++p;
    const size_t qs = p;
    std::vector<size_t> stops{p};
    size_t c = p;
    for (;;) {
        bool hit = false;
        for (const char* q : Q) {
            const size_t n = std::strlen(q);
            if (line.compare(c, n, q) == 0 && c + n < line.size() && IsSpace(line[c + n])) {
                size_t e = c + n;
                while (e < line.size() && IsSpace(line[e])) ++e;
                c = e;
                stops.push_back(c);
                hit = true;
                break;
            }
        }
        if (!hit) break;
    }
    for (size_t k = stops.size(); k-- > 0;) {
        size_t r = stops[k];
        size_t e = Word(line, r);
        if (e == r) continue;
        kind = line.substr(r, e - r);
        r = e;
        if (r >= line.size() || !IsSpace(line[r])) continue;
        while (r < line.size() && IsSpace(line[r])) ++r;
        e = Word(line, r);
        if (e == r) continue;
        name = line.substr(r, e - r);
        r = e;
        while (r < line.size() && IsSpace(line[r])) ++r;
        if (r >= line.size() || line[r] != ':') continue;
        ++r;
        while (r < line.size() && IsSpace(line[r])) ++r;
        e = Word(line, r);
        if (e == r) continue;
        semantic = line.substr(r, e - r);
        r = e;
        if (r >= line.size() || line[r] != ';') continue;
        ++r;
        while (r < line.size() && IsSpace(line[r])) ++r;
        if (r != line.size()) continue;
        quals = line.substr(qs, stops[k] - qs);
        return true;
    }
    return false;
}

inline bool BaryLower(const std::string& text, std::string& outText, std::string& gsText) {
    const std::string head = "struct SPIRV_Cross_Input\n{\n";
    const size_t h = text.find(head);
    if (h == std::string::npos) return false;
    const size_t b0 = h + head.size();
    const size_t b1 = text.find("\n};", b0);
    if (b1 == std::string::npos) return false;
    const std::string body = text.substr(b0, b1 - b0);
    if (body.find("SV_Barycentrics") == std::string::npos) return false;
    std::set<std::string> vertex;
    const std::string gav = "GetAttributeAtVertex(stage_input.";
    for (size_t p = text.find(gav); p != std::string::npos; p = text.find(gav, p + 1)) {
        const size_t a = p + gav.size(), e = Word(text, a);
        if (e > a && e < text.size() && text[e] == ',') vertex.insert(text.substr(a, e - a));
    }
    std::vector<std::string> ps, gsIn, gsBody;
    std::string position;
    bool havePosition = false;
    for (const std::string& line : Split(body)) {
        std::string quals, kind, name, semantic;
        if (!ParseField(line, quals, kind, name, semantic)) throw Fail("bary: input field not understood: " + Strip(line));
        if (semantic == "SV_Barycentrics") {
            const std::string mode = name.find("NoPersp") != std::string::npos ? "noperspective " : "";
            ps.push_back("    " + mode + kind + " " + name + " : BARYCENTRIC" + (mode.empty() ? "0" : "1") + ";");
            gsBody.push_back("        o." + name + " = float3(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);");
        } else if (vertex.count(name)) {
            size_t d = semantic.size();
            while (d > 0 && IsDigit(semantic[d - 1])) --d;
            const std::string base = semantic.substr(0, d), index = d < semantic.size() ? semantic.substr(d) : "0";
            gsIn.push_back("    " + kind + " " + name + " : " + semantic + ";");
            for (int v = 0; v < 3; ++v) {
                ps.push_back("    nointerpolation " + kind + " " + name + "_v" + std::to_string(v) + " : V" + std::to_string(v) + base + index + ";");
                gsBody.push_back("        o." + name + "_v" + std::to_string(v) + " = v[" + std::to_string(v) + "]." + name + ";");
            }
        } else if (semantic == "SV_Position") {
            position = name;
            havePosition = true;
            ps.push_back(line);
        } else if (semantic.compare(0, 3, "SV_") == 0) {
            ps.push_back(line);
        } else {
            gsIn.push_back("    " + quals + kind + " " + name + " : " + semantic + ";");
            ps.push_back(line);
            gsBody.push_back("        o." + name + " = v[i]." + name + ";");
        }
    }
    std::string out = text.substr(0, b0) + Join(ps, "\n") + text.substr(b1);
    std::string res;
    size_t last = 0;
    const std::string gav2 = "GetAttributeAtVertex(stage_input.";
    for (size_t p = out.find(gav2); p != std::string::npos; p = out.find(gav2, p + 1)) {
        const size_t a = p + gav2.size(), e = Word(out, a);
        if (e == a || out.compare(e, 2, ", ") != 0 || e + 3 >= out.size() || !IsDigit(out[e + 2]) || out[e + 3] != ')') continue;
        res += out.substr(last, p - last) + "stage_input." + out.substr(a, e - a) + "_v" + out[e + 2];
        last = e + 4;
        p = last - 1;
    }
    res += out.substr(last);
    outText = res;
    std::vector<std::string> gsOut;
    for (const std::string& l : ps) {
        bool drop = false;
        for (const char* sv : {"SV_IsFrontFace", "SV_SampleIndex", "SV_Coverage", "SV_PrimitiveID"}) {
            for (size_t c = l.find(':'); c != std::string::npos && !drop; c = l.find(':', c + 1)) {
                size_t r = c + 1;
                while (r < l.size() && IsSpace(l[r])) ++r;
                if (l.compare(r, std::strlen(sv), sv) != 0) continue;
                r += std::strlen(sv);
                while (r < l.size() && IsSpace(l[r])) ++r;
                if (r < l.size() && l[r] == ';') drop = true;
            }
        }
        if (!drop) gsOut.push_back(l);
    }
    if (!havePosition) {
        gsOut.insert(gsOut.end(), "    float4 gs_position : SV_Position;");
        position = "gs_position";
    }
    std::vector<std::string> gs{"struct GsIn", "{", "    float4 gs_position : SV_Position;"};
    gs.insert(gs.end(), gsIn.begin(), gsIn.end());
    for (const char* l : {"};", "", "struct GsOut", "{"}) gs.push_back(l);
    gs.insert(gs.end(), gsOut.begin(), gsOut.end());
    for (const char* l : {"};", "", "[maxvertexcount(3)]", "void main(triangle GsIn v[3], inout TriangleStream<GsOut> stream)", "{", "    [unroll] for (uint i = 0; i < 3; i++)", "    {", "        GsOut o;"}) gs.push_back(l);
    gs.push_back("        o." + position + " = v[i].gs_position;");
    gs.insert(gs.end(), gsBody.begin(), gsBody.end());
    for (const char* l : {"        stream.Append(o);", "    }", "}", ""}) gs.push_back(l);
    gsText = Join(gs, "\n");
    return true;
}

static const char* kIntCommon =
    "ByteAddressBuffer spvSamplerInfo : register(t0, space9);\n"
    "int spvAddress(int c, int size, uint mode, inout bool border)\n"
    "{\n"
    "    if (mode == 0u) return ((c % size) + size) % size;\n"
    "    if (mode == 1u) { int p = ((c % (2 * size)) + 2 * size) % (2 * size); return p < size ? p : 2 * size - 1 - p; }\n"
    "    if (mode == 4u) { int m = c < 0 ? -c - 1 : c; return min(m, size - 1); }\n"
    "    if (mode == 3u && (c < 0 || c >= size)) border = true;\n"
    "    return clamp(c, 0, size - 1);\n"
    "}\n"
    "uint spvMip(uint4 s, float lod, uint levels)\n"
    "{\n"
    "    float l = clamp(lod + asfloat(s.w), asfloat(s.y), asfloat(s.z));\n"
    "    return l <= 0.0f ? 0u : min((uint)(ceil(l + 0.5f) - 1.0f), levels - 1u);\n"
    "}\n"
    "float spvImplicitLod(float3 dx, float3 dy)\n"
    "{\n"
    "    return log2(max(max(length(dx), length(dy)), 1e-20f));\n"
    "}\n";

inline std::string Fmt(const std::string& f, const std::string& a) {
    std::string out = f;
    const size_t p = out.find("%s");
    if (p != std::string::npos) out.replace(p, 2, a);
    return out;
}

inline std::pair<std::string, std::string> IntHelper(const std::string& tex, const std::string& elem, const std::string& method) {
    std::string coord, dims0, dims, load;
    int axes;
    if (tex == "Texture1D") {
        coord = "float"; axes = 1;
        dims0 = "uint w, levels; t.GetDimensions(0u, w, levels); float3 size0 = float3(w, 1.0f, 1.0f);";
        dims = "uint mw, ml; t.GetDimensions(mip, mw, ml); float3 size = float3(mw, 1.0f, 1.0f);";
        load = "t.Load(int2(x, mip))";
    } else if (tex == "Texture2D") {
        coord = "float2"; axes = 2;
        dims0 = "uint w, h, levels; t.GetDimensions(0u, w, h, levels); float3 size0 = float3(w, h, 1.0f);";
        dims = "uint mw, mh, ml; t.GetDimensions(mip, mw, mh, ml); float3 size = float3(mw, mh, 1.0f);";
        load = "t.Load(int3(x, y, mip))";
    } else if (tex == "Texture2DArray") {
        coord = "float3"; axes = 2;
        dims0 = "uint w, h, layers, levels; t.GetDimensions(0u, w, h, layers, levels); float3 size0 = float3(w, h, 1.0f);";
        dims = "uint mw, mh, mlayers, ml; t.GetDimensions(mip, mw, mh, mlayers, ml); float3 size = float3(mw, mh, 1.0f);";
        load = "t.Load(int4(x, y, clamp((int)floor(uv.z + 0.5f), 0, (int)mlayers - 1), mip))";
    } else {
        coord = "float3"; axes = 3;
        dims0 = "uint w, h, d, levels; t.GetDimensions(0u, w, h, d, levels); float3 size0 = float3(w, h, d);";
        dims = "uint mw, mh, md, ml; t.GetDimensions(mip, mw, mh, md, ml); float3 size = float3(mw, mh, md);";
        load = "t.Load(int4(x, y, z, mip))";
    }
    const std::string name = "spv" + method + "_" + tex + "_" + elem;
    const std::string pad = axes == 1 ? "float3(%s, 0.0f, 0.0f)" : axes == 2 ? "float3(%s, 0.0f)" : "%s";
    std::string pick = axes == 1 ? "uv" : axes == 2 ? "uv.xy" : "uv.xyz";
    if (tex == "Texture1D") pick = "uv";
    std::string extra, lod;
    if (method == "SampleLevel") { extra = ", float lod"; lod = "lod"; }
    else if (method == "SampleBias") { extra = ", float bias"; lod = "spvImplicitLod(ddx(" + Fmt(pad, pick) + ") * size0, ddy(" + Fmt(pad, pick) + ") * size0) + bias"; }
    else if (method == "SampleGrad") {
        const std::string g = axes == 1 ? "float" : "float" + std::to_string(axes);
        extra = ", " + g + " gx, " + g + " gy";
        lod = "spvImplicitLod(" + Fmt(pad, "gx") + " * size0, " + Fmt(pad, "gy") + " * size0)";
    } else { lod = "spvImplicitLod(ddx(" + Fmt(pad, pick) + ") * size0, ddy(" + Fmt(pad, pick) + ") * size0)"; }
    std::string e = elem;
    while (!e.empty() && e.back() == '4') e.pop_back();
    const std::string border = e + "4";
    std::vector<std::string> lines{
        elem + " " + name + "(" + tex + "<" + elem + "> t, uint slot, " + coord + " uv" + extra + ")",
        "{",
        "    uint4 s = spvSamplerInfo.Load4(slot * 16u);",
        "    " + dims0,
        "    uint mip = spvMip(s, " + lod + ", levels);",
        "    " + dims,
        "    float3 tc = ((s.x >> 24) & 1u) != 0u ? " + Fmt(pad, pick) + " : " + Fmt(pad, pick) + " * size;",
        "    bool border = false;",
        "    int x = spvAddress((int)floor(tc.x), (int)size.x, s.x & 0xFu, border);",
        "    int y = spvAddress((int)floor(tc.y), (int)size.y, (s.x >> 4) & 0xFu, border);",
        "    int z = spvAddress((int)floor(tc.z), (int)size.z, (s.x >> 8) & 0xFu, border);",
        "    uint color = (s.x >> 16) & 0xFu;",
        "    if (border) return (" + elem + ")(color >= 4u ? " + border + "(1, 1, 1, 1) : " + border + "(0, 0, 0, color >= 2u ? 1 : 0));",
        "    return " + load + ";",
        "}",
    };
    return {name, Join(lines, "\n")};
}

inline std::vector<std::string> ArgsUntilClose(const std::string& text, size_t start, size_t& end) {
    int depth = 1;
    size_t cur = start;
    std::vector<std::string> parts;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') {
            if (--depth == 0) {
                parts.push_back(Strip(text.substr(cur, i - cur)));
                end = i + 1;
                return parts;
            }
        } else if (c == ',' && depth == 1) {
            parts.push_back(Strip(text.substr(cur, i - cur)));
            cur = i + 1;
        }
    }
    throw Fail("intsample: unbalanced call");
}

inline std::string IntSample(const std::string& text, std::string& info) {
    std::map<std::string, std::pair<std::string, std::string>> textures;
    std::vector<std::pair<std::string, unsigned long>> slots;
    unsigned long total = 0;
    for (const std::string& l : Split(text)) {
        for (const char* t : {"Texture1D", "Texture2DArray", "Texture2D", "Texture3D"}) {
            size_t p = 0;
            if (!Eat(l, p, t) || !Eat(l, p, "<")) continue;
            std::string elem;
            for (const char* e : {"uint4", "uint", "int4", "int"}) { size_t q = p; if (Eat(l, q, e) && q < l.size() && l[q] == '>') { elem = e; p = q; break; } }
            if (elem.empty() || !Eat(l, p, "> ")) break;
            const size_t ne = Word(l, p);
            if (ne == p) break;
            const std::string name = l.substr(p, ne - p);
            p = ne;
            if (p < l.size() && l[p] == '[') { size_t r = p + 1; std::string dg; if (Digits(l, r, dg) && r < l.size() && l[r] == ']') p = r + 1; }
            if (Eat(l, p, " : register")) textures[name] = {t, elem};
            break;
        }
        size_t p = 0;
        if (Eat(l, p, "SamplerState ") || Eat(l, p, "SamplerComparisonState ")) {
            const size_t ne = Word(l, p);
            if (ne == p) continue;
            const std::string name = l.substr(p, ne - p);
            p = ne;
            unsigned long count = 1;
            if (p < l.size() && l[p] == '[') { size_t r = p + 1; std::string dg; if (Digits(l, r, dg) && r < l.size() && l[r] == ']') { p = r + 1; count = std::stoul(dg); } }
            if (!Eat(l, p, " : register")) continue;
            auto it = std::find_if(slots.begin(), slots.end(), [&](auto& s) { return s.first == name; });
            if (it == slots.end()) slots.push_back({name, total}); else it->second = total;
            total += count;
        }
    }
    if (textures.empty()) return text;
    static const std::regex call(R"(\b(\w+)(\[[^\[\]]*\])?\.(Sample|SampleLevel|SampleGrad|SampleBias)\()");
    std::vector<std::pair<std::string, std::string>> helpers;
    std::string out;
    size_t last = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        const size_t ms = size_t(m.position(0));
        if (!textures.count(m.str(1)) || ms < last) continue;
        const auto& te = textures[m.str(1)];
        if (te.second.empty() || te.second.back() != '4') throw Fail("intsample: integer texture element type " + te.second + " is not handled");
        size_t end = 0;
        std::vector<std::string> args = ArgsUntilClose(text, ms + size_t(m.length(0)), end);
        const std::string& a0 = args[0];
        const size_t we = Word(a0, 0);
        std::string sname, sidx;
        bool hasIdx = false;
        if (we == 0) throw Fail("intsample: sampler expression not understood: " + a0);
        sname = a0.substr(0, we);
        if (we != a0.size()) {
            if (a0[we] != '[' || a0.back() != ']') throw Fail("intsample: sampler expression not understood: " + a0);
            sidx = a0.substr(we + 1, a0.size() - we - 2);
            hasIdx = true;
        }
        auto si = std::find_if(slots.begin(), slots.end(), [&](auto& s) { return s.first == sname; });
        if (si == slots.end()) throw Fail("intsample: sampler expression not understood: " + a0);
        const std::string method = m.str(3);
        const size_t maxArgs = method == "Sample" ? 2 : method == "SampleGrad" ? 4 : 3;
        if (args.size() > maxArgs) throw Fail("intsample: integer " + method + " with offset or clamp is not handled");
        const std::string slot = hasIdx ? std::to_string(si->second) + "u + (uint)(" + sidx + ")" : std::to_string(si->second) + "u";
        auto h = IntHelper(te.first, te.second, method);
        auto hi = std::find_if(helpers.begin(), helpers.end(), [&](auto& x) { return x.first == h.first; });
        if (hi == helpers.end()) helpers.push_back(h); else hi->second = h.second;
        out += text.substr(last, ms - last);
        std::vector<std::string> rest(args.begin() + 1, args.end());
        out += h.first + "(" + m.str(1) + m.str(2) + ", " + slot + ", " + Join(rest, ", ") + ")";
        last = end;
    }
    if (helpers.empty()) return text;
    out += text.substr(last);
    info = "[";
    for (size_t i = 0; i < slots.size(); ++i) info += std::string(i ? ", " : "") + "{\"name\": \"" + slots[i].first + "\", \"info_slot\": " + std::to_string(slots[i].second) + "}";
    info += "]";
    std::vector<std::string> hs;
    for (auto& x : helpers) hs.push_back(x.second);
    return std::string(kIntCommon) + Join(hs, "\n") + "\n" + out;
}

inline std::string CrossHlsl(const std::vector<uint32_t>& words, bool flip, bool fixupClip, uint32_t model, spv::ExecutionModel& stage) {
    std::vector<uint32_t> copy = words;
    spirv_cross::Parser parser(std::move(copy));
    parser.parse();
    spirv_cross::CompilerHLSL c(std::move(parser.get_parsed_ir()));
    stage = c.get_execution_model();
    if (!c.get_common_options().version) throw Fail("spirv-cross: Didn't specify GLSL version and SPIR-V did not specify language.");
    auto opts = c.get_common_options();
    opts.vertex.flip_vert_y = flip;
    opts.vertex.fixup_clipspace = fixupClip;
    c.set_common_options(opts);
    auto h = c.get_hlsl_options();
    h.shader_model = model;
    c.set_hlsl_options(h);
    c.set_resource_binding_flags(0);
    c.get_shader_resources();
    c.remap_num_workgroups_builtin();
    return c.compile();
}

inline bool Compile(const std::string& src, const char* profile, UINT flags, std::vector<char>& out, std::string& err) {
    ID3DBlob *code = nullptr, *msg = nullptr;
    const HRESULT hr = D3DCompile(src.data(), src.size(), "shader", nullptr, nullptr, "main", profile, flags, 0, &code, &msg);
    err = msg ? std::string(static_cast<const char*>(msg->GetBufferPointer()), msg->GetBufferSize()) : "";
    if (msg) msg->Release();
    if (FAILED(hr) || !code) { if (code) code->Release(); return false; }
    out.assign(static_cast<const char*>(code->GetBufferPointer()), static_cast<const char*>(code->GetBufferPointer()) + code->GetBufferSize());
    code->Release();
    return true;
}

inline size_t CloseParen(const std::string& s, size_t open, std::vector<size_t>& commas) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch == '(' || ch == '[' || ch == '{') ++depth;
        else if (ch == ')' || ch == ']' || ch == '}') { if (--depth == 0) return i; }
        else if (ch == ',' && depth == 1) commas.push_back(i);
    }
    return std::string::npos;
}

inline bool IdentChar(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }

inline std::string ExplicitLod(const std::string& src, int& changed) {
    struct Rule { const char* name; int mode; };
    static const Rule rules[] = {{".SampleBias(", 1}, {".SampleCmp(", 2}, {".Sample(", 0}, {".CalculateLevelOfDetail(", 3}, {"ddx_coarse(", 4}, {"ddy_coarse(", 4}, {"ddx_fine(", 4}, {"ddy_fine(", 4}, {"ddx(", 4}, {"ddy(", 4}, {"fwidth(", 4}};
    std::string s = src;
    for (const Rule& rule : rules) {
        const std::string name = rule.name;
        size_t at = 0;
        while ((at = s.find(name, at)) != std::string::npos) {
            if (rule.mode == 4 && at > 0 && (IdentChar(s[at - 1]) || s[at - 1] == '.')) { at += name.size(); continue; }
            const size_t open = at + name.size() - 1;
            std::vector<size_t> commas;
            const size_t close = CloseParen(s, open, commas);
            if (close == std::string::npos) break;
            std::string args = s.substr(open + 1, close - open - 1);
            std::vector<std::string> parts;
            size_t from = open + 1;
            for (size_t c : commas) { parts.push_back(s.substr(from, c - from)); from = c + 1; }
            parts.push_back(s.substr(from, close - from));
            std::string repl;
            if (rule.mode == 0 && parts.size() >= 2) {
                repl = ".SampleLevel(" + parts[0] + "," + parts[1] + ", 0.0";
                for (size_t i = 2; i < parts.size(); ++i) repl += "," + parts[i];
                repl += ")";
            } else if (rule.mode == 1 && parts.size() >= 3) {
                repl = ".SampleLevel(" + parts[0] + "," + parts[1] + ", max(0.0," + parts[2] + ")";
                for (size_t i = 3; i < parts.size(); ++i) repl += "," + parts[i];
                repl += ")";
            } else if (rule.mode == 2 && parts.size() >= 3) {
                repl = ".SampleCmpLevelZero(" + args + ")";
            } else if (rule.mode == 3) {
                size_t start = at;
                while (start > 0 && (IdentChar(s[start - 1]) || s[start - 1] == ']' || s[start - 1] == '[')) --start;
                s.replace(start, close + 1 - start, "0.0");
                at = start + 3;
                ++changed;
                continue;
            } else if (rule.mode == 4) {
                repl = "((" + args + ") * 0.0)";
            } else {
                at = close;
                continue;
            }
            s.replace(at, close + 1 - at, repl);
            at += repl.size();
            ++changed;
        }
    }
    return s;
}

inline std::string FirstErrorLine(const std::string& msg) {
    for (const std::string& l : Split(msg)) {
        const size_t p = l.find("error");
        if (p != std::string::npos) return l.substr(p);
    }
    return "?";
}

}

#include "xlower.inc"

namespace xcross {

inline double MsSince(LARGE_INTEGER& t) {
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    const double ms = double(now.QuadPart - t.QuadPart) * 1000.0 / double(f.QuadPart);
    t = now;
    return ms;
}

inline Result Convert(const std::vector<uint32_t>& words, bool flip, bool fixupClip) {
    Result r;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    try {
        std::string spvasm, aerr;
        if (!Disassemble(words, spvasm, aerr)) throw Fail("spirv-dis: " + aerr);
        std::vector<uint32_t> cur = words;
        MeshInfo mi;
        const bool isMesh = spvasm.find("OpEntryPoint MeshEXT") != std::string::npos;
        if (isMesh) {
            spvasm = Mesh2cs(spvasm, mi);
            cur.clear();
            if (!Assemble(spvasm, cur, aerr)) throw Fail("mesh2cs as: " + aerr);
        }
        {
            std::string low;
            if (Lower64(spvasm).Run(low)) {
                std::vector<uint32_t> lw;
                if (!Assemble(low, lw, aerr)) throw Fail("lower64 as: " + aerr);
                cur = lw;
            }
        }
        r.msSpv = MsSince(t);
        spv::ExecutionModel stage = spv::ExecutionModelGLCompute;
        std::string text, gs;
        bool haveGs = false;
        try {
            text = CrossHlsl(cur, flip, fixupClip, 51, stage);
        } catch (const spirv_cross::CompilerError& e) {
            const std::string m = e.what();
            if (m.find("barycentrics") == std::string::npos && m.find("non-float textures") == std::string::npos) throw Fail(std::string("spirv-cross: ") + m);
            try {
                text = CrossHlsl(cur, flip, fixupClip, 67, stage);
            } catch (const spirv_cross::CompilerError& e2) {
                throw Fail(std::string("spirv-cross: ") + e2.what());
            }
            std::string lowered;
            if (BaryLower(text, lowered, gs)) { text = lowered; haveGs = true; }
        }
        r.msCross = MsSince(t);
        if (haveGs) {
            std::string err;
            if (!Compile(gs, "gs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, r.gsDxbc, err)) throw Fail("gs: " + FirstErrorLine(err));
            r.gsHlsl = gs;
        }
        for (const char* n : {"", "2", "3", "4"}) {
            ReplaceAll(text, std::string(".Load<uint") + n + ">(", std::string(".Load") + n + "(");
            ReplaceAll(text, std::string(".Store<uint") + n + ">(", std::string(".Store") + n + "(");
        }
        {
            std::vector<std::string> lines = Split(text);
            for (std::string& l : lines) {
                size_t p = 0;
                Eat(l, p, "globallycoherent ");
                if (!Eat(l, p, "RWByteAddressBuffer ")) continue;
                const size_t ne = Word(l, p);
                if (ne == p) continue;
                const std::string name = l.substr(p, ne - p);
                size_t q = ne;
                std::string dg;
                if (!Eat(l, q, "[] : register(u") || !Digits(l, q, dg) || !Eat(l, q, ", space10);") || q != l.size()) continue;
                bool written = false;
                for (size_t s = text.find(name + "["); s != std::string::npos && !written; s = text.find(name + "[", s + 1)) {
                    if (s > 0 && IsWord(text[s - 1])) continue;
                    for (size_t j = s + name.size() + 1; j < text.size() && text[j] != ';'; ++j) {
                        if (text[j] == ']' && j + 1 < text.size() && text[j + 1] == '.' && (text.compare(j + 2, 5, "Store") == 0 || text.compare(j + 2, 11, "Interlocked") == 0)) { written = true; break; }
                    }
                }
                if (!written) l = "ByteAddressBuffer " + name + "[] : register(t0, space10);";
            }
            text = Join(lines, "\n");
        }
        if (isMesh) {
            std::string vs, err;
            text = MeshEpilogue(text, mi, flip, vs);
            const size_t m = vs.find("VSOut main(");
            if (m != std::string::npos) vs.insert(m, "[RootSignature(\"RootFlags(0), SRV(t0, space=11)\")]\n");
            if (!Compile(vs, "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, r.vsDxbc, err)) throw Fail("mesh vs: " + FirstErrorLine(err));
            r.vsHlsl = vs;
            r.meshJson = MeshJson(mi);
        }
        std::vector<std::string> pre;
        if (text.find("uaddCarry(") != std::string::npos) pre.push_back("uint uaddCarry(uint a, uint b, out uint carry) { uint r = a + b; carry = r < a ? 1u : 0u; return r; }");
        if (text.find("usubBorrow(") != std::string::npos) pre.push_back("uint usubBorrow(uint a, uint b, out uint borrow) { borrow = a < b ? 1u : 0u; return a - b; }");
        if (text.find("umulExtended(") != std::string::npos || text.find("imulExtended(") != std::string::npos) {
            pre.push_back("void umulExtended(uint a, uint b, out uint hi, out uint lo) { uint al = a & 0xFFFFu, ah = a >> 16, bl = b & 0xFFFFu, bh = b >> 16; uint ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh; uint mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu); lo = a * b; hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16); }");
            pre.push_back("void imulExtended(int a, int b, out int hi, out int lo) { uint h, l; umulExtended(asuint(a), asuint(b), h, l); h -= (a < 0 ? asuint(b) : 0u) + (b < 0 ? asuint(a) : 0u); hi = asint(h); lo = asint(l); }");
        }
        {
            std::vector<std::string> lines = Split(text);
            std::map<std::string, std::string> types;
            for (const std::string& l : lines) {
                size_t p = 0;
                while (p < l.size() && IsSpace(l[p])) ++p;
                const size_t te = Word(l, p);
                if (te == p || te >= l.size() || l[te] != ' ' || te + 1 >= l.size() || l[te + 1] != '_') continue;
                size_t v = te + 2;
                std::string dg;
                if (!Digits(l, v, dg) || v + 1 != l.size() || l[v] != ';') continue;
                types[l.substr(te + 1, v - te - 1)] = l.substr(p, te - p);
            }
            for (std::string& l : lines) {
                size_t p = 0;
                while (p < l.size() && IsSpace(l[p])) ++p;
                if (p >= l.size() || l[p] != '_') continue;
                size_t v = p + 1;
                std::string dg;
                if (!Digits(l, v, dg)) continue;
                const std::string var = l.substr(p, v - p);
                size_t q = v;
                if (!Eat(l, q, " = { ")) continue;
                if (l.size() < q + 3 || l.compare(l.size() - 3, 3, " };") != 0 || l.size() - 3 < q) continue;
                const std::string content = l.substr(q, l.size() - 3 - q);
                auto it = types.find(var);
                if (it == types.end()) continue;
                l = l.substr(0, p) + "{ " + it->second + " " + var + "_init = { " + content + " }; " + var + " = " + var + "_init; }";
            }
            text = Join(lines, "\n");
        }
        if (text.find("clockRealtime2x32EXT(") != std::string::npos || text.find("clock2x32ARB(") != std::string::npos) {
            pre.push_back("static uint g_clock = 0u;");
            pre.push_back("uint2 clockRealtime2x32EXT() { g_clock += 1024u; return uint2(g_clock, 0u); }");
            pre.push_back("uint2 clock2x32ARB() { return clockRealtime2x32EXT(); }");
        }
        {
            std::vector<std::string> found;
            for (const std::string& l : Split(text)) {
                size_t p = 0;
                if (!Eat(l, p, "SamplerComparisonState ")) continue;
                const size_t ne = Word(l, p);
                if (ne == p) continue;
                size_t q = ne;
                std::string cnt, s, sp;
                if (!Eat(l, q, "[") || !Digits(l, q, cnt) || !Eat(l, q, "] : register(s") || !Digits(l, q, s) || !Eat(l, q, ", space") || !Digits(l, q, sp) || !Eat(l, q, ");")) continue;
                found.push_back(l.substr(0, q));
                found.push_back(l.substr(p, ne - p));
                found.push_back(cnt);
                found.push_back(s);
                found.push_back(sp);
            }
            for (size_t i = 0; i + 4 < found.size(); i += 5) {
                const std::string &g0 = found[i], &name = found[i + 1];
                bool used = false;
                for (const char* m : {"Sample", "SampleLevel", "SampleGrad", "SampleBias"}) used = used || text.find(std::string(".") + m + "(" + name + "[") != std::string::npos;
                if (!used) continue;
                ReplaceAll(text, g0, g0 + "\nSamplerState " + name + "_plain[" + found[i + 2] + "] : register(s" + found[i + 3] + ", space" + std::to_string(std::stoul(found[i + 4]) + 1) + ");");
                for (const char* m : {"Sample", "SampleLevel", "SampleGrad", "SampleBias", "Gather"}) ReplaceAll(text, std::string(".") + m + "(" + name + "[", std::string(".") + m + "(" + name + "_plain[");
            }
        }
        std::string info;
        text = IntSample(text, info);
        if (text.find("IsHelperLane()") != std::string::npos) pre.push_back("bool IsHelperLane() { return false; }");
        std::string layout;
        text = Pack(text, layout);
        if (!info.empty()) layout += std::string(layout.size() > 1 ? ", " : "") + "{\"sampler_info\": " + info + "}";
        layout += "]";
        r.layout = layout;
        r.profile = stage == spv::ExecutionModelVertex ? "vs_5_1" : stage == spv::ExecutionModelFragment ? "ps_5_1" : stage == spv::ExecutionModelGeometry ? "gs_5_1" : "cs_5_1";
        const std::string sig = RootSig(text, r.profile);
        {
            std::vector<std::string> lines = Split(text);
            for (std::string& l : lines) {
                const size_t we = Word(l, 0);
                if (we == 0 || l.compare(we, 6, " main(") != 0) continue;
                l = "[RootSignature(\"" + sig + "\")]\n" + l;
                break;
            }
            text = Join(lines, "\n");
        }
        r.hlsl = Join(pre, "\n") + (pre.empty() ? "" : "\n") + text;
        r.msPasses = MsSince(t);
        std::string err;
        bool ok = false;
        const UINT base = D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
        auto full = std::async(std::launch::async, [&r, base] {
            std::pair<bool, std::pair<std::vector<char>, std::string>> o;
            o.first = Compile(r.hlsl, r.profile.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3 | base, o.second.first, o.second.second);
            return o;
        });
        std::vector<char> plain;
        std::string plainErr;
        bool plainOk = false, plainRan = false;
        if (full.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            plainOk = Compile(r.hlsl, r.profile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION | base, plain, plainErr);
            plainRan = true;
        }
        auto o = full.get();
        ok = o.first;
        r.dxbc = std::move(o.second.first);
        err = std::move(o.second.second);
        if (!ok && err.find("internal error") != std::string::npos) {
            if (!plainRan) plainOk = Compile(r.hlsl, r.profile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION | base, plain, plainErr);
            ok = plainOk;
            r.dxbc = std::move(plain);
            err = std::move(plainErr);
        }
        if (!ok && err.find("X3662") != std::string::npos) {
            int changed = 0;
            const std::string lod = ExplicitLod(r.hlsl, changed);
            std::vector<char> retry;
            std::string retryErr;
            if (changed && Compile(lod, r.profile.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3 | base, retry, retryErr)) {
                ok = true;
                r.dxbc = std::move(retry);
                r.hlsl = lod;
                r.explicitLod = changed;
            } else if (changed) {
                err = retryErr;
            }
        }
        r.msFxc = MsSince(t);
        if (!ok) throw Fail(FirstErrorLine(err));
        r.ok = true;
    } catch (const Fail& e) {
        r.error = e.what();
    } catch (const std::exception& e) {
        r.error = std::string("spirv-cross: ") + e.what();
    }
    return r;
}

}
