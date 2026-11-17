#ifndef CODEGEN_X86_RECIPROCALLOWERING_HPP
#define CODEGEN_X86_RECIPROCALLOWERING_HPP

#include <codegen/x86/ReciprocalOperands.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include <cstdint>
#include <optional>

namespace Codegen {

class ReciprocalLowering {
public:
    void EmitOutOfLine(StubBodyBuilder& body, const ReciprocalOperands& operands, bool legacyOnly = false, std::optional<std::int32_t> shadow = std::nullopt) const;
    [[nodiscard]] LoweredBody LowerOutOfLine(const ReciprocalOperands& operands, bool legacyOnly = false, std::optional<std::int32_t> shadow = std::nullopt) const;
};

}

#endif
