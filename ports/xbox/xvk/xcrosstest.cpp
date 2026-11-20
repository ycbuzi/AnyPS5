#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include "xcross.inc"

static bool Slurp(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static std::string Lf(std::string s) {
    xcross::ReplaceAll(s, "\r\n", "\n");
    return s;
}

static void Dump(const std::string& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}

static uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

struct Stats { int total = 0, match = 0, skip = 0, diff = 0, bothFail = 0; };

static void Check(const std::string& out, const std::string& name, const std::string& spvPath, const std::string& base, int fixupMode, Stats& st) {
    std::string spv;
    if (!Slurp(spvPath, spv) || spv.size() % 4) return;
    std::vector<uint32_t> words(spv.size() / 4);
    std::memcpy(words.data(), spv.data(), spv.size());
    char hex[32];
    std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)Fnv(words.data(), spv.size()));
    const bool fixup = fixupMode < 0 ? name != hex : fixupMode > 0;
    ++st.total;
    const xcross::Result r = xcross::Convert(words, true, fixup);
    std::string pyHlsl, pyLayout, pyDxbc, pyGs, pyVs, pyMesh;
    Slurp(base + ".hlsl", pyHlsl);
    Slurp(base + ".layout.json", pyLayout);
    const bool pyOk = Slurp(base + ".dxbc", pyDxbc);
    Slurp(base + ".gs.hlsl", pyGs);
    Slurp(base + ".vs.hlsl", pyVs);
    Slurp(base + ".mesh.json", pyMesh);
    std::vector<std::string> why;
    if (r.ok != pyOk) why.push_back(std::string("compile ") + (r.ok ? "local-only" : "python-only") + (r.ok ? "" : ": " + r.error));
    if (Lf(pyHlsl) != r.hlsl) why.push_back("hlsl");
    if (pyOk && Lf(pyLayout) != r.layout) why.push_back("layout");
    if (Lf(pyGs) != r.gsHlsl) why.push_back("gs");
    if (pyOk && Lf(pyVs) != r.vsHlsl) why.push_back("vs");
    if (pyOk && pyMesh != r.meshJson) why.push_back("mesh.json");
    if (!r.ok && !pyOk) { ++st.bothFail; std::printf("BOTHFAIL %s %s%s\n", name.c_str(), r.error.c_str(), why.empty() ? "" : " (text differs)"); return; }
    if (why.empty()) { ++st.match; if (r.dxbc.size() != pyDxbc.size()) std::printf("MATCH %s (dxbc %zu vs %zu bytes)\n", name.c_str(), r.dxbc.size(), pyDxbc.size()); return; }
    ++st.diff;
    std::string w;
    for (auto& x : why) w += " [" + x + "]";
    std::printf("DIFF %s%s\n", name.c_str(), w.c_str());
    Dump(out + "\\" + name + ".local.hlsl", r.hlsl);
    Dump(out + "\\" + name + ".py.hlsl", Lf(pyHlsl));
    Dump(out + "\\" + name + ".local.json", r.layout);
    Dump(out + "\\" + name + ".py.json", Lf(pyLayout));
    if (!r.gsHlsl.empty() || !pyGs.empty()) { Dump(out + "\\" + name + ".local.gs.hlsl", r.gsHlsl); Dump(out + "\\" + name + ".py.gs.hlsl", Lf(pyGs)); }
    if (!r.vsHlsl.empty() || !pyVs.empty()) { Dump(out + "\\" + name + ".local.vs.hlsl", r.vsHlsl); Dump(out + "\\" + name + ".py.vs.hlsl", Lf(pyVs)); Dump(out + "\\" + name + ".local.mesh.json", r.meshJson); Dump(out + "\\" + name + ".py.mesh.json", pyMesh); }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: xcrosstest <out dir> <xvk-shaders dir>...\n       xcrosstest <out dir> --flat <spv dir> <dxbc.py output dir>\n"); return 2; }
    const std::string out = argv[1];
    CreateDirectoryA(out.c_str(), nullptr);
    Stats st;
    const bool flat = std::string(argv[2]) == "--flat" && argc >= 5;
    for (int a = flat ? 3 : 2; a < (flat ? 4 : argc); ++a) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((std::string(argv[a]) + (flat ? "\\*.spv" : "\\*")).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == '.') continue;
            std::string name = fd.cFileName;
            if (flat) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                name = name.substr(0, name.size() - 4);
                if (name.size() > 4 && (name.compare(name.size() - 4, 4, ".low") == 0 || name.compare(name.size() - 3, 3, ".cs") == 0)) continue;
                Check(out, name, std::string(argv[a]) + "\\" + name + ".spv", std::string(argv[4]) + "\\" + name, 0, st);
            } else {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                const std::string base = std::string(argv[a]) + "\\" + name + "\\" + name;
                Check(out, name, base + ".spv", base, -1, st);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::printf("total %d: match %d, diff %d, both failed %d, skipped (lower64/mesh) %d\n", st.total, st.match, st.diff, st.bothFail, st.skip);
    return st.diff ? 1 : 0;
}
