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
};

constexpr Form Nds(const bool aligned) { return {Shape::Nds, aligned}; }
constexpr Form Unary(const bool aligned) { return {Shape::Unary, aligned}; }

bool In(const std::uint8_t op, const std::uint8_t first, const std::uint8_t last) { return op >= first && op <= last; }

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
        if (In(op, 0x60, 0x6D) || In(op, 0x74, 0x76) || op == 0x7C || op == 0x7D || op == 0xD0 || In(op, 0xD1, 0xD5) || In(op, 0xD8, 0xDF) || In(op, 0xE0, 0xE5) || In(op, 0xE8, 0xEF) || In(op, 0xF1, 0xF6) || In(op, 0xF8, 0xFE)) return Nds(true);
        if (op == 0x6E || op == 0x7E) return {Shape::Unary, false, true};
        if (op == 0x6F || op == 0x7F || op == 0xD6 || op == 0xD7 || op == 0xE7) return Unary(false);
        if (op == 0x70 || op == 0xE6) return Unary(true);
        if (In(op, 0x71, 0x73)) return {Shape::ShiftImm};
    }
    if (pp == 2 || pp == 3) {
        if (op == 0x10) return {Shape::ScalarMove};
        if (op == 0x11) return {Shape::ScalarStore};
        if (op == 0x2A) return {Shape::Nds, false, true};
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
    if (In(op, 0x00, 0x0B) || op == 0x28 || op == 0x29 || op == 0x2B || In(op, 0x37, 0x40) || In(op, 0xDC, 0xDF)) return Nds(true);
    if (op == 0x17 || In(op, 0x1C, 0x1E) || op == 0x41 || op == 0xDB) return Unary(true);
    if (In(op, 0x20, 0x25) || In(op, 0x30, 0x35) || op == 0x2A) return Unary(false);
    return {};
}

Form Map0F3A(const std::uint8_t op) {
    if (op == 0x08 || op == 0x09 || op == 0xDF) return Unary(true);
    if (op == 0x0A || op == 0x0B || op == 0x20 || op == 0x21) return Nds(false);
    if (op == 0x22) return {Shape::Nds, false, true};
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

}

std::optional<VexLegacy> DecodeVexLegacy(const std::uint8_t* data, const std::size_t length) {
    std::size_t vex = 0;
    while (vex < length && ScalarLowering::LegacyPrefix(data[vex])) ++vex;
    if (vex + 3 > length || (data[vex] != 0xC4 && data[vex] != 0xC5)) return std::nullopt;
    const bool threeByte = data[vex] == 0xC4;
    if (threeByte && vex + 4 > length) return std::nullopt;
    const auto payload = data[vex + 1];
    const auto last = threeByte ? data[vex + 2] : payload;
    const std::uint8_t r = (payload & 0x80) ? 0 : 1;
    const std::uint8_t x = threeByte && !(payload & 0x40) ? 1 : 0;
    const std::uint8_t b = threeByte && !(payload & 0x20) ? 1 : 0;
    const std::uint8_t map = threeByte ? static_cast<std::uint8_t>(payload & 0x1F) : 1;
    const bool w = threeByte && (last & 0x80) != 0;
    const auto vvvv = static_cast<std::uint8_t>(~last >> 3 & 0xF);
    const bool l = (last & 4) != 0;
    const auto pp = static_cast<std::uint8_t>(last & 3);
    const auto opIndex = vex + (threeByte ? 3 : 2);
    const auto op = data[opIndex];
    if (l) return std::nullopt;
    if (map == 1 && op == 0x77) {
        if (pp != 0 || vvvv != 0 || opIndex + 1 != length) return std::nullopt;
        return VexLegacy{kZeroUpper, {}};
    }
    if (opIndex + 2 > length) return std::nullopt;
    const auto form = Lookup(map, pp, op);
    if (form.Kind == Shape::None) return std::nullopt;
    const auto modrm = data[opIndex + 1];
    const bool memory = (modrm >> 6) != 3;
    const auto reg = static_cast<std::uint8_t>(r << 3 | (modrm >> 3 & 7));
    const auto rm = static_cast<std::uint8_t>(b << 3 | (modrm & 7));
    if (memory && form.Aligned) return std::nullopt;
    if (form.Predicate && data[length - 1] >= 8) return std::nullopt;
    switch (form.Kind) {
    case Shape::Nds:
        if (vvvv != reg) return std::nullopt;
        break;
    case Shape::Unary:
        if (vvvv != 0) return std::nullopt;
        break;
    case Shape::ShiftImm:
        if (memory || vvvv != rm) return std::nullopt;
        break;
    case Shape::ScalarMove:
        if (memory ? vvvv != 0 : vvvv != reg) return std::nullopt;
        break;
    case Shape::ScalarStore:
        if (memory ? vvvv != 0 : vvvv != rm) return std::nullopt;
        break;
    case Shape::None:
        return std::nullopt;
    }
    std::vector<std::uint8_t> legacy(data, data + vex);
    if (pp != 0) legacy.push_back(kMandatoryPrefix[pp]);
    const auto rex = static_cast<std::uint8_t>((form.Wide && w ? 8 : 0) | r << 2 | x << 1 | b);
    if (rex != 0) legacy.push_back(static_cast<std::uint8_t>(0x40 | rex));
    legacy.push_back(0x0F);
    if (map == 2) legacy.push_back(0x38);
    if (map == 3) legacy.push_back(0x3A);
    const auto shrink = static_cast<std::int32_t>(opIndex) - static_cast<std::int32_t>(legacy.size());
    if (shrink < 0) return std::nullopt;
    const auto body = legacy.size();
    legacy.insert(legacy.end(), data + opIndex, data + length);
    if ((modrm & 0xC7) == 0x05) {
        const auto at = body + 2;
        if (at + 4 > legacy.size()) return std::nullopt;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, legacy.data() + at, sizeof(displacement));
        displacement += shrink;
        std::memcpy(legacy.data() + at, &displacement, sizeof(displacement));
    }
    return VexLegacy{kName, std::move(legacy)};
}

void EmitVexLegacy(StubBodyBuilder& body, const VexLegacy& legacy) {
    if (legacy.Replacement.empty()) return;
    const X64InstructionDecoder decoder;
    const std::span<const std::uint8_t> bytes(legacy.Replacement);
    const auto info = decoder.DecodeInstruction(bytes.data(), bytes.size());
    if (info.Length != bytes.size())
        throw CodegenException(std::string(legacy.Name) + " replacement does not decode to one instruction");
    if (!info.HasRipRelativeDisp) {
        body.Raw(bytes);
        return;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + info.RipRelativeDispOffset, sizeof(displacement));
    body.RipRelative(bytes, info.RipRelativeDispOffset, static_cast<std::int64_t>(bytes.size()) + displacement);
}

}
