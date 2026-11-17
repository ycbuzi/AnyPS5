#include <codegen/x86/ReciprocalLowering.hpp>
#include <codegen/x86/VexLegacyLowering.hpp>
#include <array>
#include <cstring>

namespace Codegen {

namespace {

constexpr std::uint8_t kSqrtps = 0x51;
constexpr std::uint8_t kDivps = 0x5E;
constexpr std::uint8_t kMovaps = 0x28;
constexpr std::uint8_t kMovss = 0x10;
constexpr std::uint8_t kScalarPrefix = 0xF3;
constexpr std::uint8_t kVexScalarPrefix = 0x02;
constexpr float kOne = 1.0f;

StubConstant _ones() {
    StubConstant constant{};
    for (std::size_t lane = 0; lane < 4; ++lane)
        std::memcpy(constant.data() + lane * sizeof(kOne), &kOne, sizeof(kOne));
    return constant;
}

std::uint8_t _scratch(const ReciprocalOperands& operands, const std::uint8_t taken = 16) {
    std::uint8_t reg = 0;
    while (reg == operands.Destination || reg == operands.Source || reg == operands.Upper || reg == taken)
        ++reg;
    return reg;
}

void _vex(StubBodyBuilder& body, const std::uint8_t pp, const std::uint8_t opcode, const std::uint8_t dst, const std::uint8_t vvvv, const std::uint8_t src) {
    const std::array<std::uint8_t, 5> bytes{
        0xC4,
        static_cast<std::uint8_t>(((dst & 8) != 0 ? 0x00 : 0x80) | 0x40 | ((src & 8) != 0 ? 0x00 : 0x20) | 0x01),
        static_cast<std::uint8_t>(((~vvvv & 0x0F) << 3) | pp),
        opcode,
        static_cast<std::uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7))
    };
    body.Raw(bytes);
}

void _vexMovaps(StubBodyBuilder& body, const std::uint8_t dst, const std::uint8_t src) {
    _vex(body, 0, kMovaps, dst, 0, src);
}

void _emitScalar(StubBodyBuilder& body, const ReciprocalOperands& operands, const std::uint8_t scratch) {
    const auto dst = operands.Destination;
    const auto opcode = operands.Operation == ReciprocalOperation::ReciprocalSquareRoot ? kSqrtps : kMovss;
    if (operands.Vex)
        _vex(body, kVexScalarPrefix, opcode, dst, operands.Upper, operands.Source);
    else
        body.Sse(kScalarPrefix, {0x0F, opcode}, dst, operands.Source);
    body.RipOperand({0x0F, kMovaps}, scratch, _ones());
    body.Sse(kScalarPrefix, {0x0F, kDivps}, scratch, dst);
    body.Sse(kScalarPrefix, {0x0F, kMovss}, dst, scratch);
}

void _emitLegacyScalar(StubBodyBuilder& body, const ReciprocalOperands& operands, const std::uint8_t scratch) {
    const auto dst = operands.Destination;
    body.RipOperand({0x0F, kMovaps}, scratch, _ones());
    if (operands.Operation == ReciprocalOperation::ReciprocalSquareRoot) {
        const auto root = _scratch(operands, scratch);
        body.Spill(root);
        body.Sse(kScalarPrefix, {0x0F, kSqrtps}, root, operands.Source);
        body.Sse(kScalarPrefix, {0x0F, kDivps}, scratch, root);
        body.Restore(root);
    } else {
        body.Sse(kScalarPrefix, {0x0F, kDivps}, scratch, operands.Source);
    }
    if (operands.Upper != dst)
        body.SsePlain({0x0F, kMovaps}, dst, operands.Upper);
    body.Sse(kScalarPrefix, {0x0F, kMovss}, dst, scratch);
}

}

void ReciprocalLowering::EmitOutOfLine(StubBodyBuilder& body, const ReciprocalOperands& operands, const bool legacyOnly, const std::optional<std::int32_t> shadow) const {
    const auto scratch = _scratch(operands);
    const auto dst = operands.Destination;
    body.Spill(scratch);
    if (legacyOnly && operands.Vex) {
        if (operands.Scalar) {
            _emitLegacyScalar(body, operands, scratch);
        } else if (operands.Operation == ReciprocalOperation::ReciprocalSquareRoot) {
            body.SsePlain({0x0F, kSqrtps}, scratch, operands.Source);
            body.RipOperand({0x0F, kMovaps}, dst, _ones());
            body.SsePlain({0x0F, kDivps}, dst, scratch);
        } else {
            body.RipOperand({0x0F, kMovaps}, scratch, _ones());
            body.SsePlain({0x0F, kDivps}, scratch, operands.Source);
            body.SsePlain({0x0F, kMovaps}, dst, scratch);
        }
        if (shadow)
            EmitClearUpper(body, *shadow, dst);
    } else if (operands.Scalar) {
        _emitScalar(body, operands, scratch);
    } else if (operands.Operation == ReciprocalOperation::ReciprocalSquareRoot) {
        body.SsePlain({0x0F, kSqrtps}, scratch, operands.Source);
        body.RipOperand({0x0F, kMovaps}, dst, _ones());
        body.SsePlain({0x0F, kDivps}, dst, scratch);
        if (operands.Vex)
            _vexMovaps(body, dst, dst);
    } else {
        body.RipOperand({0x0F, kMovaps}, scratch, _ones());
        body.SsePlain({0x0F, kDivps}, scratch, operands.Source);
        if (operands.Vex)
            _vexMovaps(body, dst, scratch);
        else
            body.SsePlain({0x0F, kMovaps}, dst, scratch);
    }
    body.Restore(scratch);
}

LoweredBody ReciprocalLowering::LowerOutOfLine(const ReciprocalOperands& operands, const bool legacyOnly, const std::optional<std::int32_t> shadow) const {
    StubBodyBuilder body;
    EmitOutOfLine(body, operands, legacyOnly, shadow);
    return body.Finish();
}

}
