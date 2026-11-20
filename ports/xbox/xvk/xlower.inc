#include "spirv-tools/libspirv.h"

namespace xcross {

inline std::vector<std::string> SplitLines(const std::string& t) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a < t.size()) {
        size_t b = t.find('\n', a);
        if (b == std::string::npos) b = t.size();
        std::string l = t.substr(a, b - a);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        a = b + 1;
    }
    return out;
}

inline size_t IdEnd(const std::string& s, size_t p) {
    if (p >= s.size() || s[p] != '%') return std::string::npos;
    const size_t e = Word(s, p + 1);
    return e == p + 1 ? std::string::npos : e;
}

inline bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

inline bool IsBlank(const std::string& s, size_t p) {
    for (; p < s.size(); ++p) if (!IsSpace(s[p])) return false;
    return true;
}

inline std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> out;
    size_t p = 0;
    for (;;) {
        while (p < s.size() && IsSpace(s[p])) ++p;
        if (p >= s.size()) return out;
        size_t e = p;
        while (e < s.size() && !IsSpace(s[e])) ++e;
        out.push_back(s.substr(p, e - p));
        p = e;
    }
}

inline std::string ReplaceId(const std::string& s, const std::string& id, const std::string& to) {
    std::string out;
    size_t last = 0;
    for (size_t p = s.find(id); p != std::string::npos; p = s.find(id, p + 1)) {
        const size_t e = p + id.size();
        if (e < s.size() && IsWord(s[e])) continue;
        out += s.substr(last, p - last) + to;
        last = e;
        p = e - 1;
    }
    return out + s.substr(last);
}

inline long long BoundOf(const std::string& text, const char* who) {
    const size_t p = text.find("; Bound: ");
    std::string d;
    size_t q = p + 9;
    if (p == std::string::npos || !Digits(text, q, d)) throw Fail(std::string(who) + ": no bound");
    return std::stoll(d);
}

inline bool Disassemble(const std::vector<uint32_t>& w, std::string& out, std::string& err) {
    spv_context ctx = spvContextCreate(SPV_ENV_UNIVERSAL_1_5);
    spv_text text = nullptr;
    spv_diagnostic diag = nullptr;
    const spv_result_t r = spvBinaryToText(ctx, w.data(), w.size(), SPV_BINARY_TO_TEXT_OPTION_INDENT, &text, &diag);
    if (r == SPV_SUCCESS && text) out.assign(text->str, text->length);
    if (diag) { err = diag->error ? diag->error : "?"; spvDiagnosticDestroy(diag); }
    spvTextDestroy(text);
    spvContextDestroy(ctx);
    return r == SPV_SUCCESS;
}

inline bool Assemble(const std::string& t, std::vector<uint32_t>& out, std::string& err) {
    spv_context ctx = spvContextCreate(SPV_ENV_VULKAN_1_1);
    spv_binary bin = nullptr;
    spv_diagnostic diag = nullptr;
    const spv_result_t r = spvTextToBinaryWithOptions(ctx, t.data(), t.size(), SPV_TEXT_TO_BINARY_OPTION_NONE, &bin, &diag);
    if (r == SPV_SUCCESS && bin) out.assign(bin->code, bin->code + bin->wordCount);
    if (diag) { err = diag->error ? diag->error : "?"; spvDiagnosticDestroy(diag); }
    spvBinaryDestroy(bin);
    spvContextDestroy(ctx);
    return r == SPV_SUCCESS;
}

class Lower64 {
public:
    explicit Lower64(const std::string& text) : lines(SplitLines(text)), bound(BoundOf(text, "lower64")) {}

    bool Run(std::string& result) {
        const std::string text = Join(lines, "\n");
        if (text.find("PhysicalStorageBufferAddresses") == std::string::npos && text.find("OpTypeInt 64") == std::string::npos && text.find("OpAtomicStore") == std::string::npos) return false;
        u32 = FindType("OpTypeInt 32 0");
        wide = FindType("OpTypeInt 64 0");
        if (u32.empty()) throw Fail("lower64: no 32-bit unsigned type");
        const std::string wv = "OpTypeVector " + (wide.empty() ? std::string("%none") : wide) + " ";
        for (const std::string& l : lines) {
            bool bad = l.find("OpTypeInt 64 1") != std::string::npos || l.find("OpTypeFloat 64") != std::string::npos;
            for (size_t p = l.find(wv); p != std::string::npos && !bad; p = l.find(wv, p + 1)) {
                const size_t c = p + wv.size();
                if (c < l.size() && l[c] >= '2' && l[c] <= '4') bad = true;
            }
            if (bad) throw Fail("lower64: unsupported 64-bit type: " + Strip(l));
        }
        uvec2 = FindType("OpTypeVector " + u32 + " 2");
        boolT = GlobalType("OpTypeBool");
        bvec2 = GlobalType("OpTypeVector " + boolT + " 2");
        zero = U32Const(0);
        one = U32Const(1);
        c32 = U32Const(32);
        c2 = U32Const(2);
        c63 = U32Const(63);
        for (const std::string& l : lines) {
            size_t p = 0;
            while (p < l.size() && IsSpace(l[p])) ++p;
            const size_t e = IdEnd(l, p);
            if (e == std::string::npos) continue;
            size_t q = e;
            if (!Eat(l, q, " = OpTypePointer PhysicalStorageBuffer ")) continue;
            const size_t pe = IdEnd(l, q);
            if (pe == std::string::npos) continue;
            psb[l.substr(p, e - p)] = l.substr(q, pe - q);
        }
        rta = New();
        blk = New();
        arr = New();
        arrptr = New();
        eptr = New();
        mem = New();
        if (!psb.empty()) {
            for (const std::string& g : {rta + " = OpTypeRuntimeArray " + u32, blk + " = OpTypeStruct " + rta, arr + " = OpTypeRuntimeArray " + blk,
                                         arrptr + " = OpTypePointer StorageBuffer " + arr, eptr + " = OpTypePointer StorageBuffer " + u32, mem + " = OpVariable " + arrptr + " StorageBuffer"})
                globals.push_back(g);
        }
        std::vector<std::string> out;
        for (const std::string& l : lines) {
            std::vector<std::string> r = Line(l);
            out.insert(out.end(), r.begin(), r.end());
        }
        result = Finish(out);
        return true;
    }

private:
    struct Ptr { bool u32 = true; std::string ac, idx, word; int n = 0; };
    std::vector<std::string> lines;
    long long bound;
    std::map<std::string, std::string> vals, psb;
    std::vector<std::string> globals;
    std::map<uint64_t, std::string> consts;
    std::vector<std::pair<std::string, std::string>> pending;
    std::map<std::string, Ptr> ptrs;
    std::string u32, wide, uvec2, boolT, bvec2, zero, one, c32, c2, c63, rta, blk, arr, arrptr, eptr, mem;
    bool vec2done = false;

    std::string New() { return "%x" + std::to_string(++bound); }

    std::string V(const std::string& id) const {
        auto it = vals.find(id);
        return it == vals.end() ? std::string() : it->second;
    }

    std::string FindType(const std::string& lit) const {
        for (const std::string& l : lines) {
            size_t p = 0;
            while (p < l.size() && IsSpace(l[p])) ++p;
            const size_t e = IdEnd(l, p);
            if (e == std::string::npos) continue;
            size_t q = e;
            if (!Eat(l, q, " = ") || l.compare(q, lit.size(), lit) != 0 || !IsBlank(l, q + lit.size())) continue;
            return l.substr(p, e - p);
        }
        return "";
    }

    std::string GlobalType(const std::string& text) {
        const std::string found = FindType(text);
        if (!found.empty()) return found;
        const std::string tail = "= " + text;
        for (const std::string& g : globals)
            if (g.size() >= tail.size() && g.compare(g.size() - tail.size(), tail.size(), tail) == 0) return Strip(g.substr(0, g.find(" = ")));
        const std::string r = New();
        globals.push_back(r + " = " + text);
        return r;
    }

    std::string U32Const(uint64_t v) {
        v &= 0xffffffffull;
        auto it = consts.find(v);
        if (it != consts.end()) return it->second;
        const std::string r = New();
        pending.push_back({r, r + " = OpConstant " + u32 + " " + std::to_string(v)});
        consts[v] = r;
        return r;
    }

    std::vector<std::string> Emit(std::initializer_list<std::string> ids) {
        std::vector<std::string> out;
        for (const std::string& id : ids) {
            auto it = std::find_if(pending.begin(), pending.end(), [&](auto& p) { return p.first == id; });
            if (it == pending.end()) continue;
            out.push_back(it->second);
            pending.erase(it);
        }
        return out;
    }

    static const std::string& Arg(const std::vector<std::string>& a, size_t i) {
        if (i >= a.size()) throw Fail("lower64: missing operand");
        return a[i];
    }

    std::vector<std::string> Vec2Decl() {
        if (vec2done) return {};
        vec2done = true;
        return {uvec2 + " = OpTypeVector " + u32 + " 2"};
    }

    void Parts(const std::string& v, std::string& lo, std::string& hi, std::vector<std::string>& code) {
        lo = New();
        hi = New();
        code.push_back(lo + " = OpCompositeExtract " + u32 + " " + v + " 0");
        code.push_back(hi + " = OpCompositeExtract " + u32 + " " + v + " 1");
    }

    std::vector<std::string> Line(const std::string& l) {
        const std::string s = Strip(l);
        if (s == "OpCapability Int64" || s == "OpCapability PhysicalStorageBufferAddresses" || s == "OpExtension \"SPV_KHR_physical_storage_buffer\"") return {};
        if (StartsWith(s, "OpMemoryModel PhysicalStorageBuffer64")) { std::string t = s; ReplaceAll(t, "PhysicalStorageBuffer64", "Logical"); return {t}; }
        const size_t e0 = IdEnd(s, 0);
        if (e0 != std::string::npos) { size_t q = e0; if (Eat(s, q, " = OpTypePointer PhysicalStorageBuffer")) return {}; }
        if (!wide.empty() && s == wide + " = OpTypeInt 64 0") {
            if (!uvec2.empty()) return Vec2Decl();
            return {wide + " = OpTypeVector " + u32 + " 2"};
        }
        if (!wide.empty() && !uvec2.empty() && StartsWith(s, "OpName " + wide + " ")) return {};
        if (!wide.empty() && !uvec2.empty() && s == uvec2 + " = OpTypeVector " + u32 + " 2") return Vec2Decl();
        const std::string W = wide.empty() ? std::string("%none") : wide;
        if (e0 != std::string::npos) {
            size_t q = e0;
            std::string d;
            if (Eat(s, q, " = OpConstant ") && Eat(s, q, W.c_str()) && Eat(s, q, " ") && Digits(s, q, d) && q == s.size()) {
                const uint64_t v = std::stoull(d);
                const std::string rid = s.substr(0, e0), lo = U32Const(v), hi = U32Const(v >> 32);
                vals[rid] = wide;
                std::vector<std::string> out = Emit({lo, hi});
                out.push_back(rid + " = OpConstantComposite " + wide + " " + lo + " " + hi);
                return out;
            }
            q = e0;
            if (Eat(s, q, " = OpSpecConstant")) {
                q = Word(s, q);
                if (Eat(s, q, " ") && Eat(s, q, W.c_str()) && (q >= s.size() || !IsWord(s[q]))) throw Fail("lower64: 64-bit spec constant");
            }
        }
        std::string rid, op, rt, rest;
        bool assign = false;
        if (e0 != std::string::npos) {
            size_t q = e0;
            if (Eat(s, q, " = ") && s.compare(q, 2, "Op") == 0) {
                const size_t oe = Word(s, q);
                size_t r = oe;
                if (oe > q + 2 && Eat(s, r, " ")) {
                    const size_t te = IdEnd(s, r);
                    if (te != std::string::npos) {
                        rid = s.substr(0, e0);
                        op = s.substr(q, oe - q);
                        rt = s.substr(r, te - r);
                        rest = s.substr(te);
                        assign = true;
                    }
                }
            }
        }
        if (!assign) {
            std::string sop;
            size_t q = 0;
            if (Eat(s, q, "OpStore ")) sop = "OpStore";
            else if (Eat(s, q, "OpAtomicStore ")) sop = "OpAtomicStore";
            if (!sop.empty()) {
                const size_t pe = IdEnd(s, q);
                size_t r = pe;
                if (pe != std::string::npos && Eat(s, r, " ")) {
                    const std::string ptr = s.substr(q, pe - q), srest = s.substr(r);
                    if (ptrs.count(ptr)) return Store(sop, ptr, srest);
                    if (sop == "OpAtomicStore") {
                        const std::vector<std::string> a = Tokens(srest);
                        const std::string x = New();
                        const std::string t = V(Arg(a, 2));
                        return {x + " = OpAtomicExchange " + (t.empty() ? u32 : t) + " " + ptr + " " + srest};
                    }
                }
            }
            return {l};
        }
        const std::vector<std::string> args = Tokens(rest);
        if (!StartsWith(op, "OpType")) vals[rid] = rt;
        const std::string& w = wide;
        if (op == "OpUConvert") {
            const std::string& src = Arg(args, 0);
            if (rt == w) {
                if (V(src) != u32) {
                    const std::string x = New();
                    return {x + " = OpUConvert " + u32 + " " + src, rid + " = OpCompositeConstruct " + w + " " + x + " " + zero};
                }
                return {rid + " = OpCompositeConstruct " + w + " " + src + " " + zero};
            }
            if (V(src) == w) {
                if (rt == u32) return {rid + " = OpCompositeExtract " + rt + " " + src + " 0"};
                const std::string x = New();
                return {x + " = OpCompositeExtract " + u32 + " " + src + " 0", rid + " = OpUConvert " + rt + " " + x};
            }
            return {l};
        }
        if ((op == "OpIAdd" || op == "OpISub") && rt == w) return AddSub(rid, op, Arg(args, 0), Arg(args, 1));
        const bool shiftOp = op == "OpShiftLeftLogical" || op == "OpShiftRightLogical";
        if (shiftOp && rt == w) return Shift(rid, op, Arg(args, 0), Arg(args, 1));
        if (shiftOp && V(Arg(args, 1)) == w) {
            const std::string x = New();
            return {x + " = OpCompositeExtract " + u32 + " " + args[1] + " 0", rid + " = " + op + " " + rt + " " + args[0] + " " + x};
        }
        static const std::set<std::string> cmp{"OpULessThan", "OpULessThanEqual", "OpUGreaterThan", "OpUGreaterThanEqual", "OpIEqual", "OpINotEqual"};
        if (cmp.count(op) && V(Arg(args, 0)) == w) return Compare(rid, op, args[0], Arg(args, 1));
        if (op == "OpSelect" && rt == w) {
            const std::string c = New();
            return {c + " = OpCompositeConstruct " + bvec2 + " " + Arg(args, 0) + " " + args[0], rid + " = OpSelect " + w + " " + c + " " + Arg(args, 1) + " " + Arg(args, 2)};
        }
        static const std::set<std::string> bad{"OpIMul", "OpUDiv", "OpUMod", "OpSDiv", "OpSMod", "OpSConvert", "OpBitcast", "OpSNegate", "OpConvertUToF", "OpConvertFToU", "OpConvertPtrToU", "OpBitCount", "OpBitReverse", "OpBitFieldUExtract", "OpBitFieldInsert", "OpSLessThan", "OpSGreaterThan", "OpShiftRightArithmetic"};
        if (bad.count(op)) {
            bool hit = rt == w;
            for (const std::string& a : args) hit = hit || V(a) == w;
            if (hit) throw Fail("lower64: unsupported 64-bit " + op);
        }
        if (op == "OpConvertUToPtr") return ToPtr(rid, rt, Arg(args, 0));
        if (op == "OpLoad" && ptrs.count(Arg(args, 0))) return Load(rid, rt, args[0]);
        static const std::set<std::string> atomics{"OpAtomicLoad", "OpAtomicCompareExchange", "OpAtomicExchange", "OpAtomicIAdd", "OpAtomicISub", "OpAtomicUMin", "OpAtomicUMax", "OpAtomicSMin", "OpAtomicSMax", "OpAtomicAnd", "OpAtomicOr", "OpAtomicXor", "OpAtomicIIncrement", "OpAtomicIDecrement"};
        if (atomics.count(op) && ptrs.count(Arg(args, 0))) {
            const Ptr& p = ptrs[args[0]];
            if (!p.u32) throw Fail("lower64: vector atomic");
            return {rid + " = " + op + " " + rt + " " + p.ac + " " + Join(std::vector<std::string>(args.begin() + 1, args.end()), " ")};
        }
        for (const std::string& a : args) if (ptrs.count(a)) throw Fail("lower64: unsupported use of a device address in " + op);
        return {l};
    }

    std::vector<std::string> AddSub(const std::string& rid, const std::string& op, const std::string& a, const std::string& b) {
        std::string al, ah, bl, bh;
        std::vector<std::string> code;
        Parts(a, al, ah, code);
        Parts(b, bl, bh, code);
        const std::string lo = New(), f = New(), cy = New(), hi = New(), h2 = New();
        code.push_back(lo + " = " + op + " " + u32 + " " + al + " " + bl);
        if (op == "OpIAdd") code.push_back(f + " = OpULessThan " + boolT + " " + lo + " " + al);
        else code.push_back(f + " = OpULessThan " + boolT + " " + al + " " + bl);
        code.push_back(cy + " = OpSelect " + u32 + " " + f + " " + one + " " + zero);
        code.push_back(hi + " = " + op + " " + u32 + " " + ah + " " + bh);
        code.push_back(h2 + " = " + op + " " + u32 + " " + hi + " " + cy);
        code.push_back(rid + " = OpCompositeConstruct " + wide + " " + lo + " " + h2);
        return code;
    }

    std::vector<std::string> Shift(const std::string& rid, const std::string& op, const std::string& a, std::string n) {
        std::string al, ah;
        std::vector<std::string> code;
        Parts(a, al, ah, code);
        if (V(n) == wide) {
            const std::string x = New();
            code.push_back(x + " = OpCompositeExtract " + u32 + " " + n + " 0");
            n = x;
        }
        const std::string s = New(), isSmall = New(), nz = New(), inv = New(), sub = New();
        code.push_back(s + " = OpBitwiseAnd " + u32 + " " + n + " " + c63);
        code.push_back(isSmall + " = OpULessThan " + boolT + " " + s + " " + c32);
        code.push_back(nz + " = OpINotEqual " + boolT + " " + s + " " + zero);
        code.push_back(inv + " = OpISub " + u32 + " " + c32 + " " + s);
        code.push_back(sub + " = OpISub " + u32 + " " + s + " " + c32);
        const bool left = op == "OpShiftLeftLogical";
        const std::string nearW = left ? al : ah, farW = left ? ah : al, keep = left ? "OpShiftLeftLogical" : "OpShiftRightLogical", back = left ? "OpShiftRightLogical" : "OpShiftLeftLogical";
        const std::string n1 = New(), f1 = New(), f2 = New(), f3 = New(), f4 = New(), big = New(), rn = New(), rf = New();
        code.push_back(n1 + " = " + keep + " " + u32 + " " + nearW + " " + s);
        code.push_back(f1 + " = " + keep + " " + u32 + " " + farW + " " + s);
        code.push_back(f2 + " = " + back + " " + u32 + " " + nearW + " " + inv);
        code.push_back(f3 + " = OpSelect " + u32 + " " + nz + " " + f2 + " " + zero);
        code.push_back(f4 + " = OpBitwiseOr " + u32 + " " + f1 + " " + f3);
        code.push_back(big + " = " + keep + " " + u32 + " " + nearW + " " + sub);
        code.push_back(rn + " = OpSelect " + u32 + " " + isSmall + " " + n1 + " " + zero);
        code.push_back(rf + " = OpSelect " + u32 + " " + isSmall + " " + f4 + " " + big);
        code.push_back(rid + " = OpCompositeConstruct " + wide + " " + (left ? rn + " " + rf : rf + " " + rn));
        return code;
    }

    std::vector<std::string> Compare(const std::string& rid, const std::string& op, const std::string& a, const std::string& b) {
        std::string al, ah, bl, bh;
        std::vector<std::string> code;
        Parts(a, al, ah, code);
        Parts(b, bl, bh, code);
        if (op == "OpIEqual" || op == "OpINotEqual") {
            const std::string e1 = New(), e2 = New();
            code.push_back(e1 + " = OpIEqual " + boolT + " " + al + " " + bl);
            code.push_back(e2 + " = OpIEqual " + boolT + " " + ah + " " + bh);
            if (op == "OpIEqual") code.push_back(rid + " = OpLogicalAnd " + boolT + " " + e1 + " " + e2);
            else {
                const std::string e3 = New();
                code.push_back(e3 + " = OpLogicalAnd " + boolT + " " + e1 + " " + e2);
                code.push_back(rid + " = OpLogicalNot " + boolT + " " + e3);
            }
            return code;
        }
        const bool swap = op == "OpUGreaterThan" || op == "OpULessThanEqual";
        const std::string xl = swap ? bl : al, xh = swap ? bh : ah, yl = swap ? al : bl, yh = swap ? ah : bh;
        const std::string hl = New(), he = New(), ll = New(), both = New(), lt = New();
        code.push_back(hl + " = OpULessThan " + boolT + " " + xh + " " + yh);
        code.push_back(he + " = OpIEqual " + boolT + " " + xh + " " + yh);
        code.push_back(ll + " = OpULessThan " + boolT + " " + xl + " " + yl);
        code.push_back(both + " = OpLogicalAnd " + boolT + " " + he + " " + ll);
        if (op == "OpULessThan" || op == "OpUGreaterThan") code.push_back(rid + " = OpLogicalOr " + boolT + " " + hl + " " + both);
        else {
            code.push_back(lt + " = OpLogicalOr " + boolT + " " + hl + " " + both);
            code.push_back(rid + " = OpLogicalNot " + boolT + " " + lt);
        }
        return code;
    }

    std::vector<std::string> ToPtr(const std::string& rid, const std::string& rt, const std::string& addr) {
        auto pit = psb.find(rt);
        if (pit == psb.end()) throw Fail("lower64: pointer type " + rt);
        const std::string pointee = pit->second;
        std::string lo, hi;
        std::vector<std::string> code;
        Parts(addr, lo, hi, code);
        const std::string idx = New(), word = New();
        code.push_back(idx + " = OpISub " + u32 + " " + hi + " " + one);
        code.push_back("OpDecorate " + idx + " NonUniform");
        code.push_back(word + " = OpShiftRightLogical " + u32 + " " + lo + " " + c2);
        if (pointee == u32) {
            const std::string ac = New();
            code.push_back(ac + " = OpAccessChain " + eptr + " " + mem + " " + idx + " " + zero + " " + word);
            Ptr p;
            p.u32 = true;
            p.ac = ac;
            ptrs[rid] = p;
            return code;
        }
        std::string decl;
        for (const std::string& l : lines) {
            size_t p = 0;
            while (p < l.size() && IsSpace(l[p])) ++p;
            if (l.compare(p, pointee.size() + 3, pointee + " = ") == 0) { decl = l; break; }
        }
        const std::string pat = "OpTypeVector " + u32 + " ";
        int n = 0;
        for (size_t p = decl.find(pat); p != std::string::npos && !n; p = decl.find(pat, p + 1)) {
            const size_t c = p + pat.size();
            if (c < decl.size() && decl[c] >= '2' && decl[c] <= '4') n = decl[c] - '0';
        }
        if (!n) throw Fail("lower64: pointee " + pointee);
        Ptr p;
        p.u32 = false;
        p.idx = idx;
        p.word = word;
        p.n = n;
        ptrs[rid] = p;
        return code;
    }

    std::vector<std::string> Chain(const Ptr& p, int k, std::string& ac) {
        std::string w = New();
        ac = New();
        std::vector<std::string> code;
        if (k) code.push_back(w + " = OpIAdd " + u32 + " " + p.word + " " + U32Const(uint64_t(k)));
        else w = p.word;
        code.push_back(ac + " = OpAccessChain " + eptr + " " + mem + " " + p.idx + " " + zero + " " + w);
        return code;
    }

    std::vector<std::string> Load(const std::string& rid, const std::string& rt, const std::string& ptr) {
        const Ptr p = ptrs[ptr];
        if (p.u32) return {rid + " = OpLoad " + rt + " " + p.ac};
        std::vector<std::string> code, comps;
        for (int k = 0; k < p.n; ++k) {
            std::string ac;
            std::vector<std::string> c = Chain(p, k, ac);
            const std::string v = New();
            code.insert(code.end(), c.begin(), c.end());
            code.push_back(v + " = OpLoad " + u32 + " " + ac);
            comps.push_back(v);
        }
        code.push_back(rid + " = OpCompositeConstruct " + rt + " " + Join(comps, " "));
        return code;
    }

    std::vector<std::string> Store(const std::string& op, const std::string& ptr, const std::string& rest) {
        const Ptr p = ptrs[ptr];
        const std::vector<std::string> args = Tokens(rest);
        const std::string& val = Arg(args, 0);
        if (op == "OpAtomicStore") {
            if (!p.u32) throw Fail("lower64: vector atomic store");
            const std::string x = New();
            return {x + " = OpAtomicExchange " + u32 + " " + p.ac + " " + rest};
        }
        if (p.u32) return {"OpStore " + p.ac + " " + val};
        std::vector<std::string> code;
        for (int k = 0; k < p.n; ++k) {
            std::string ac;
            std::vector<std::string> c = Chain(p, k, ac);
            const std::string v = New();
            code.insert(code.end(), c.begin(), c.end());
            code.push_back(v + " = OpCompositeExtract " + u32 + " " + val + " " + std::to_string(k));
            code.push_back("OpStore " + ac + " " + v);
        }
        return code;
    }

    std::string Finish(std::vector<std::string> out) {
        std::vector<std::string> g;
        for (auto& p : pending) g.push_back(p.second);
        g.insert(g.end(), globals.begin(), globals.end());
        globals = g;
        if (!wide.empty() && !uvec2.empty()) {
            for (auto& l : out) l = ReplaceId(l, wide, uvec2);
            for (auto& l : globals) l = ReplaceId(l, wide, uvec2);
        }
        std::vector<std::string> decos{"OpDecorate " + rta + " ArrayStride 4", "OpMemberDecorate " + blk + " 0 Offset 0", "OpDecorate " + blk + " Block", "OpDecorate " + mem + " DescriptorSet 10", "OpDecorate " + mem + " Binding 0"};
        std::vector<std::string> body;
        std::set<std::string> bodySet;
        for (const std::string& l : out)
            if (StartsWith(Strip(l), "OpDecorate") && l.find("NonUniform") != std::string::npos && !StartsWith(l, " ")) { body.push_back(l); bodySet.insert(l); }
        std::vector<std::string> kept;
        for (const std::string& l : out) if (!bodySet.count(l)) kept.push_back(l);
        out = kept;
        decos.insert(decos.end(), body.begin(), body.end());
        std::vector<std::string> caps{"OpCapability ShaderNonUniform", "OpCapability RuntimeDescriptorArray", "OpCapability StorageBufferArrayNonUniformIndexing"};
        std::vector<std::string> ext{"OpExtension \"SPV_EXT_descriptor_indexing\""};
        if (psb.empty()) { decos.clear(); caps.clear(); ext.clear(); }
        long lastDeco = -1;
        for (size_t i = 0; i < out.size(); ++i) {
            const std::string s = Strip(out[i]);
            for (const char* p : {"OpDecorate", "OpMemberDecorate", "OpEntryPoint", "OpExecutionMode", "OpName", "OpSource"})
                if (StartsWith(s, p)) lastDeco = long(i);
        }
        if (lastDeco < 0) throw Fail("lower64: no decorations");
        std::vector<std::string> res;
        bool placedCaps = false, placedGlobals = false;
        for (size_t i = 0; i < out.size(); ++i) {
            const std::string& l = out[i];
            const std::string s = Strip(l);
            if (!placedCaps && StartsWith(s, "OpCapability")) { res.insert(res.end(), caps.begin(), caps.end()); placedCaps = true; }
            if (!ext.empty() && placedCaps && !StartsWith(s, "OpCapability")) { res.insert(res.end(), ext.begin(), ext.end()); ext.clear(); }
            bool fn = StartsWith(s, "OpFunction ");
            if (!fn) { const size_t e = IdEnd(s, 0); size_t q = e; fn = e != std::string::npos && Eat(s, q, " = OpFunction "); }
            if (!placedGlobals && fn) { res.insert(res.end(), globals.begin(), globals.end()); placedGlobals = true; }
            res.push_back(l);
            if (long(i) == lastDeco) res.insert(res.end(), decos.begin(), decos.end());
        }
        return Join(res, "\n") + "\n";
    }
};

struct MeshInfo {
    long long maxv = 0, maxp = 0, threads = 0, record = 0, block = 0;
    std::vector<long long> locs;
    bool cull = false;
};

inline std::string Mesh2cs(const std::string& text, MeshInfo& info) {
    const std::vector<std::string> lines = SplitLines(text);
    long long bound = BoundOf(text, "mesh2cs");
    auto New = [&]() { return "%m" + std::to_string(++bound); };
    std::string entry;
    for (const std::string& l : lines) {
        size_t p = 0;
        while (p < l.size() && IsSpace(l[p])) ++p;
        if (!Eat(l, p, "OpEntryPoint MeshEXT ")) continue;
        const size_t e = IdEnd(l, p);
        if (e != std::string::npos && e < l.size() && l[e] == ' ') entry = l.substr(p, e - p);
    }
    if (entry.empty()) throw Fail("mesh2cs: no mesh entry point");
    std::vector<std::string> outOrder;
    std::set<std::string> outvars, invars;
    std::map<std::string, std::vector<std::string>> decos;
    std::vector<std::pair<std::pair<std::string, std::string>, std::string>> ptrs;
    auto ptrGet = [&](const std::string& cls, const std::string& pointee) -> std::string {
        for (auto& p : ptrs) if (p.first.first == cls && p.first.second == pointee) return p.second;
        return "";
    };
    auto assignVar = [](const std::string& s, const char* cls, std::string& id, std::string& ty) {
        const size_t e = IdEnd(s, 0);
        if (e == std::string::npos) return false;
        size_t q = e;
        if (!Eat(s, q, " = OpVariable ")) return false;
        const size_t te = IdEnd(s, q);
        if (te == std::string::npos) return false;
        size_t r = te;
        if (!Eat(s, r, " ") || !Eat(s, r, cls) || r != s.size()) return false;
        id = s.substr(0, e);
        ty = s.substr(q, te - q);
        return true;
    };
    for (const std::string& l : lines) {
        const std::string s = Strip(l);
        std::string id, ty;
        if (assignVar(s, "Output", id, ty) && !outvars.count(id)) { outvars.insert(id); outOrder.push_back(id); }
        if (assignVar(s, "Input", id, ty)) invars.insert(id);
        size_t q = 0;
        if (Eat(s, q, "OpDecorate ")) {
            const size_t e = IdEnd(s, q);
            size_t r = e;
            if (e != std::string::npos && Eat(s, r, " ")) decos[s.substr(q, e - q)].push_back(s.substr(r));
        }
        const size_t e = IdEnd(s, 0);
        q = e;
        if (e != std::string::npos && Eat(s, q, " = OpTypePointer ")) {
            const size_t ce = Word(s, q);
            size_t r = ce;
            if (ce > q && Eat(s, r, " ")) {
                const size_t pe = IdEnd(s, r);
                if (pe == s.size()) {
                    const std::string cls = s.substr(q, ce - q), pointee = s.substr(r, pe - r), pid = s.substr(0, e);
                    auto it = std::find_if(ptrs.begin(), ptrs.end(), [&](auto& p) { return p.first.first == cls && p.first.second == pointee; });
                    if (it == ptrs.end()) ptrs.push_back({{cls, pointee}, pid}); else it->second = pid;
                }
            }
        }
        q = 0;
        if (Eat(s, q, "OpExecutionMode ") && Eat(s, q, entry.c_str()) && Eat(s, q, " ")) {
            size_t r = q;
            std::string d;
            for (const char* m : {"OutputVertices", "OutputPrimitivesNV", "OutputPrimitivesEXT"}) {
                r = q;
                if (Eat(s, r, m) && Eat(s, r, " ") && Digits(s, r, d)) { (std::string(m) == "OutputVertices" ? info.maxv : info.maxp) = std::stoll(d); break; }
            }
            r = q;
            if (Eat(s, r, "OutputLines") || Eat(s, r, "OutputPoints")) throw Fail("mesh2cs: only triangle output is supported");
        }
    }
    std::vector<std::pair<std::string, std::string>> names;
    auto hasName = [&](const char* n) { for (auto& x : names) if (x.second == n) return true; return false; };
    for (const std::string& v : outOrder) {
        const std::vector<std::string>& ds = decos[v];
        long long loc = -1;
        bool haveLoc = false, perPrim = false;
        for (const std::string& d : ds) {
            if (!haveLoc && StartsWith(d, "Location ")) {
                const std::vector<std::string> t = Tokens(d);
                if (t.size() < 2) throw Fail("mesh2cs: bad Location");
                loc = std::stoll(t[1]);
                haveLoc = true;
            }
            if (StartsWith(d, "PerPrimitive")) perPrim = true;
        }
        auto has = [&](const char* x) { return std::find(ds.begin(), ds.end(), x) != ds.end(); };
        if (has("BuiltIn Position")) names.push_back({v, "xvk_pos"});
        else if (has("BuiltIn PrimitiveTriangleIndicesEXT")) names.push_back({v, "xvk_idx"});
        else if (has("BuiltIn CullPrimitiveEXT")) { names.push_back({v, "xvk_cull"}); info.cull = true; }
        else if (haveLoc && !perPrim) { names.push_back({v, "xvk_v" + std::to_string(loc)}); info.locs.push_back(loc); }
        else throw Fail("mesh2cs: unsupported mesh output " + v);
    }
    if (!hasName("xvk_pos") || !hasName("xvk_idx")) throw Fail("mesh2cs: mesh shader without position or triangle indices");
    std::sort(info.locs.begin(), info.locs.end());
    std::vector<std::pair<std::string, std::string>> remap;
    for (auto& p : ptrs) {
        if (p.first.first != "Output") continue;
        const std::string w = ptrGet("Workgroup", p.first.second);
        if (!w.empty()) remap.push_back({p.second, w});
    }
    std::string u32;
    for (const std::string& l : lines) {
        size_t p = 0;
        while (p < l.size() && IsSpace(l[p])) ++p;
        const size_t e = IdEnd(l, p);
        size_t q = e;
        if (e != std::string::npos && Eat(l, q, " = OpTypeInt 32 0") && q == l.size()) { u32 = l.substr(p, e - p); break; }
    }
    if (u32.empty()) throw Fail("mesh2cs: no 32-bit unsigned type");
    std::string wu32 = ptrGet("Workgroup", u32);
    std::vector<std::string> extra;
    if (wu32.empty()) { wu32 = New(); extra.push_back(wu32 + " = OpTypePointer Workgroup " + u32); }
    const std::string vcount = New(), pcount = New();
    extra.push_back(vcount + " = OpVariable " + wu32 + " Workgroup");
    extra.push_back(pcount + " = OpVariable " + wu32 + " Workgroup");
    std::vector<std::string> out;
    bool placed = false;
    for (const std::string& l : lines) {
        std::string s = Strip(l);
        if (s == "OpCapability MeshShadingEXT" || s == "OpCapability MeshShadingNV" || s == "OpExtension \"SPV_EXT_mesh_shader\"" || s == "OpExtension \"SPV_NV_mesh_shader\"") continue;
        {
            size_t q = 0;
            if (Eat(s, q, "OpExecutionMode ") && Eat(s, q, entry.c_str()) && Eat(s, q, " Output") && Word(s, q) > q) continue;
        }
        {
            size_t q = 0;
            if (Eat(s, q, "OpDecorate ") || Eat(s, q, "OpName ")) {
                const size_t e = IdEnd(s, q);
                if (e != std::string::npos && e < s.size() && s[e] == ' ' && outvars.count(s.substr(q, e - q))) continue;
            }
        }
        {
            const size_t e = IdEnd(s, 0);
            size_t q = e;
            if (e != std::string::npos && Eat(s, q, " = OpTypePointer Output ")) {
                const size_t pe = IdEnd(s, q);
                if (pe == s.size()) {
                    const std::string pid = s.substr(0, e);
                    if (std::find_if(remap.begin(), remap.end(), [&](auto& r) { return r.first == pid; }) != remap.end()) continue;
                    s = pid + " = OpTypePointer Workgroup " + s.substr(q, pe - q);
                }
            }
        }
        {
            std::string id, ty;
            if (assignVar(s, "Output", id, ty)) s = id + " = OpVariable " + ty + " Workgroup";
        }
        if (StartsWith(s, "OpEntryPoint MeshEXT")) {
            const size_t m = s.find("\"main\"");
            if (m == std::string::npos) throw Fail("mesh2cs: entry point is not main");
            std::string head = s.substr(0, m);
            std::vector<std::string> keep;
            for (const std::string& t : Tokens(s.substr(m + 6))) if (invars.count(t)) keep.push_back(t);
            head.replace(head.find("OpEntryPoint MeshEXT"), 20, "OpEntryPoint GLCompute");
            s = head + "\"main\" " + Join(keep, " ");
        }
        {
            size_t q = 0;
            if (Eat(s, q, "OpSetMeshOutputsEXT ")) {
                const size_t a = IdEnd(s, q);
                size_t r = a;
                if (a != std::string::npos && Eat(s, r, " ")) {
                    const size_t b = IdEnd(s, r);
                    if (b == s.size()) {
                        out.push_back("OpStore " + vcount + " " + s.substr(q, a - q));
                        out.push_back("OpStore " + pcount + " " + s.substr(r, b - r));
                        continue;
                    }
                }
            }
        }
        if (!placed) {
            bool fn = StartsWith(s, "OpFunction ");
            if (!fn) { const size_t e = IdEnd(s, 0); size_t q = e; fn = e != std::string::npos && Eat(s, q, " = OpFunction "); }
            if (fn) { out.insert(out.end(), extra.begin(), extra.end()); placed = true; }
        }
        if (StartsWith(s, "OpMemoryModel")) { out.push_back(s); continue; }
        for (auto& r : remap) s = ReplaceId(s, r.first, r.second);
        out.push_back(s);
    }
    names.push_back({vcount, "xvk_vcount"});
    names.push_back({pcount, "xvk_pcount"});
    size_t first = out.size();
    for (size_t i = 0; i < out.size(); ++i)
        if (StartsWith(out[i], "OpName") || StartsWith(out[i], "OpDecorate") || StartsWith(out[i], "OpMemberName")) { first = i; break; }
    if (first == out.size()) throw Fail("mesh2cs: no names or decorations");
    std::vector<std::string> nl;
    for (auto& n : names) nl.push_back("OpName " + n.first + " \"" + n.second + "\"");
    out.insert(out.begin() + long(first), nl.begin(), nl.end());
    return Join(out, "\n") + "\n";
}

inline std::string MeshJson(const MeshInfo& m) {
    std::string locs;
    for (size_t i = 0; i < m.locs.size(); ++i) locs += (i ? ", " : "") + std::to_string(m.locs[i]);
    return "{\"maxv\": " + std::to_string(m.maxv) + ", \"maxp\": " + std::to_string(m.maxp) + ", \"locs\": [" + locs + "], \"cull\": " + (m.cull ? "true" : "false") +
           ", \"threads\": " + std::to_string(m.threads) + ", \"record\": " + std::to_string(m.record) + ", \"block\": " + std::to_string(m.block) + "}";
}

inline std::string FindDeclName(const std::string& hlsl, const std::string& prefix, const std::string& suffix) {
    for (size_t p = hlsl.find(prefix); p != std::string::npos; p = hlsl.find(prefix, p + 1)) {
        const size_t a = p + prefix.size(), e = Word(hlsl, a);
        if (e > a && hlsl.compare(e, suffix.size(), suffix) == 0) return hlsl.substr(a, e - a);
    }
    throw Fail("mesh2cs: " + suffix + " input missing");
}

inline std::string MeshEpilogue(std::string hlsl, MeshInfo& info, bool flip, std::string& vsOut) {
    long long t[3] = {0, 0, 0};
    bool found = false;
    for (size_t p = hlsl.find("[numthreads("); p != std::string::npos && !found; p = hlsl.find("[numthreads(", p + 1)) {
        size_t q = p + 12;
        std::string a, b, c;
        if (Digits(hlsl, q, a) && Eat(hlsl, q, ", ") && Digits(hlsl, q, b) && Eat(hlsl, q, ", ") && Digits(hlsl, q, c) && Eat(hlsl, q, ")]")) {
            t[0] = std::stoll(a); t[1] = std::stoll(b); t[2] = std::stoll(c);
            found = true;
        }
    }
    if (!found) throw Fail("mesh2cs: no numthreads");
    const long long threads = t[0] * t[1] * t[2], n = info.maxv, p = info.maxp, r = 4 * (1 + (long long)info.locs.size()), b = n * r + p * 4;
    std::string decl = "RWByteAddressBuffer xvk_mesh_out : register(u0, space11);\n", groups;
    if (hlsl.find("cbuffer SPIRV_Cross_NumWorkgroups\n") != std::string::npos) {
        ReplaceAll(hlsl, "cbuffer SPIRV_Cross_NumWorkgroups\n", "cbuffer SPIRV_Cross_NumWorkgroups : register(b0, space11)\n");
        groups = "SPIRV_Cross_NumWorkgroups_1_count";
    } else {
        decl += "cbuffer xvk_mesh_groups : register(b0, space11) { uint4 xvk_groups; };\n";
        groups = "xvk_groups";
    }
    size_t mEnd = std::string::npos;
    const std::string sig = " main(SPIRV_Cross_Input stage_input)\n{\n";
    for (size_t ls = 0; ls < hlsl.size() && mEnd == std::string::npos;) {
        const size_t e = Word(hlsl, ls);
        if (e > ls && hlsl.compare(e, sig.size(), sig) == 0) { mEnd = e + sig.size(); break; }
        const size_t nl = hlsl.find('\n', ls);
        if (nl == std::string::npos) break;
        ls = nl + 1;
    }
    if (mEnd == std::string::npos || hlsl.find("SV_GroupID") == std::string::npos || hlsl.find("SV_GroupIndex") == std::string::npos) throw Fail("mesh2cs: main needs SV_GroupID and SV_GroupIndex inputs");
    const std::string gid = FindDeclName(hlsl, "uint3 ", " : SV_GroupID"), lid = FindDeclName(hlsl, "uint ", " : SV_GroupIndex");
    {
        std::vector<std::string> ls = Split(hlsl);
        for (std::string& l : ls) {
            size_t q = 0;
            while (q < l.size() && IsSpace(l[q])) ++q;
            if (l.compare(q, 13, "xvk_vcount = ") != 0 && l.compare(q, 13, "xvk_pcount = ") != 0) continue;
            const size_t semi = l.find(';', q + 13);
            if (semi == std::string::npos || semi == q + 13 || semi + 1 != l.size()) continue;
            l = l.substr(0, q) + "if (" + lid + " == 0u) " + l.substr(q);
        }
        hlsl = Join(ls, "\n");
    }
    auto u = [](long long v) { return std::to_string(v) + "u"; };
    std::vector<std::string> body{"    GroupMemoryBarrierWithGroupSync();",
                                  "    uint xvk_g = (stage_input." + gid + ".z * " + groups + ".y + stage_input." + gid + ".y) * " + groups + ".x + stage_input." + gid + ".x;",
                                  "    uint xvk_base = xvk_g * " + u(b) + ";",
                                  "    uint xvk_vc = min(xvk_vcount, " + u(n) + ");",
                                  "    uint xvk_pc = min(xvk_pcount, " + u(p) + ");",
                                  "    for (uint xvk_v = stage_input." + lid + "; xvk_v < xvk_vc; xvk_v += " + u(threads) + ")",
                                  "    {",
                                  "        uint xvk_a = (xvk_base + xvk_v * " + u(r) + ") * 4u;",
                                  "        xvk_mesh_out.Store4(xvk_a, asuint(xvk_pos[xvk_v]));"};
    for (size_t k = 0; k < info.locs.size(); ++k) body.push_back("        xvk_mesh_out.Store4(xvk_a + " + u(16 * (long long)(k + 1)) + ", asuint(xvk_v" + std::to_string(info.locs[k]) + "[xvk_v]));");
    body.push_back("    }");
    body.push_back("    for (uint xvk_p = stage_input." + lid + "; xvk_p < " + u(p) + "; xvk_p += " + u(threads) + ")");
    body.push_back("    {");
    body.push_back(std::string("        bool xvk_live = xvk_p < xvk_pc") + (info.cull ? " && !xvk_cull[xvk_p]" : "") + ";");
    body.push_back("        uint3 xvk_i = xvk_live ? min(xvk_idx[xvk_p], uint3(" + u(n - 1) + ", " + u(n - 1) + ", " + u(n - 1) + ")) : uint3(0u, 0u, 0u);");
    body.push_back("        xvk_mesh_out.Store4((xvk_base + " + u(n * r) + " + xvk_p * 4u) * 4u, uint4(xvk_i, xvk_live ? 1u : 0u));");
    body.push_back("    }");
    const size_t end = hlsl.find("\n}", mEnd);
    if (end == std::string::npos) throw Fail("mesh2cs: main has no end");
    hlsl = decl + hlsl.substr(0, end) + "\n" + Join(body, "\n") + hlsl.substr(end);
    std::vector<std::string> vs{"ByteAddressBuffer xvk_mesh_in : register(t0, space11);", "struct VSOut", "{", "    float4 pos : SV_Position;"};
    for (long long loc : info.locs) vs.push_back("    float4 v" + std::to_string(loc) + " : TEXCOORD" + std::to_string(loc) + ";");
    for (const std::string& l : std::vector<std::string>{"};", "", "VSOut main(uint vid : SV_VertexID)", "{", "    uint prim = vid / 3u;", "    uint corner = vid - prim * 3u;",
                                                         "    uint g = prim / " + u(p) + ";", "    uint p = prim - g * " + u(p) + ";", "    uint base = g * " + u(b) + ";",
                                                         "    uint4 pr = xvk_mesh_in.Load4((base + " + u(n * r) + " + p * 4u) * 4u);",
                                                         "    uint idx = corner == 0u ? pr.x : (corner == 1u ? pr.y : pr.z);", "    uint a = (base + idx * " + u(r) + ") * 4u;",
                                                         "    VSOut o;", "    o.pos = asfloat(xvk_mesh_in.Load4(a));"})
        vs.push_back(l);
    for (size_t k = 0; k < info.locs.size(); ++k) vs.push_back("    o.v" + std::to_string(info.locs[k]) + " = asfloat(xvk_mesh_in.Load4(a + " + u(16 * (long long)(k + 1)) + "));");
    if (flip) vs.push_back("    o.pos.y = -o.pos.y;");
    for (const char* l : {"    if (pr.w == 0u) o.pos = asfloat(uint4(0x7fc00000u, 0x7fc00000u, 0x7fc00000u, 0x7fc00000u));", "    return o;", "}", ""}) vs.push_back(l);
    info.threads = threads;
    info.record = r;
    info.block = b;
    vsOut = Join(vs, "\n");
    return hlsl;
}

}
