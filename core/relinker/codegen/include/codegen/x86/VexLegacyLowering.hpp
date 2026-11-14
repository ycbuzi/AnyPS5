#ifndef CODEGEN_X86_VEXLEGACYLOWERING_HPP
#define CODEGEN_X86_VEXLEGACYLOWERING_HPP

#include <codegen/x86/StubBodyBuilder.hpp>
#include <codegen/x86/FmaLowering.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Codegen {

struct VexLegacy {
    const char* Name;
    std::vector<std::uint8_t> Replacement;
};

struct VexStub {
    const char* Name;
    std::vector<std::uint8_t> Original;
};

struct VexShadow {
    const char* Name;
    std::vector<std::uint8_t> Original;
};

[[nodiscard]] std::optional<FmaOperands> DecodeFmaSse(const std::uint8_t* data, std::size_t length, bool shadow);
void EmitFmaSse(StubBodyBuilder& body, const FmaOperands& operands, std::optional<std::int32_t> shadow);
[[nodiscard]] std::optional<std::string> VexFormName(const std::uint8_t* data, std::size_t length);
[[nodiscard]] bool VexWide(const std::uint8_t* data, std::size_t length);
[[nodiscard]] std::optional<VexShadow> DecodeVexShadow(const std::uint8_t* data, std::size_t length, bool shadow);
void EmitVexShadow(StubBodyBuilder& body, const VexShadow& site, std::optional<std::int32_t> shadow);
[[nodiscard]] std::optional<VexStub> DecodeVexStub(const std::uint8_t* data, std::size_t length);
void EmitVexStub(StubBodyBuilder& body, const VexStub& stub);
[[nodiscard]] std::optional<VexLegacy> DecodeVexLegacy(const std::uint8_t* data, std::size_t length);
void EmitVexLegacy(StubBodyBuilder& body, const VexLegacy& legacy);

}

#endif
