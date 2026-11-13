#include <codegen/x86/FmaLowering.hpp>
#include <codegen/x86/VexEncoding.hpp>
#include <array>
#include <string>

namespace Codegen {

using namespace ScalarLowering;
using namespace VexEncoding;

namespace {

constexpr std::int32_t kRedZone = 128;
constexpr std::int32_t kSlot = 32;
constexpr std::uint8_t kShiftLeftExtension = 6;
constexpr std::uint8_t kSignBit = 63;
constexpr std::uint8_t kOddElements = 0x0A;
constexpr Form kMultiplyPacked{Map0F, Prefix66, false, false, 0x59};
constexpr Form kMultiplyScalar{Map0F, PrefixF2, false, false, 0x59};
constexpr Form kAddPacked{Map0F, Prefix66, false, false, 0x58};
constexpr Form kAddScalar{Map0F, PrefixF2, false, false, 0x58};
constexpr Form kSubtractPacked{Map0F, Prefix66, false, false, 0x5C};
constexpr Form kSubtractScalar{Map0F, PrefixF2, false, false, 0x5C};
constexpr Form kAddSubtractPacked{Map0F, Prefix66, false, false, 0xD0};
constexpr Form kExclusiveOr{Map0F, Prefix66, false, false, 0x57};
constexpr Form kBlend{Map0F3A, Prefix66, false, false, 0x0D};
constexpr Form kSinglesToDoubles{Map0F, 0, false, true, 0x5A};
constexpr Form kDoublesToSingles{Map0F, Prefix66, false, true, 0x5A};
constexpr Form kSingleToDouble{Map0F, PrefixF3, false, false, 0x5A};
constexpr Form kDoubleToSingle{Map0F, PrefixF2, false, false, 0x5A};
constexpr Form kMergeLowDouble{Map0F, PrefixF2, false, false, 0x10};
constexpr Form kLoadSingles{Map0F, 0, false, true, 0x10};
constexpr Form kCompareEqual{Map0F, Prefix66, false, false, 0x76};
constexpr Form kShiftQwords{Map0F, Prefix66, false, false, 0x73};

constexpr std::array<const char*, 6> kOperationNames{"VFMADDSUB", "VFMSUBADD", "VFMADD", "VFMSUB", "VFNMADD", "VFNMSUB"};
constexpr std::array<const char*, 3> kOrderNames{"132", "213", "231"};

const char* _name(const FmaOperation operation, const int order, const bool scalar, const bool wide) {
    static const auto names = [] {
        std::array<std::string, 72> built;
        for (std::size_t index = 0; index < built.size(); ++index)
            built[index] = std::string(kOperationNames[index / 12]) + kOrderNames[index / 4 % 3] + ((index & 2) != 0 ? "S" : "P") + ((index & 1) != 0 ? "D" : "S");
        return built;
    }();
    return names[static_cast<std::size_t>(operation) * 12 + static_cast<std::size_t>(order) * 4 + (scalar ? 2 : 0) + (wide ? 1 : 0)].c_str();
}

struct Source {
    std::uint8_t Register;
    const MemoryOperand* Memory;
};

struct Context {
    StubBodyBuilder& Body;
    const FmaOperands& Operands;
    std::int32_t Frame;
    std::uint8_t Product;
    std::uint8_t Work;
    std::uint8_t Mask;
    std::uint8_t Alternate;
};

constexpr Form _sized(const Form& form, const bool wide) {
    return {form.Map, form.Prefix, form.Wide, wide, form.Opcode};
}

bool _negated(const FmaOperation operation) {
    return operation == FmaOperation::NegatedAdd || operation == FmaOperation::NegatedSub;
}

void _apply(const Context& context, const Form& form, const std::uint8_t destination, const std::uint8_t first, const Source& second, const std::optional<std::uint8_t> immediate = std::nullopt) {
    if (second.Memory)
        Memory(context.Body, form, destination, first, *second.Memory, context.Frame, context.Operands.Name, immediate);
    else
        Register(context.Body, form, destination, first, second.Register, immediate);
}

void _accumulate(const Context& context, const bool scalar, const bool wide, const Source& addend, const std::uint8_t result) {
    const auto product = context.Product;
    if (_negated(context.Operands.Operation)) Register(context.Body, _sized(kExclusiveOr, wide), product, product, context.Mask);
    switch (context.Operands.Operation) {
    case FmaOperation::Add:
    case FmaOperation::NegatedAdd:
        _apply(context, scalar ? kAddScalar : _sized(kAddPacked, wide), result, product, addend);
        break;
    case FmaOperation::Sub:
    case FmaOperation::NegatedSub:
        _apply(context, scalar ? kSubtractScalar : _sized(kSubtractPacked, wide), result, product, addend);
        break;
    case FmaOperation::AddSub:
        _apply(context, _sized(kAddSubtractPacked, wide), result, product, addend);
        break;
    case FmaOperation::SubAdd:
        _apply(context, _sized(kSubtractPacked, wide), context.Alternate, product, addend);
        _apply(context, _sized(kAddPacked, wide), product, product, addend);
        Register(context.Body, _sized(kBlend, wide), result, product, context.Alternate, kOddElements);
        break;
    }
}

void _widen(const Context& context, const std::uint8_t destination, const Source& source, const bool upper) {
    if (!upper) {
        _apply(context, kSinglesToDoubles, destination, 0, source);
        return;
    }
    ExtractUpper(context.Body, destination, source.Register);
    Register(context.Body, kSinglesToDoubles, destination, 0, destination);
}

void _singlesHalf(const Context& context, const Source& multiplicand, const Source& multiplier, const Source& addend, const bool upper) {
    _widen(context, context.Product, multiplicand, upper);
    _widen(context, context.Work, multiplier, upper);
    Register(context.Body, _sized(kMultiplyPacked, true), context.Product, context.Product, context.Work);
    _widen(context, context.Work, addend, upper);
    _accumulate(context, false, true, {context.Work, nullptr}, context.Product);
}

}

std::optional<FmaOperands> DecodeFma(const std::uint8_t* data, const std::size_t length) {
    const auto instruction = Decode(data, length);
    if (!instruction || instruction->Map != Map0F38 || instruction->Prefix != Prefix66) return std::nullopt;
    const auto order = instruction->Opcode >> 4;
    const auto kind = instruction->Opcode & 15;
    if (order < 9 || order > 11 || kind < 6) return std::nullopt;
    const bool scalar = kind >= 8 && (kind & 1) != 0;
    const auto operation = kind < 8 ? static_cast<FmaOperation>(kind - 6) : static_cast<FmaOperation>(2 + (kind - 8) / 2);
    FmaOperands operands{
        _name(operation, order - 9, scalar, instruction->Wide),
        static_cast<FmaOrder>(order - 9), operation, scalar, instruction->Wide, !scalar && instruction->Vector256,
        instruction->Reg, instruction->Vvvv, instruction->Rm, instruction->Memory};
    if (instruction->End != length) throw CodegenException(std::string(operands.Name) + " has trailing bytes");
    return operands;
}

void EmitFma(StubBodyBuilder& body, const FmaOperands& operands) {
    const bool negated = _negated(operands.Operation);
    const bool alternate = operands.Operation == FmaOperation::SubAdd;
    const bool halves = !operands.Double && operands.Vector256;
    const bool loaded = halves && operands.Memory.has_value();
    std::array<bool, 16> used{};
    used[operands.Destination] = true;
    used[operands.Second] = true;
    if (!operands.Memory) used[operands.Third] = true;
    std::array<std::uint8_t, 6> scratch{};
    const std::size_t count = 1 + (operands.Double ? 0 : 1) + (negated ? 1 : 0) + (alternate ? 1 : 0) + (halves ? 1 : 0) + (loaded ? 1 : 0);
    std::size_t taken = 0;
    for (std::uint8_t reg = 15; taken < count; --reg)
        if (!used[reg]) scratch[taken++] = reg;
    std::size_t next = 0;
    const auto product = scratch[next++];
    const auto work = operands.Double ? product : scratch[next++];
    const auto mask = negated ? scratch[next++] : product;
    const auto other = alternate ? scratch[next++] : product;
    const auto low = halves ? scratch[next++] : product;
    const auto frame = kRedZone + kSlot * static_cast<std::int32_t>(count);
    const Context context{body, operands, frame, product, work, mask, other};
    StackSlot(body, 0x8D, true, Stack, -frame);
    for (std::size_t index = 0; index < count; ++index)
        StackOperand(body, MoveUnaligned256Store, scratch[index], 0, static_cast<std::int32_t>(index) * kSlot);

    Source third{operands.Third, operands.Memory ? &*operands.Memory : nullptr};
    if (loaded) {
        _apply(context, kLoadSingles, scratch[next], 0, third);
        third = {scratch[next], nullptr};
    }
    const Source first{operands.Destination, nullptr};
    const Source second{operands.Second, nullptr};
    const auto multiplicand = operands.Order == FmaOrder::Order132 ? first : second;
    const auto multiplier = operands.Order == FmaOrder::Order213 ? first : third;
    const auto addend = operands.Order == FmaOrder::Order132 ? second : operands.Order == FmaOrder::Order213 ? third : first;
    if (negated) {
        Register(body, kCompareEqual, mask, mask, mask);
        Register(body, kShiftQwords, kShiftLeftExtension, mask, mask, kSignBit);
        if (!operands.Scalar && (!operands.Double || operands.Vector256)) InsertUpper(body, mask, mask, mask);
    }

    if (operands.Double && operands.Scalar) {
        _apply(context, kMultiplyScalar, product, multiplicand.Register, multiplier);
        _accumulate(context, true, false, addend, product);
        Register(body, kMergeLowDouble, operands.Destination, operands.Destination, product);
    } else if (operands.Double) {
        _apply(context, _sized(kMultiplyPacked, operands.Vector256), product, multiplicand.Register, multiplier);
        _accumulate(context, false, operands.Vector256, addend, operands.Destination);
    } else if (operands.Scalar) {
        _apply(context, kSingleToDouble, product, product, multiplicand);
        _apply(context, kSingleToDouble, work, work, multiplier);
        Register(body, kMultiplyScalar, product, product, work);
        _apply(context, kSingleToDouble, work, work, addend);
        _accumulate(context, true, false, {work, nullptr}, product);
        Register(body, kDoubleToSingle, operands.Destination, operands.Destination, product);
    } else {
        _singlesHalf(context, multiplicand, multiplier, addend, false);
        if (halves) {
            Register(body, kDoublesToSingles, low, 0, product);
            _singlesHalf(context, multiplicand, multiplier, addend, true);
            Register(body, kDoublesToSingles, product, 0, product);
            InsertUpper(body, operands.Destination, low, product);
        } else {
            Register(body, kDoublesToSingles, operands.Destination, 0, product);
        }
    }

    for (std::size_t index = 0; index < count; ++index)
        StackOperand(body, MoveUnaligned256Load, scratch[index], 0, static_cast<std::int32_t>(index) * kSlot);
    StackSlot(body, 0x8D, true, Stack, frame);
}

}
