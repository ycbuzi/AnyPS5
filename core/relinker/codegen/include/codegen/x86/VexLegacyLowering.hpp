#ifndef CODEGEN_X86_VEXLEGACYLOWERING_HPP
#define CODEGEN_X86_VEXLEGACYLOWERING_HPP

#include <codegen/x86/StubBodyBuilder.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace Codegen {

struct VexLegacy {
    const char* Name;
    std::vector<std::uint8_t> Replacement;
};

[[nodiscard]] std::optional<VexLegacy> DecodeVexLegacy(const std::uint8_t* data, std::size_t length);
void EmitVexLegacy(StubBodyBuilder& body, const VexLegacy& legacy);

}

#endif
