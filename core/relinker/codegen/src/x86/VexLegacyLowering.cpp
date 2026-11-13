#include <codegen/x86/VexLegacyLowering.hpp>
#include <codegen/x86/ScalarLowering.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/CodegenException.hpp>
#include <cstring>
#include <span>
#include <string>

namespace Codegen {

namespace {

constexpr const char* kName = "VEX.128";
constexpr const char* kZeroUpper = "VZEROUPPER";
constexpr std::uint8_t kMandatoryPrefix[] = {0, 0x66, 0xF3, 0xF2};

enum class Shape : std::uint8_t {
    None,
    Nds,
    Unary,
    ShiftImm,
    ScalarMove,
    ScalarStore
};

struct Form {
    Shape Kind = Shape::None;
    bool Aligned = false;
    bool Wide = false;
    bool Predicate = false;
    bool GprSource = false;
    bool Commutative = false;
};

constexpr Form Nds(const bool aligned) { return {Shape::Nds, aligned}; }
constexpr Form Unary(const bool aligned) { return {Shape::Unary, aligned}; }
constexpr Form Integer() { return {Shape::Nds, true, false, false, false, true}; }

bool In(const std::uint8_t op, const std::uint8_t first, const std::uint8_t last) { return op >= first && op <= last; }

bool CommutativeInteger(const std::uint8_t op) {
    return op == 0x74 || op == 0x75 || op == 0x76 || op == 0xD4 || op == 0xD5 || op == 0xDA || op == 0xDB || op == 0xDC || op == 0xDD || op == 0xDE
        || op == 0xE0 || op == 0xE3 || op == 0xE4 || op == 0xE5 || op == 0xEA || op == 0xEB || op == 0xEC || op == 0xED || op == 0xEE || op == 0xEF
        || op == 0xF4 || op == 0xF5 || op == 0xFC || op == 0xFD || op == 0xFE;
}

Form Map0F(const std::uint8_t pp, const std::uint8_t op) {
    if (pp == 0 || pp == 1) {
        if (op == 0x10 || op == 0x11 || op == 0x13 || op == 0x17 || op == 0x28 || op == 0x29 || op == 0x2B || op == 0x2E || op == 0x2F || op == 0x50) return Unary(false);
        if (op == 0x12 || op == 0x16) return Nds(false);
        if (op == 0x14 || op == 0x15 || In(op, 0x54, 0x59) || In(op, 0x5C, 0x5F) || op == 0xC6) return Nds(true);
        if (op == 0x51 || op == 0x5B) return Unary(true);
        if (op == 0x5A) return Unary(pp == 1);
        if (op == 0xC2) return {Shape::Nds, true, false, true};
    }
    if (pp == 1) {
        if (CommutativeInteger(op)) return Integer();
        if (In(op, 0x60, 0x6D) || op == 0x7C || op == 0x7D || op == 0xD0 || In(op, 0xD1, 0xD3) || In(op, 0xD8, 0xD9) || op == 0xDF || op == 0xE1 || op == 0xE2 || In(op, 0xE8, 0xE9) || op == 0xF1 || op == 0xF2 || op == 0xF3 || op == 0xF6 || In(op, 0xF8, 0xFB)) return Nds(true);
        if (op == 0x6E || op == 0x7E) return {Shape::Unary, false, true};
        if (op == 0x6F || op == 0x7F || op == 0xD6 || op == 0xD7 || op == 0xE7) return Unary(false);
        if (op == 0x70 || op == 0xE6) return Unary(true);
        if (In(op, 0x71, 0x73)) return {Shape::ShiftImm};
    }
    if (pp == 2 || pp == 3) {
        if (op == 0x10) return {Shape::ScalarMove};
        if (op == 0x11) return {Shape::ScalarStore};
        if (op == 0x2A) return {Shape::Nds, false, true, false, true};
        if (op == 0x2C || op == 0x2D) return {Shape::Unary, false, true};
        if (op == 0x51 || op == 0x58 || op == 0x59 || op == 0x5A || In(op, 0x5C, 0x5F)) return Nds(false);
        if (op == 0xC2) return {Shape::Nds, false, false, true};
        if (op == 0x70) return Unary(true);
    }
    if (pp == 2) {
        if (op == 0x12 || op == 0x16 || op == 0x5B) return Unary(true);
        if (op == 0x6F || op == 0x7E || op == 0x7F || op == 0xE6) return Unary(false);
    }
    if (pp == 3) {
        if (op == 0x12 || op == 0xF0) return Unary(false);
        if (op == 0x7C || op == 0x7D || op == 0xD0) return Nds(true);
        if (op == 0xE6) return Unary(true);
    }
    return {};
}

Form Map0F38(const std::uint8_t op) {
    if (op == 0x29 || In(op, 0x38, 0x40) || op == 0x28) return Integer();
    if (In(op, 0x00, 0x0B) || op == 0x2B || op == 0x37 || In(op, 0xDC, 0xDF)) return Nds(true);
    if (op == 0x17 || In(op, 0x1C, 0x1E) || op == 0x41 || op == 0xDB) return Unary(true);
    if (In(op, 0x20, 0x25) || In(op, 0x30, 0x35) || op == 0x2A) return Unary(false);
    return {};
}

Form Map0F3A(const std::uint8_t op) {
    if (op == 0x08 || op == 0x09 || op == 0xDF) return Unary(true);
    if (op == 0x0A || op == 0x0B || op == 0x21) return Nds(false);
    if (op == 0x20) return {Shape::Nds, false, false, false, true};
    if (op == 0x22) return {Shape::Nds, false, true, false, true};
    if (In(op, 0x0C, 0x0F) || In(op, 0x40, 0x42) || op == 0x44) return Nds(true);
    if (op == 0x14 || op == 0x15 || op == 0x17) return Unary(false);
    if (op == 0x16 || In(op, 0x60, 0x63)) return {Shape::Unary, false, true};
    return {};
}

Form Lookup(const std::uint8_t map, const std::uint8_t pp, const std::uint8_t op) {
    if (map == 1) return Map0F(pp, op);
    if (pp != 1) return {};
    if (map == 2) return Map0F38(op);
    if (map == 3) return Map0F3A(op);
    return {};
}

bool HasImmediate(const std::uint8_t map, const std::uint8_t op) {
    return map == 3 || (map == 1 && (op == 0x70 || In(op, 0x71, 0x73) || op == 0xC2 || op == 0xC6));
}

struct Parsed {
    std::size_t Vex;
    std::size_t OpIndex;
    std::uint8_t R, X, B, Map, Pp, Op, Modrm, Vvvv;
    bool W;
    bool Memory;
    std::uint8_t Reg, Rm;
    Form Kind;
};

std::optional<Parsed> Parse(const std::uint8_t* data, const std::size_t length) {
    Parsed p{};
    while (p.Vex < length && ScalarLowering::LegacyPrefix(data[p.Vex])) ++p.Vex;
    if (p.Vex + 3 > length || (data[p.Vex] != 0xC4 && data[p.Vex] != 0xC5)) return std::nullopt;
    const bool threeByte = data[p.Vex] == 0xC4;
    if (threeByte && p.Vex + 4 > length) return std::nullopt;
    const auto payload = data[p.Vex + 1];
    const auto last = threeByte ? data[p.Vex + 2] : payload;
    p.R = (payload & 0x80) ? 0 : 1;
    p.X = threeByte && !(payload & 0x40) ? 1 : 0;
    p.B = threeByte && !(payload & 0x20) ? 1 : 0;
    p.Map = threeByte ? static_cast<std::uint8_t>(payload & 0x1F) : 1;
    p.W = threeByte && (last & 0x80) != 0;
    p.Vvvv = static_cast<std::uint8_t>(~last >> 3 & 0xF);
    p.Pp = static_cast<std::uint8_t>(last & 3);
    p.OpIndex = p.Vex + (threeByte ? 3 : 2);
    p.Op = data[p.OpIndex];
    if ((last & 4) != 0) return std::nullopt;
    if (p.Map == 1 && p.Op == 0x77) {
        if (p.Pp != 0 || p.Vvvv != 0 || p.OpIndex + 1 != length) return std::nullopt;
        return p;
    }
    if (p.OpIndex + 2 > length) return std::nullopt;
    p.Kind = Lookup(p.Map, p.Pp, p.Op);
    if (p.Kind.Kind == Shape::None) return std::nullopt;
    p.Modrm = data[p.OpIndex + 1];
    p.Memory = (p.Modrm >> 6) != 3;
    p.Reg = static_cast<std::uint8_t>(p.R << 3 | (p.Modrm >> 3 & 7));
    p.Rm = static_cast<std::uint8_t>(p.B << 3 | (p.Modrm & 7));
    if (p.Kind.Predicate && data[length - 1] >= 8) return std::nullopt;
    if (p.Kind.Kind == Shape::Unary && p.Vvvv != 0) return std::nullopt;
    if (p.Kind.Kind == Shape::ShiftImm && p.Memory) return std::nullopt;
    if ((p.Kind.Kind == Shape::ScalarMove || p.Kind.Kind == Shape::ScalarStore) && p.Memory && p.Vvvv != 0) return std::nullopt;
    return p;
}

bool ZeroUpper(const Parsed& p) { return p.Map == 1 && p.Op == 0x77; }

bool InPlaceShape(const Parsed& p) {
    if (p.Memory && p.Kind.Aligned) return false;
    switch (p.Kind.Kind) {
    case Shape::Nds: return p.Vvvv == p.Reg;
    case Shape::Unary: return true;
    case Shape::ShiftImm: return p.Vvvv == p.Rm;
    case Shape::ScalarMove: return p.Memory || p.Vvvv == p.Reg;
    case Shape::ScalarStore: return p.Memory || p.Vvvv == p.Rm;
    case Shape::None: return false;
    }
    return false;
}

std::vector<std::uint8_t> Header(const Parsed& p, const std::uint8_t r, const std::uint8_t x, const std::uint8_t b, const std::vector<std::uint8_t>& prefixes) {
    std::vector<std::uint8_t> bytes(prefixes);
    if (p.Pp != 0) bytes.push_back(kMandatoryPrefix[p.Pp]);
    const auto rex = static_cast<std::uint8_t>((p.Kind.Wide && p.W ? 8 : 0) | r << 2 | x << 1 | b);
    if (rex != 0) bytes.push_back(static_cast<std::uint8_t>(0x40 | rex));
    bytes.push_back(0x0F);
    if (p.Map == 2) bytes.push_back(0x38);
    if (p.Map == 3) bytes.push_back(0x3A);
    return bytes;
}

std::vector<std::uint8_t> LegacyBytes(const std::uint8_t* data, const std::size_t length, const Parsed& p, std::int32_t& shrink) {
    auto legacy = Header(p, p.R, p.X, p.B, std::vector<std::uint8_t>(data, data + p.Vex));
    shrink = static_cast<std::int32_t>(p.OpIndex) - static_cast<std::int32_t>(legacy.size());
    const auto body = legacy.size();
    legacy.insert(legacy.end(), data + p.OpIndex, data + length);
    if ((p.Modrm & 0xC7) == 0x05) {
        const auto at = body + 2;
        if (at + 4 > legacy.size()) throw CodegenException("VEX.128 RIP-relative operand is truncated");
        std::int32_t displacement = 0;
        std::memcpy(&displacement, legacy.data() + at, sizeof(displacement));
        displacement += shrink;
        std::memcpy(legacy.data() + at, &displacement, sizeof(displacement));
    }
    return legacy;
}

std::vector<std::uint8_t> RegisterForm(const Parsed& p, const std::uint8_t opcode, const std::uint8_t reg, const std::uint8_t rm, const std::optional<std::uint8_t> imm) {
    auto bytes = Header(p, reg >> 3, 0, rm >> 3, {});
    bytes.push_back(opcode);
    bytes.push_back(static_cast<std::uint8_t>(0xC0 | (reg & 7) << 3 | (rm & 7)));
    if (imm) bytes.push_back(*imm);
    return bytes;
}

std::vector<std::uint8_t> Movaps(const std::uint8_t dst, const std::uint8_t src) {
    std::vector<std::uint8_t> bytes;
    if ((dst | src) & 8) bytes.push_back(static_cast<std::uint8_t>(0x40 | (dst >> 3) << 2 | src >> 3));
    bytes.insert(bytes.end(), {0x0F, 0x28, static_cast<std::uint8_t>(0xC0 | (dst & 7) << 3 | (src & 7))});
    return bytes;
}

std::uint8_t Scratch(std::initializer_list<std::uint8_t> used) {
    for (std::uint8_t candidate = 0; candidate < 16; ++candidate) {
        bool taken = false;
        for (const auto value : used) taken = taken || value == candidate;
        if (!taken) return candidate;
    }
    throw CodegenException("No scratch XMM register for a VEX.128 stub");
}

void EmitLegacy(StubBodyBuilder& body, std::span<const std::uint8_t> bytes, const std::string& name) {
    const X64InstructionDecoder decoder;
    const auto info = decoder.DecodeInstruction(bytes.data(), bytes.size());
    if (info.Length != bytes.size())
        throw CodegenException(name + " replacement does not decode to one instruction");
    if (!info.HasRipRelativeDisp) {
        body.Raw(bytes);
        return;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + info.RipRelativeDispOffset, sizeof(displacement));
    body.RipRelative(bytes, info.RipRelativeDispOffset, static_cast<std::int64_t>(bytes.size()) + displacement);
}

}

std::optional<VexLegacy> DecodeVexLegacy(const std::uint8_t* data, const std::size_t length) {
    const auto p = Parse(data, length);
    if (!p) return std::nullopt;
    if (ZeroUpper(*p)) return VexLegacy{kZeroUpper, {}};
    if (!InPlaceShape(*p)) return std::nullopt;
    std::int32_t shrink = 0;
    auto legacy = LegacyBytes(data, length, *p, shrink);
    if (shrink < 0) return std::nullopt;
    return VexLegacy{kName, std::move(legacy)};
}

void EmitVexLegacy(StubBodyBuilder& body, const VexLegacy& legacy) {
    if (legacy.Replacement.empty()) return;
    EmitLegacy(body, legacy.Replacement, legacy.Name);
}

std::optional<VexStub> DecodeVexStub(const std::uint8_t* data, const std::size_t length) {
    const auto p = Parse(data, length);
    if (!p || ZeroUpper(*p)) return std::nullopt;
    if (DecodeVexLegacy(data, length)) return std::nullopt;
    return VexStub{kName, std::vector<std::uint8_t>(data, data + length)};
}

void EmitVexStub(StubBodyBuilder& body, const VexStub& stub) {
    const auto* data = stub.Original.data();
    const auto length = stub.Original.size();
    const auto p = *Parse(data, length);
    const std::string name(stub.Name);
    if (InPlaceShape(p)) {
        std::int32_t shrink = 0;
        EmitLegacy(body, LegacyBytes(data, length, p, shrink), name);
        return;
    }
    const std::optional<std::uint8_t> imm = HasImmediate(p.Map, p.Op) ? std::optional<std::uint8_t>(data[length - 1]) : std::nullopt;
    if (p.Kind.Kind == Shape::ShiftImm) {
        body.Raw(Movaps(p.Vvvv, p.Rm));
        body.Raw(RegisterForm(p, p.Op, static_cast<std::uint8_t>(p.Modrm >> 3 & 7), p.Vvvv, imm));
        return;
    }
    auto dst = p.Reg;
    auto source = p.Rm;
    auto opcode = p.Op;
    if (p.Kind.Kind == Shape::ScalarStore) {
        dst = p.Rm;
        source = p.Reg;
        opcode = 0x10;
    }
    const auto src1 = p.Kind.Kind == Shape::Unary ? dst : p.Vvvv;
    if (p.Memory && !p.Kind.Aligned) {
        if (dst != src1) body.Raw(Movaps(dst, src1));
        Parsed merged = p;
        std::int32_t shrink = 0;
        EmitLegacy(body, LegacyBytes(data, length, merged, shrink), name);
        return;
    }
    std::optional<std::uint8_t> loaded;
    if (p.Memory) {
        const auto scratch = Scratch({dst, src1});
        std::vector<std::uint8_t> prefixes(data, data + p.Vex);
        const auto memory = DecodeMemoryOperand(data, length, p.OpIndex + 1, static_cast<std::uint8_t>(p.X << 1 | p.B), prefixes);
        body.Spill(scratch);
        body.Load(scratch, memory);
        loaded = scratch;
        source = scratch;
    }
    if (p.Kind.Kind == Shape::Unary || dst == src1) {
        body.Raw(RegisterForm(p, opcode, dst, source, imm));
    } else if (p.Kind.GprSource || dst != source) {
        body.Raw(Movaps(dst, src1));
        body.Raw(RegisterForm(p, opcode, dst, source, imm));
    } else if (p.Kind.Commutative) {
        body.Raw(RegisterForm(p, opcode, dst, src1, imm));
    } else {
        const auto scratch = Scratch({dst, src1, source});
        body.Spill(scratch);
        body.Raw(Movaps(scratch, src1));
        body.Raw(RegisterForm(p, opcode, scratch, source, imm));
        body.Raw(Movaps(dst, scratch));
        body.Restore(scratch);
    }
    if (loaded) body.Restore(*loaded);
}

}
