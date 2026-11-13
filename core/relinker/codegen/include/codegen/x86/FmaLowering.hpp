#ifndef CODEGEN_X86_FMALOWERING_HPP
#define CODEGEN_X86_FMALOWERING_HPP

#include <codegen/x86/StubBodyBuilder.hpp>
#include <cstdint>
#include <optional>

namespace Codegen {

enum class FmaOrder : std::uint8_t { Order132, Order213, Order231 };
enum class FmaOperation : std::uint8_t { AddSub, SubAdd, Add, Sub, NegatedAdd, NegatedSub };

struct FmaOperands {
    const char* Name;
    FmaOrder Order;
    FmaOperation Operation;
    bool Scalar;
    bool Double;
    bool Vector256;
    std::uint8_t Destination;
    std::uint8_t Second;
    std::uint8_t Third;
    std::optional<MemoryOperand> Memory;
};

[[nodiscard]] std::optional<FmaOperands> DecodeFma(const std::uint8_t* data, std::size_t length);
void EmitFma(StubBodyBuilder& body, const FmaOperands& operands);

}

#endif
