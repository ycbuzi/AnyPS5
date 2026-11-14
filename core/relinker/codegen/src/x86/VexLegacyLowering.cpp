#include <codegen/x86/VexLegacyLowering.hpp>
#include <codegen/x86/FmaLowering.hpp>
#include <codegen/x86/ScalarLowering.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/CodegenException.hpp>
#include <algorithm>
#include <array>
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
        if (op == 0xC4) return {Shape::Nds, false, false, false, true};
        if (op == 0xC5) return Unary(false);
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
    return map == 3 || (map == 1 && (op == 0x70 || In(op, 0x71, 0x73) || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6));
}

bool Lane256(const std::uint8_t map, const std::uint8_t pp, const std::uint8_t op, const Form& form) {
    if (form.Kind == Shape::ScalarMove || form.Kind == Shape::ScalarStore || form.GprSource || form.Wide) return false;
    if (map == 1) {
        if (pp >= 2 && (op == 0x51 || op == 0x58 || op == 0x59 || op == 0x5A || (op >= 0x5C && op <= 0x5F) || op == 0xC2)) return false;
        if (op == 0x5A || op == 0xE6 || op == 0x50 || op == 0xD7 || op == 0x2E || op == 0x2F || op == 0xD6) return false;
        if ((op == 0x12 || op == 0x13 || op == 0x16 || op == 0x17) && pp < 2) return false;
        if (op == 0xF0 || op == 0xC5) return false;
        return true;
    }
    if (map == 2) return !(op == 0x17 || (op >= 0x20 && op <= 0x25) || (op >= 0x30 && op <= 0x35) || op == 0x41 || op >= 0xDB);
    if (map == 3) return op == 0x08 || op == 0x09 || (op >= 0x0C && op <= 0x0F) || op == 0x40 || op == 0x42;
    return false;
}

bool ShiftCount(const std::uint8_t map, const std::uint8_t pp, const std::uint8_t op) {
    return map == 1 && pp == 1 && (op == 0xD1 || op == 0xD2 || op == 0xD3 || op == 0xE1 || op == 0xE2 || op == 0xF1 || op == 0xF2 || op == 0xF3);
}

struct Parsed {
    std::size_t Vex;
    std::size_t OpIndex;
    std::uint8_t R, X, B, Map, Pp, Op, Modrm, Vvvv;
    bool W;
    bool L;
    bool Memory;
    std::uint8_t Reg, Rm;
    Form Kind;
};

std::optional<Parsed> Parse(const std::uint8_t* data, const std::size_t length, const bool table = true) {
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
    p.L = (last & 4) != 0;
    if (p.Map == 1 && p.Op == 0x77) {
        if (p.Pp != 0 || p.Vvvv != 0 || p.OpIndex + 1 != length) return std::nullopt;
        return p;
    }
    if (p.OpIndex + 2 > length) return std::nullopt;
    p.Modrm = data[p.OpIndex + 1];
    p.Memory = (p.Modrm >> 6) != 3;
    p.Reg = static_cast<std::uint8_t>(p.R << 3 | (p.Modrm >> 3 & 7));
    p.Rm = static_cast<std::uint8_t>(p.B << 3 | (p.Modrm & 7));
    if (!table) return p;
    if (p.Map == 3 && p.Pp == 1 && p.Op == 0x02 && !p.W) p.Op = 0x0C;
    p.Kind = Lookup(p.Map, p.Pp, p.Op);
    if (p.Kind.Kind == Shape::None) return std::nullopt;
    if (p.L && !Lane256(p.Map, p.Pp, p.Op, p.Kind)) return std::nullopt;
    if (p.Kind.Predicate && data[length - 1] >= 8) return std::nullopt;
    if (p.Kind.Kind == Shape::Unary && p.Vvvv != 0) return std::nullopt;
    if (p.Kind.Kind == Shape::ShiftImm && p.Memory) return std::nullopt;
    if ((p.Kind.Kind == Shape::ScalarMove || p.Kind.Kind == Shape::ScalarStore) && p.Memory && p.Vvvv != 0) return std::nullopt;
    return p;
}

bool ZeroUpper(const Parsed& p) { return p.Map == 1 && p.Op == 0x77 && !p.L; }

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
    legacy[body] = p.Op;
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


constexpr const char* kName256 = "VEX.256";
constexpr const char* kZeroAll = "VZEROALL";

std::vector<std::uint8_t> FsVector(const std::uint8_t opcode, const std::uint8_t reg, const std::int32_t displacement) {
    std::vector<std::uint8_t> bytes{0x64, 0xF3};
    if (reg & 8) bytes.push_back(0x44);
    bytes.insert(bytes.end(), {0x0F, opcode, static_cast<std::uint8_t>((reg & 7) << 3 | 4), 0x25});
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(displacement) >> shift));
    return bytes;
}

std::int32_t UpperSlot(const std::int32_t shadow, const std::uint8_t reg) { return shadow + 16 * reg; }

void LoadUpper(StubBodyBuilder& body, const std::uint8_t into, const std::int32_t shadow, const std::uint8_t reg) { body.Raw(FsVector(0x6F, into, UpperSlot(shadow, reg))); }
void StoreUpper(StubBodyBuilder& body, const std::uint8_t from, const std::int32_t shadow, const std::uint8_t reg) { body.Raw(FsVector(0x7F, from, UpperSlot(shadow, reg))); }

void ClearUpper(StubBodyBuilder& body, const std::int32_t shadow, const std::uint8_t reg) {
    for (const std::int32_t half : {0, 8}) {
        std::vector<std::uint8_t> bytes{0x64, 0x48, 0xC7, 0x04, 0x25};
        const auto displacement = static_cast<std::uint32_t>(UpperSlot(shadow, reg) + half);
        for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(displacement >> shift));
        bytes.insert(bytes.end(), {0, 0, 0, 0});
        body.Raw(bytes);
    }
}

std::vector<std::uint8_t> Xorps(const std::uint8_t reg) {
    std::vector<std::uint8_t> bytes;
    if (reg & 8) bytes.push_back(0x45);
    bytes.insert(bytes.end(), {0x0F, 0x57, static_cast<std::uint8_t>(0xC0 | (reg & 7) << 3 | (reg & 7))});
    return bytes;
}

std::vector<std::uint8_t> RegisterOp(const std::uint8_t prefix, std::initializer_list<std::uint8_t> opcode, const std::uint8_t reg, const std::uint8_t rm, const std::optional<std::uint8_t> imm = std::nullopt) {
    std::vector<std::uint8_t> bytes;
    if (prefix) bytes.push_back(prefix);
    if ((reg | rm) & 8) bytes.push_back(static_cast<std::uint8_t>(0x40 | (reg >> 3) << 2 | rm >> 3));
    bytes.insert(bytes.end(), opcode);
    bytes.push_back(static_cast<std::uint8_t>(0xC0 | (reg & 7) << 3 | (rm & 7)));
    if (imm) bytes.push_back(*imm);
    return bytes;
}

std::vector<std::uint8_t> MemoryOp(const std::uint8_t* data, const std::size_t length, const Parsed& p, const std::uint8_t prefix, std::initializer_list<std::uint8_t> opcode, const std::uint8_t reg) {
    std::vector<std::uint8_t> prefixes(data, data + p.Vex);
    const auto memory = DecodeMemoryOperand(data, length, p.OpIndex + 1, static_cast<std::uint8_t>(p.X << 1 | p.B), prefixes);
    std::vector<std::uint8_t> bytes(prefixes);
    if (prefix) bytes.push_back(prefix);
    const auto rex = static_cast<std::uint8_t>((reg >> 3) << 2 | p.X << 1 | p.B);
    if (rex) bytes.push_back(static_cast<std::uint8_t>(0x40 | rex));
    bytes.insert(bytes.end(), opcode);
    const auto modrmAt = bytes.size();
    bytes.insert(bytes.end(), data + p.OpIndex + 1, data + p.OpIndex + 1 + memory.EncodedSize);
    bytes[modrmAt] = static_cast<std::uint8_t>((bytes[modrmAt] & 0xC7) | (reg & 7) << 3);
    if (memory.RipRelative) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + modrmAt + 1, sizeof(displacement));
        displacement += static_cast<std::int32_t>(length) - static_cast<std::int32_t>(bytes.size());
        std::memcpy(bytes.data() + modrmAt + 1, &displacement, sizeof(displacement));
    }
    return bytes;
}

MemoryOperand Operand(const std::uint8_t* data, const std::size_t length, const Parsed& p) {
    return DecodeMemoryOperand(data, length, p.OpIndex + 1, static_cast<std::uint8_t>(p.X << 1 | p.B), std::vector<std::uint8_t>(data, data + p.Vex));
}

std::optional<std::uint8_t> XmmDestination(const Parsed& p) {
    switch (p.Kind.Kind) {
    case Shape::ShiftImm: return p.Vvvv;
    case Shape::ScalarStore: return p.Memory ? std::nullopt : std::optional<std::uint8_t>(p.Rm);
    case Shape::ScalarMove:
    case Shape::Nds: return p.Reg;
    case Shape::None: return std::nullopt;
    case Shape::Unary: break;
    }
    if (p.Map == 1) {
        if ((p.Op == 0x11 && p.Pp < 2) || p.Op == 0x29 || p.Op == 0x7F || p.Op == 0xD6 || p.Op == 0x13 || p.Op == 0x17 || p.Op == 0x2B || p.Op == 0xE7)
            return p.Memory ? std::nullopt : std::optional<std::uint8_t>(p.Rm);
        if (p.Op == 0x2C || p.Op == 0x2D || p.Op == 0x2E || p.Op == 0x2F || p.Op == 0x50 || p.Op == 0xD7 || p.Op == 0xC5 || (p.Op == 0x7E && p.Pp == 1)) return std::nullopt;
    }
    if (p.Map == 2 && p.Op == 0x17) return std::nullopt;
    if (p.Map == 3) {
        if (p.Op >= 0x14 && p.Op <= 0x17) return std::nullopt;
        if (p.Op == 0x60 || p.Op == 0x62) return std::uint8_t{0};
        if (p.Op == 0x61 || p.Op == 0x63) return std::nullopt;
    }
    return p.Reg;
}

bool StoreDirection(const Parsed& p) {
    return p.Map == 1 && ((p.Op == 0x11 && p.Pp < 2) || p.Op == 0x29 || (p.Op == 0x7F && p.Pp != 0) || p.Op == 0x2B || p.Op == 0xE7);
}

bool PlainMove(const Parsed& p) {
    return (p.Map == 1 && (((p.Op == 0x10 || p.Op == 0x28) && p.Pp < 2) || (p.Op == 0x6F && p.Pp != 0))) || (p.Map == 2 && p.Op == 0x2A);
}

std::vector<std::uint8_t> LowHalf(const std::uint8_t* data, const std::size_t length, const Parsed& p) {
    std::vector<std::uint8_t> bytes(data, data + length);
    bytes[p.Vex + (data[p.Vex] == 0xC4 ? 2 : 1)] &= static_cast<std::uint8_t>(~4);
    return bytes;
}

std::uint8_t HighImmediate(const Parsed& p, const std::uint8_t imm) {
    if (p.Map == 3 && p.Op == 0x0C) return static_cast<std::uint8_t>(imm >> 4);
    if (p.Map == 3 && p.Op == 0x0D) return static_cast<std::uint8_t>(imm >> 2);
    if (p.Map == 3 && p.Op == 0x42) return static_cast<std::uint8_t>(imm >> 3);
    if (p.Map == 1 && p.Pp == 1 && p.Op == 0xC6) return static_cast<std::uint8_t>(imm >> 2);
    return imm;
}

void EmitHigh(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& p, const std::int32_t shadow) {
    const std::optional<std::uint8_t> imm = HasImmediate(p.Map, p.Op) ? std::optional<std::uint8_t>(HighImmediate(p, data[length - 1])) : std::nullopt;
    if (StoreDirection(p)) {
        const auto scratch = Scratch({});
        body.Spill(scratch);
        LoadUpper(body, scratch, shadow, p.Reg);
        if (p.Memory) body.Store(scratch, OffsetMemoryOperand(Operand(data, length, p), 16));
        else StoreUpper(body, scratch, shadow, p.Rm);
        body.Restore(scratch);
        return;
    }
    const auto first = Scratch({p.Memory ? std::uint8_t{16} : p.Rm});
    const auto second = Scratch({first, p.Memory ? std::uint8_t{16} : p.Rm});
    body.Spill(first);
    body.Spill(second);
    const auto memory = p.Memory ? std::optional<MemoryOperand>(Operand(data, length, p)) : std::nullopt;
    const auto sourceUpper = [&](const std::uint8_t into) {
        if (memory) body.Load(into, OffsetMemoryOperand(*memory, 16));
        else LoadUpper(body, into, shadow, p.Rm);
    };
    if (PlainMove(p)) {
        sourceUpper(first);
        StoreUpper(body, first, shadow, p.Reg);
    } else if (p.Kind.Kind == Shape::ShiftImm) {
        LoadUpper(body, first, shadow, p.Rm);
        body.Raw(RegisterForm(p, p.Op, static_cast<std::uint8_t>(p.Modrm >> 3 & 7), first, imm));
        StoreUpper(body, first, shadow, p.Vvvv);
    } else if (p.Kind.Kind == Shape::Unary) {
        sourceUpper(second);
        body.Raw(RegisterForm(p, p.Op, first, second, imm));
        StoreUpper(body, first, shadow, p.Reg);
    } else {
        LoadUpper(body, first, shadow, p.Vvvv);
        if (ShiftCount(p.Map, p.Pp, p.Op)) {
            if (memory) body.Load(second, *memory);
            else body.Raw(Movaps(second, p.Rm));
        } else {
            sourceUpper(second);
        }
        body.Raw(RegisterForm(p, p.Op, first, second, imm));
        StoreUpper(body, first, shadow, p.Reg);
    }
    body.Restore(second);
    body.Restore(first);
}

enum class Special : std::uint8_t { None, Insert, Extract, Permute2, PermuteQ, PermuteD, PermuteImm, BlendV, Extend, HalfToSingle, SingleToHalf, VariableShift, BroadcastSs, BroadcastSd, Broadcast128, BroadcastD, BroadcastQ };

constexpr StubConstant kDwordByteRepeat{0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
constexpr StubConstant kDwordByteOffsets{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};

Special SpecialOf(const Parsed& h, const std::uint8_t* data, const std::size_t length) {
    if (h.Pp != 1) return Special::None;
    if (h.Map == 3 && h.L && h.W && h.Vvvv == 0 && (h.Op == 0x00 || h.Op == 0x01)) return Special::PermuteQ;
    if (h.Map == 2 && h.L && !h.W && (h.Op == 0x36 || h.Op == 0x16)) return Special::PermuteD;
    if (h.Map == 2 && (h.Op == 0x45 || h.Op == 0x47 || (h.Op == 0x46 && !h.W))) return Special::VariableShift;
    if (h.Map == 3 && !h.W && h.Vvvv == 0 && (h.Op == 0x04 || h.Op == 0x05)) return Special::PermuteImm;
    if (h.W) return Special::None;
    if (h.Map == 2 && h.Op == 0x13 && !h.L && h.Vvvv == 0 && !h.Memory) return Special::HalfToSingle;
    if (h.Map == 3 && h.Op == 0x1D && !h.L && h.Vvvv == 0 && !h.Memory && (data[length - 1] & 4) == 0 && (data[length - 1] & 3) % 3 == 0) return Special::SingleToHalf;
    if (h.Map == 3 && (h.Op == 0x4A || h.Op == 0x4B || h.Op == 0x4C)) return Special::BlendV;
    if (h.Map == 3 && h.L && (h.Op == 0x18 || h.Op == 0x38)) return Special::Insert;
    if (h.Map == 3 && h.L && (h.Op == 0x19 || h.Op == 0x39)) return Special::Extract;
    if (h.Map == 3 && h.L && (h.Op == 0x06 || h.Op == 0x46)) return Special::Permute2;
    if (h.Map != 2 || h.Vvvv != 0) return Special::None;
    if (h.L && (In(h.Op, 0x20, 0x25) || In(h.Op, 0x30, 0x35))) return Special::Extend;
    if (h.Op == 0x18) return Special::BroadcastSs;
    if (h.Op == 0x19 && h.L) return Special::BroadcastSd;
    if ((h.Op == 0x1A || h.Op == 0x5A) && h.L && h.Memory) return Special::Broadcast128;
    if (h.Op == 0x58) return Special::BroadcastD;
    if (h.Op == 0x59) return Special::BroadcastQ;
    return Special::None;
}

void LoadHalves(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& h, const std::uint8_t low, const std::uint8_t high, const std::int32_t shadow) {
    if (h.Memory) {
        const auto memory = Operand(data, length, h);
        body.Load(low, memory);
        body.Load(high, OffsetMemoryOperand(memory, 16));
        return;
    }
    body.Raw(Movaps(low, h.Rm));
    LoadUpper(body, high, shadow, h.Rm);
}

std::uint8_t QwordShuffle(const unsigned first, const unsigned second) {
    return static_cast<std::uint8_t>(2 * first | (2 * first + 1) << 2 | (2 * second) << 4 | (2 * second + 1) << 6);
}

constexpr StubConstant Splat(const std::uint32_t value) {
    StubConstant constant{};
    for (std::size_t index = 0; index < constant.size(); ++index) constant[index] = static_cast<std::uint8_t>(value >> (8 * (index % 4)));
    return constant;
}

template <std::size_t N>
std::array<std::uint8_t, N> Scratches(std::initializer_list<std::uint8_t> excluded) {
    std::array<std::uint8_t, N> scratch{};
    std::uint8_t candidate = 0;
    for (std::size_t n = 0; n < N; ++n, ++candidate) {
        while (candidate < 16 && std::find(excluded.begin(), excluded.end(), candidate) != excluded.end()) ++candidate;
        if (candidate >= 16) throw CodegenException("No scratch XMM registers for a VEX stub");
        scratch[n] = candidate;
    }
    return scratch;
}

void EmitBlendV(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& h, const std::optional<std::int32_t> shadow) {
    const auto dst = h.Reg;
    const auto first = h.Vvvv;
    const auto selector = static_cast<std::uint8_t>(data[length - 1] >> 4);
    const auto second = h.Memory ? first : h.Rm;
    const auto [out, picked, upperFirst, upperSelector] = Scratches<4>({dst, first, second, selector});
    const auto memory = h.Memory ? std::optional<MemoryOperand>(Operand(data, length, h)) : std::nullopt;
    const auto half = [&](const std::uint8_t one, const std::uint8_t mask, const bool upper) {
        if (h.Op == 0x4C) {
            body.Raw(Xorps(out));
            body.Sse(0x66, {0x0F, 0x64}, out, mask);
        } else {
            body.Raw(Movaps(out, mask));
            body.ShiftDwordImm(4, out, 31);
            if (h.Op == 0x4B) body.Raw(RegisterOp(0x66, {0x0F, 0x70}, out, out, std::uint8_t{0xF5}));
        }
        if (memory) body.Load(picked, upper ? OffsetMemoryOperand(*memory, 16) : *memory);
        else if (upper) LoadUpper(body, picked, *shadow, second);
        else body.Raw(Movaps(picked, second));
        body.Sse(0x66, {0x0F, 0xDB}, picked, out);
        body.Sse(0x66, {0x0F, 0xDF}, out, one);
        body.Sse(0x66, {0x0F, 0xEB}, out, picked);
    };
    body.Spill(out);
    body.Spill(picked);
    if (h.L) {
        body.Spill(upperFirst);
        body.Spill(upperSelector);
        LoadUpper(body, upperFirst, *shadow, first);
        LoadUpper(body, upperSelector, *shadow, selector);
        half(upperFirst, upperSelector, true);
        StoreUpper(body, out, *shadow, dst);
        body.Restore(upperSelector);
        body.Restore(upperFirst);
    }
    half(first, selector, false);
    body.Raw(Movaps(dst, out));
    body.Restore(picked);
    body.Restore(out);
    if (!h.L && shadow) ClearUpper(body, *shadow, dst);
}

void EmitHalfToSingle(StubBodyBuilder& body, const Parsed& h, const std::optional<std::int32_t> shadow) {
    const auto dst = h.Reg;
    const auto scratch = Scratches<5>({dst, h.Rm});
    const auto [words, magnitude, normal, small, out] = scratch;
    for (const auto reg : scratch) body.Spill(reg);
    body.Sse(0x66, {0x0F, 0x38, 0x33}, words, h.Rm);
    body.Raw(Movaps(magnitude, words));
    body.RipOperand({0x0F, 0xDB}, magnitude, Splat(0x7FFF));
    body.Raw(Movaps(normal, magnitude));
    body.ShiftDwordImm(6, normal, 13);
    body.RipOperand({0x0F, 0xFE}, normal, Splat(0x38000000));
    body.SsePlain({0x0F, 0x5B}, small, magnitude);
    body.RipOperand({0x0F, 0x6F}, out, Splat(0x33800000));
    body.SsePlain({0x0F, 0x59}, small, out);
    body.RipOperand({0x0F, 0x6F}, out, Splat(0x400));
    body.Sse(0x66, {0x0F, 0x66}, out, magnitude);
    body.Sse(0x66, {0x0F, 0xDB}, small, out);
    body.Sse(0x66, {0x0F, 0xDF}, out, normal);
    body.Sse(0x66, {0x0F, 0xEB}, out, small);
    body.Raw(Movaps(normal, magnitude));
    body.RipOperand({0x0F, 0x66}, normal, Splat(0x7BFF));
    body.RipOperand({0x0F, 0xDB}, normal, Splat(0x38000000));
    body.Sse(0x66, {0x0F, 0xFE}, out, normal);
    body.RipOperand({0x0F, 0x66}, magnitude, Splat(0x7C00));
    body.RipOperand({0x0F, 0xDB}, magnitude, Splat(0x00400000));
    body.Sse(0x66, {0x0F, 0xEB}, out, magnitude);
    body.ShiftDwordImm(6, words, 16);
    body.RipOperand({0x0F, 0xDB}, words, Splat(0x80000000));
    body.Sse(0x66, {0x0F, 0xEB}, out, words);
    body.Raw(Movaps(dst, out));
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg) body.Restore(*reg);
    if (shadow) ClearUpper(body, *shadow, dst);
}

void EmitSingleToHalf(StubBodyBuilder& body, const Parsed& h, const std::uint8_t imm, const std::optional<std::int32_t> shadow) {
    const auto dst = h.Rm;
    const bool truncate = (imm & 3) == 3;
    const auto scratch = Scratches<6>({dst, h.Reg});
    const auto [magnitude, sign, normal, small, select, over] = scratch;
    for (const auto reg : scratch) body.Spill(reg);
    body.Raw(Movaps(magnitude, h.Reg));
    body.Raw(Movaps(sign, h.Reg));
    body.RipOperand({0x0F, 0xDB}, magnitude, Splat(0x7FFFFFFF));
    body.ShiftDwordImm(2, sign, 31);
    body.ShiftDwordImm(6, sign, 15);
    body.Raw(Movaps(normal, magnitude));
    if (truncate) {
        body.RipOperand({0x0F, 0xFE}, normal, Splat(0xC8000000));
    } else {
        body.ShiftDwordImm(2, normal, 13);
        body.RipOperand({0x0F, 0xDB}, normal, Splat(1));
        body.Sse(0x66, {0x0F, 0xFE}, normal, magnitude);
        body.RipOperand({0x0F, 0xFE}, normal, Splat(0xC8000FFF));
    }
    body.ShiftDwordImm(2, normal, 13);
    body.RipOperand({0x0F, 0x6F}, small, Splat(0x4B800000));
    body.SsePlain({0x0F, 0x59}, small, magnitude);
    if (truncate) body.Sse(0xF3, {0x0F, 0x5B}, small, small);
    else body.Sse(0x66, {0x0F, 0x5B}, small, small);
    body.RipOperand({0x0F, 0x6F}, select, Splat(0x38800000));
    body.Sse(0x66, {0x0F, 0x66}, select, magnitude);
    body.Sse(0x66, {0x0F, 0xDB}, small, select);
    body.Sse(0x66, {0x0F, 0xDF}, select, normal);
    body.Sse(0x66, {0x0F, 0xEB}, select, small);
    body.Raw(Movaps(over, magnitude));
    body.RipOperand({0x0F, 0x66}, over, Splat(0x477FFFFF));
    if (truncate) {
        body.Raw(Movaps(normal, magnitude));
        body.RipOperand({0x0F, 0x76}, normal, Splat(0x7F800000));
        body.RipOperand({0x0F, 0x6F}, small, Splat(0x7BFF));
        body.Sse(0x66, {0x0F, 0xFA}, small, normal);
    } else {
        body.RipOperand({0x0F, 0x6F}, small, Splat(0x7C00));
    }
    body.Sse(0x66, {0x0F, 0xDB}, small, over);
    body.Sse(0x66, {0x0F, 0xDF}, over, select);
    body.Sse(0x66, {0x0F, 0xEB}, over, small);
    body.Raw(Movaps(select, magnitude));
    body.RipOperand({0x0F, 0x66}, select, Splat(0x7F800000));
    body.ShiftDwordImm(2, magnitude, 13);
    body.RipOperand({0x0F, 0xDB}, magnitude, Splat(0x3FF));
    body.RipOperand({0x0F, 0xEB}, magnitude, Splat(0x7E00));
    body.Sse(0x66, {0x0F, 0xDB}, magnitude, select);
    body.Sse(0x66, {0x0F, 0xDF}, select, over);
    body.Sse(0x66, {0x0F, 0xEB}, select, magnitude);
    body.Sse(0x66, {0x0F, 0xEB}, select, sign);
    body.Raw(Xorps(magnitude));
    body.Sse(0x66, {0x0F, 0x38, 0x2B}, select, magnitude);
    body.Raw(Movaps(dst, select));
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg) body.Restore(*reg);
    if (shadow) ClearUpper(body, *shadow, dst);
}

void EmitVariableShift(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& h, const std::optional<std::int32_t> shadow) {
    const auto dst = h.Reg;
    const auto source = h.Vvvv;
    const bool dwords = !h.W;
    const std::uint8_t opcode = h.Op == 0x47 ? (dwords ? 0xF2 : 0xF3) : h.Op == 0x46 ? 0xE2 : (dwords ? 0xD2 : 0xD3);
    const auto counts = h.Memory ? source : h.Rm;
    const auto [result, lane, count, upperSource, loaded] = Scratches<5>({dst, source, counts});
    const auto memory = h.Memory ? std::optional<MemoryOperand>(Operand(data, length, h)) : std::nullopt;
    const auto half = [&](const std::uint8_t from, const std::uint8_t by) {
        const unsigned lanes = dwords ? 4 : 2;
        for (unsigned index = 0; index < lanes; ++index) {
            body.Raw(Movaps(count, by));
            if (index) body.ShiftImm(3, count, static_cast<std::uint8_t>((dwords ? 4 : 8) * index));
            if (dwords) body.Sse(0x66, {0x0F, 0x38, 0x35}, count, count);
            const auto into = index ? lane : result;
            body.Raw(Movaps(into, from));
            body.Sse(0x66, {0x0F, opcode}, into, count);
            if (index) body.SseImm(0x66, {0x0F, 0x3A, 0x0E}, result, lane, static_cast<std::uint8_t>(dwords ? 3u << (2 * index) : 0xFu << (4 * index)));
        }
    };
    for (const auto reg : {result, lane, count, upperSource, loaded}) body.Spill(reg);
    if (h.L) {
        LoadUpper(body, upperSource, *shadow, source);
        if (memory) body.Load(loaded, OffsetMemoryOperand(*memory, 16));
        else LoadUpper(body, loaded, *shadow, counts);
        half(upperSource, loaded);
        StoreUpper(body, result, *shadow, dst);
    }
    if (memory) body.Load(loaded, *memory);
    half(source, memory ? loaded : counts);
    body.Raw(Movaps(dst, result));
    for (const auto reg : {loaded, upperSource, count, lane, result}) body.Restore(reg);
    if (!h.L && shadow) ClearUpper(body, *shadow, dst);
}

void EmitPermuteD(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& h, const std::int32_t shadow) {
    const auto dst = h.Reg;
    const auto index = h.Vvvv;
    const auto scratch = Scratches<6>({dst, index});
    const auto [low, high, out, control, left, right] = scratch;
    for (const auto reg : scratch) body.Spill(reg);
    LoadHalves(body, data, length, h, low, high, shadow);
    for (const bool upper : {true, false}) {
        if (upper) LoadUpper(body, out, shadow, index);
        else body.Raw(Movaps(out, index));
        body.Raw(Movaps(control, out));
        body.ShiftDwordImm(6, control, 30);
        body.ShiftDwordImm(2, control, 28);
        body.RipOperand({0x0F, 0x38, 0x00}, control, kDwordByteRepeat);
        body.RipOperand({0x0F, 0xEB}, control, kDwordByteOffsets);
        body.Raw(Movaps(left, low));
        body.Sse(0x66, {0x0F, 0x38, 0x00}, left, control);
        body.Raw(Movaps(right, high));
        body.Sse(0x66, {0x0F, 0x38, 0x00}, right, control);
        body.ShiftDwordImm(6, out, 29);
        body.ShiftDwordImm(4, out, 31);
        body.Sse(0x66, {0x0F, 0xDB}, right, out);
        body.Sse(0x66, {0x0F, 0xDF}, out, left);
        body.Sse(0x66, {0x0F, 0xEB}, out, right);
        if (upper) StoreUpper(body, out, shadow, dst);
        else body.Raw(Movaps(dst, out));
    }
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg) body.Restore(*reg);
}

void EmitSpecial(StubBodyBuilder& body, const std::uint8_t* data, const std::size_t length, const Parsed& h, const Special kind, const std::optional<std::int32_t> shadow) {
    const auto dst = h.Reg;
    const auto settle = [&](const std::uint8_t reg) {
        if (!shadow) return;
        if (h.L) StoreUpper(body, reg, *shadow, reg);
        else ClearUpper(body, *shadow, reg);
    };
    switch (kind) {
    case Special::BroadcastSs:
    case Special::BroadcastD:
        if (h.Memory) EmitLegacy(body, MemoryOp(data, length, h, 0x66, {0x0F, 0x6E}, dst), kName);
        else if (dst != h.Rm) body.Raw(Movaps(dst, h.Rm));
        body.Raw(RegisterOp(0x66, {0x0F, 0x70}, dst, dst, std::uint8_t{0}));
        settle(dst);
        return;
    case Special::BroadcastSd:
    case Special::BroadcastQ:
        if (h.Memory) EmitLegacy(body, MemoryOp(data, length, h, 0xF3, {0x0F, 0x7E}, dst), kName);
        else body.Raw(RegisterOp(0xF3, {0x0F, 0x7E}, dst, h.Rm));
        body.Raw(RegisterOp(0x66, {0x0F, 0x6C}, dst, dst));
        settle(dst);
        return;
    case Special::Broadcast128:
        EmitLegacy(body, MemoryOp(data, length, h, 0, {0x0F, 0x10}, dst), kName);
        settle(dst);
        return;
    case Special::Insert: {
        const auto imm = data[length - 1];
        const auto src1 = h.Vvvv;
        if (imm & 1) {
            if (h.Memory) {
                const auto scratch = Scratch({});
                body.Spill(scratch);
                body.Load(scratch, Operand(data, length, h));
                StoreUpper(body, scratch, *shadow, dst);
                body.Restore(scratch);
            } else {
                StoreUpper(body, h.Rm, *shadow, dst);
            }
            if (dst != src1) body.Raw(Movaps(dst, src1));
        } else {
            if (dst != src1) {
                const auto scratch = Scratch({});
                body.Spill(scratch);
                LoadUpper(body, scratch, *shadow, src1);
                StoreUpper(body, scratch, *shadow, dst);
                body.Restore(scratch);
            }
            if (h.Memory) EmitLegacy(body, MemoryOp(data, length, h, 0, {0x0F, 0x10}, dst), kName);
            else if (dst != h.Rm) body.Raw(Movaps(dst, h.Rm));
        }
        return;
    }
    case Special::Extract: {
        const auto imm = data[length - 1];
        const auto source = h.Reg;
        if (h.Memory) {
            if (imm & 1) {
                const auto scratch = Scratch({});
                body.Spill(scratch);
                LoadUpper(body, scratch, *shadow, source);
                body.Store(scratch, Operand(data, length, h));
                body.Restore(scratch);
            } else {
                EmitLegacy(body, MemoryOp(data, length, h, 0, {0x0F, 0x11}, source), kName);
            }
            return;
        }
        if (imm & 1) LoadUpper(body, h.Rm, *shadow, source);
        else if (h.Rm != source) body.Raw(Movaps(h.Rm, source));
        ClearUpper(body, *shadow, h.Rm);
        return;
    }
    case Special::Permute2: {
        const auto imm = data[length - 1];
        const auto first = Scratch({dst});
        const auto second = Scratch({dst, first});
        const auto memory = h.Memory ? std::optional<MemoryOperand>(Operand(data, length, h)) : std::nullopt;
        body.Spill(first);
        body.Spill(second);
        for (const auto [into, selector] : {std::pair{first, static_cast<std::uint8_t>(imm & 0xF)}, std::pair{second, static_cast<std::uint8_t>(imm >> 4)}}) {
            if (selector & 8) { body.Raw(Xorps(into)); continue; }
            switch (selector & 3) {
            case 0: body.Raw(Movaps(into, h.Vvvv)); break;
            case 1: LoadUpper(body, into, *shadow, h.Vvvv); break;
            case 2: if (memory) body.Load(into, *memory); else body.Raw(Movaps(into, h.Rm)); break;
            default: if (memory) body.Load(into, OffsetMemoryOperand(*memory, 16)); else LoadUpper(body, into, *shadow, h.Rm); break;
            }
        }
        StoreUpper(body, second, *shadow, dst);
        body.Raw(Movaps(dst, first));
        body.Restore(second);
        body.Restore(first);
        return;
    }
    case Special::PermuteQ: {
        const auto imm = data[length - 1];
        const auto low = Scratch({dst});
        const auto high = Scratch({dst, low});
        const auto out = Scratch({dst, low, high});
        body.Spill(low);
        body.Spill(high);
        body.Spill(out);
        LoadHalves(body, data, length, h, low, high, *shadow);
        const auto half = [&](const std::uint8_t into, const unsigned shift) {
            const unsigned first = imm >> shift & 3;
            const unsigned second = imm >> (shift + 2) & 3;
            body.Raw(Movaps(into, (first & 2) ? high : low));
            body.Raw(RegisterOp(0x66, {0x0F, 0xC6}, into, (second & 2) ? high : low, static_cast<std::uint8_t>((first & 1) | (second & 1) << 1)));
        };
        half(out, 4);
        StoreUpper(body, out, *shadow, dst);
        half(dst, 0);
        body.Restore(out);
        body.Restore(high);
        body.Restore(low);
        return;
    }
    case Special::PermuteD:
        EmitPermuteD(body, data, length, h, *shadow);
        return;
    case Special::BlendV:
        EmitBlendV(body, data, length, h, shadow);
        return;
    case Special::VariableShift:
        EmitVariableShift(body, data, length, h, shadow);
        return;
    case Special::HalfToSingle:
        EmitHalfToSingle(body, h, shadow);
        return;
    case Special::SingleToHalf:
        EmitSingleToHalf(body, h, data[length - 1], shadow);
        return;
    case Special::Extend: {
        constexpr std::uint8_t kHighOffset[] = {8, 4, 2, 8, 4, 8};
        const auto offset = kHighOffset[h.Op & 7];
        const auto scratch = Scratch({dst, h.Rm});
        body.Spill(scratch);
        if (h.Memory) {
            body.MemoryOp(0x66, {0x0F, 0x38, h.Op}, scratch, OffsetMemoryOperand(Operand(data, length, h), offset));
        } else {
            body.Raw(Movaps(scratch, h.Rm));
            body.ShiftImm(3, scratch, offset);
            body.Sse(0x66, {0x0F, 0x38, h.Op}, scratch, scratch);
        }
        StoreUpper(body, scratch, *shadow, dst);
        body.Restore(scratch);
        if (h.Memory) EmitLegacy(body, MemoryOp(data, length, h, 0x66, {0x0F, 0x38, h.Op}, dst), kName);
        else body.Sse(0x66, {0x0F, 0x38, h.Op}, dst, h.Rm);
        return;
    }
    case Special::PermuteImm: {
        const auto imm = data[length - 1];
        const bool doubles = h.Op == 0x05;
        const auto lowImm = doubles ? QwordShuffle(imm & 1, imm >> 1 & 1) : imm;
        const auto highImm = doubles ? QwordShuffle(imm >> 2 & 1, imm >> 3 & 1) : imm;
        if (h.L) {
            const auto scratch = Scratch({dst});
            body.Spill(scratch);
            if (h.Memory) body.Load(scratch, OffsetMemoryOperand(Operand(data, length, h), 16));
            else LoadUpper(body, scratch, *shadow, h.Rm);
            body.Raw(RegisterOp(0x66, {0x0F, 0x70}, scratch, scratch, highImm));
            StoreUpper(body, scratch, *shadow, dst);
            body.Restore(scratch);
        }
        if (h.Memory) {
            body.Load(dst, Operand(data, length, h));
            body.Raw(RegisterOp(0x66, {0x0F, 0x70}, dst, dst, lowImm));
        } else {
            body.Raw(RegisterOp(0x66, {0x0F, 0x70}, dst, h.Rm, lowImm));
        }
        if (!h.L && shadow) ClearUpper(body, *shadow, dst);
        return;
    }
    case Special::None:
        return;
    }
}

}

std::optional<VexLegacy> DecodeVexLegacy(const std::uint8_t* data, const std::size_t length) {
    const auto p = Parse(data, length);
    if (!p || p->L) return std::nullopt;
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
    if (!p || p->L || ZeroUpper(*p)) return std::nullopt;
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

void EmitLow128(StubBodyBuilder& body, const std::vector<std::uint8_t>& bytes) {
    if (const auto legacy = DecodeVexLegacy(bytes.data(), bytes.size())) EmitVexLegacy(body, *legacy);
    else EmitVexStub(body, VexStub{kName, bytes});
}

std::optional<VexShadow> DecodeVexShadow(const std::uint8_t* data, const std::size_t length, const bool shadow) {
    const auto header = Parse(data, length, false);
    if (!header) return std::nullopt;
    if (header->Map == 1 && header->Op == 0x77 && header->Pp == 0 && header->Vvvv == 0 && header->OpIndex + 1 == length)
        return shadow ? std::optional<VexShadow>(VexShadow{header->L ? kZeroAll : kZeroUpper, std::vector<std::uint8_t>(data, data + length)}) : std::nullopt;
    if (header->OpIndex + 2 > length) return std::nullopt;
    const auto special = SpecialOf(*header, data, length);
    if (special != Special::None && (shadow || !header->L)) return VexShadow{header->L ? kName256 : kName, std::vector<std::uint8_t>(data, data + length)};
    const auto p = Parse(data, length);
    if (!p || (p->L && !shadow)) return std::nullopt;
    if (!shadow) return std::nullopt;
    return VexShadow{p->L ? kName256 : kName, std::vector<std::uint8_t>(data, data + length)};
}

void EmitVexShadow(StubBodyBuilder& body, const VexShadow& site, const std::optional<std::int32_t> shadow) {
    const auto* data = site.Original.data();
    const auto length = site.Original.size();
    const auto header = *Parse(data, length, false);
    if (header.Map == 1 && header.Op == 0x77) {
        for (std::uint8_t reg = 0; reg < 16; ++reg) {
            if (header.L) body.Raw(Xorps(reg));
            ClearUpper(body, *shadow, reg);
        }
        return;
    }
    if (const auto special = SpecialOf(header, data, length); special != Special::None) {
        EmitSpecial(body, data, length, header, special, shadow);
        return;
    }
    const auto p = *Parse(data, length);
    if (p.L) {
        EmitHigh(body, data, length, p, *shadow);
        EmitLow128(body, LowHalf(data, length, p));
        return;
    }
    EmitLow128(body, site.Original);
    if (const auto written = XmmDestination(p); written && shadow) ClearUpper(body, *shadow, *written);
}

std::optional<std::string> VexFormName(const std::uint8_t* data, const std::size_t length) {
    const auto header = Parse(data, length, false);
    if (!header) return std::nullopt;
    constexpr const char* kPrefix[] = {"NP", "66", "F3", "F2"};
    constexpr const char* kMap[] = {"?", "0F", "0F38", "0F3A"};
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string name = std::string("VEX.") + (header->L ? "256." : "128.") + kPrefix[header->Pp] + "." + (header->Map < 4 ? kMap[header->Map] : "?") + ".W" + (header->W ? "1 " : "0 ");
    name += kHex[header->Op >> 4];
    name += kHex[header->Op & 15];
    if (header->OpIndex + 1 < length && header->Memory) name += " mem";
    return name;
}

bool VexWide(const std::uint8_t* data, const std::size_t length) {
    std::size_t vex = 0;
    while (vex < length && ScalarLowering::LegacyPrefix(data[vex])) ++vex;
    if (vex + 3 > length || (data[vex] != 0xC4 && data[vex] != 0xC5)) return false;
    return (data[vex + (data[vex] == 0xC4 ? 2 : 1)] & 4) != 0;
}

namespace {

constexpr StubConstant kDoubleSigns{0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0x80};

struct FmaRegisters {
    std::uint8_t First;
    std::uint8_t Second;
    std::uint8_t Addend;
};

FmaRegisters FmaOrderOf(const FmaOperands& f, const std::uint8_t a, const std::uint8_t b, const std::uint8_t c) {
    switch (f.Order) {
    case FmaOrder::Order132: return {a, c, b};
    case FmaOrder::Order213: return {b, a, c};
    case FmaOrder::Order231: return {b, c, a};
    }
    return {a, c, b};
}

void FmaCombine(StubBodyBuilder& body, const FmaOperands& f, const bool scalar, const std::uint8_t product, const std::uint8_t addend, const bool addendWritable) {
    const std::uint8_t prefix = scalar ? 0xF2 : 0x66;
    if (f.Operation == FmaOperation::NegatedAdd || f.Operation == FmaOperation::NegatedSub) body.RipOperand({0x0F, 0x57}, product, kDoubleSigns);
    switch (f.Operation) {
    case FmaOperation::Add:
    case FmaOperation::NegatedAdd:
        body.Raw(RegisterOp(prefix, {0x0F, 0x58}, product, addend));
        return;
    case FmaOperation::Sub:
    case FmaOperation::NegatedSub:
        body.Raw(RegisterOp(prefix, {0x0F, 0x5C}, product, addend));
        return;
    case FmaOperation::AddSub:
        body.Raw(RegisterOp(0x66, {0x0F, 0xD0}, product, addend));
        return;
    case FmaOperation::SubAdd:
        if (!addendWritable) throw CodegenException(std::string(f.Name) + " needs a writable addend");
        body.RipOperand({0x0F, 0x57}, addend, kDoubleSigns);
        body.Raw(RegisterOp(0x66, {0x0F, 0xD0}, product, addend));
        return;
    }
}

void FmaHalf(StubBodyBuilder& body, const FmaOperands& f, const std::uint8_t a, const std::uint8_t b, const std::uint8_t c, const std::uint8_t out, const std::uint8_t t1, const std::uint8_t t2, const std::uint8_t t3) {
    const auto [first, second, addend] = FmaOrderOf(f, a, b, c);
    if (f.Scalar && !f.Double) {
        body.Raw(RegisterOp(0xF3, {0x0F, 0x5A}, t1, first));
        body.Raw(RegisterOp(0xF3, {0x0F, 0x5A}, t2, second));
        body.Raw(RegisterOp(0xF2, {0x0F, 0x59}, t1, t2));
        body.Raw(RegisterOp(0xF3, {0x0F, 0x5A}, t2, addend));
        FmaCombine(body, f, true, t1, t2, true);
        body.Raw(RegisterOp(0xF2, {0x0F, 0x5A}, t1, t1));
        body.Raw(RegisterOp(0xF3, {0x0F, 0x10}, out, t1));
        return;
    }
    if (f.Scalar) {
        body.Raw(Movaps(t1, first));
        body.Raw(RegisterOp(0xF2, {0x0F, 0x59}, t1, second));
        FmaCombine(body, f, true, t1, addend, false);
        body.Raw(RegisterOp(0xF2, {0x0F, 0x10}, out, t1));
        return;
    }
    if (f.Double) {
        body.Raw(Movaps(t1, first));
        body.Raw(RegisterOp(0x66, {0x0F, 0x59}, t1, second));
        if (f.Operation == FmaOperation::SubAdd) {
            body.Raw(Movaps(t2, addend));
            FmaCombine(body, f, false, t1, t2, true);
        } else {
            FmaCombine(body, f, false, t1, addend, false);
        }
        body.Raw(Movaps(out, t1));
        return;
    }
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t1, first));
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t2, second));
    body.Raw(RegisterOp(0x66, {0x0F, 0x59}, t1, t2));
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t2, addend));
    FmaCombine(body, f, false, t1, t2, true);
    body.Raw(RegisterOp(0x66, {0x0F, 0x5A}, t1, t1));
    body.Raw(RegisterOp(0x66, {0x0F, 0x70}, t2, first, std::uint8_t{0xEE}));
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t2, t2));
    body.Raw(RegisterOp(0x66, {0x0F, 0x70}, t3, second, std::uint8_t{0xEE}));
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t3, t3));
    body.Raw(RegisterOp(0x66, {0x0F, 0x59}, t2, t3));
    body.Raw(RegisterOp(0x66, {0x0F, 0x70}, t3, addend, std::uint8_t{0xEE}));
    body.Raw(RegisterOp(0, {0x0F, 0x5A}, t3, t3));
    FmaCombine(body, f, false, t2, t3, true);
    body.Raw(RegisterOp(0x66, {0x0F, 0x5A}, t2, t2));
    body.Raw(RegisterOp(0, {0x0F, 0x16}, t1, t2));
    body.Raw(Movaps(out, t1));
}

std::uint8_t ScratchAvoiding(std::initializer_list<std::uint8_t> used, std::initializer_list<std::uint8_t> more = {}) {
    for (std::uint8_t candidate = 15;; --candidate) {
        bool taken = false;
        for (const auto value : used) taken = taken || value == candidate;
        for (const auto value : more) taken = taken || value == candidate;
        if (!taken) return candidate;
        if (candidate == 0) break;
    }
    throw CodegenException("No scratch XMM register for an FMA stub");
}

}

std::optional<FmaOperands> DecodeFmaSse(const std::uint8_t* data, const std::size_t length, const bool shadow) {
    auto f = DecodeFma(data, length);
    if (!f || (f->Vector256 && !shadow)) return std::nullopt;
    return f;
}

void EmitFmaSse(StubBodyBuilder& body, const FmaOperands& f, const std::optional<std::int32_t> shadow) {
    const auto a = f.Destination;
    const auto b = f.Second;
    const auto memory = f.Memory;
    const std::uint8_t c = memory ? std::uint8_t{16} : f.Third;
    if (f.Vector256) {
        const auto h1 = ScratchAvoiding({a, b, c});
        const auto h2 = ScratchAvoiding({a, b, c, h1});
        const auto h3 = ScratchAvoiding({a, b, c, h1, h2});
        const auto t1 = ScratchAvoiding({a, b, c, h1, h2, h3});
        const auto t2 = ScratchAvoiding({a, b, c, h1, h2, h3}, {t1});
        const auto t3 = ScratchAvoiding({a, b, c, h1, h2, h3}, {t1, t2});
        for (const auto reg : {h1, h2, h3, t1, t2, t3}) body.Spill(reg);
        LoadUpper(body, h1, *shadow, a);
        LoadUpper(body, h2, *shadow, b);
        if (memory) body.Load(h3, OffsetMemoryOperand(*memory, 16));
        else LoadUpper(body, h3, *shadow, c);
        FmaHalf(body, f, h1, h2, h3, h1, t1, t2, t3);
        StoreUpper(body, h1, *shadow, a);
        for (const auto reg : {t3, t2, t1, h3, h2, h1}) body.Restore(reg);
    }
    const auto loaded = memory ? std::optional<std::uint8_t>(ScratchAvoiding({a, b})) : std::nullopt;
    const auto third = loaded ? *loaded : c;
    const auto t1 = ScratchAvoiding({a, b, third});
    const auto t2 = ScratchAvoiding({a, b, third, t1});
    const auto t3 = ScratchAvoiding({a, b, third, t1, t2});
    if (loaded) {
        body.Spill(*loaded);
        if (!f.Scalar) body.Load(*loaded, *memory);
        else body.MemoryOp(f.Double ? 0xF2 : 0xF3, {0x0F, 0x10}, *loaded, *memory);
    }
    for (const auto reg : {t1, t2, t3}) body.Spill(reg);
    FmaHalf(body, f, a, b, third, a, t1, t2, t3);
    for (const auto reg : {t3, t2, t1}) body.Restore(reg);
    if (loaded) body.Restore(*loaded);
    if (shadow && !f.Vector256) ClearUpper(body, *shadow, a);
}

}
