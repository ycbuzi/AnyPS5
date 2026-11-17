#ifndef CODEGEN_X86_AMD64ONLYSUBSTITUTIONTYPES_HPP
#define CODEGEN_X86_AMD64ONLYSUBSTITUTIONTYPES_HPP

#include <domain/Types.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Codegen {

enum class Amd64OnlyTarget : std::uint8_t {
    Intel,
    Rosetta,
    Avx,
    Sse
};

[[nodiscard]] constexpr Amd64OnlyTarget Amd64OnlyTargetFor(const bool rosetta, const bool avx, const bool sse = false) {
    return sse ? Amd64OnlyTarget::Sse : avx ? Amd64OnlyTarget::Avx : rosetta ? Amd64OnlyTarget::Rosetta : Amd64OnlyTarget::Intel;
}

enum LoweredHostCpuFeature : std::uint32_t {
    LoweredLzcnt = 1u << 0,
    LoweredBmi1 = 1u << 1,
    LoweredBmi2 = 1u << 2,
    LoweredMovbe = 1u << 3,
    LoweredAvx = 1u << 4,
    LoweredAvx2 = 1u << 5,
    LoweredFma = 1u << 6,
    LoweredF16c = 1u << 7,
    LoweredAvxState = 1u << 8
};

[[nodiscard]] constexpr std::uint32_t LoweredHostCpuFeatures(const Amd64OnlyTarget target, const bool everyVexLowered = false) {
    if (target != Amd64OnlyTarget::Sse) return 0u;
    const std::uint32_t scalar = LoweredLzcnt | LoweredBmi1 | LoweredBmi2 | LoweredMovbe;
    return everyVexLowered ? scalar | LoweredAvx | LoweredAvx2 | LoweredFma | LoweredF16c | LoweredAvxState : scalar;
}

inline constexpr std::uint64_t kUpperShadowBytes = 256;

[[nodiscard]] constexpr std::uint64_t UpperShadowReservation(const std::uint64_t alignment) {
    const auto unit = alignment == 0 ? std::uint64_t{1} : alignment;
    return (kUpperShadowBytes + unit - 1) / unit * unit;
}

[[nodiscard]] constexpr std::int64_t UpperShadowOffset(const std::uint64_t memorySize, const std::uint64_t alignment) {
    const auto unit = alignment == 0 ? std::uint64_t{1} : alignment;
    return -static_cast<std::int64_t>((memorySize + unit - 1) / unit * unit + UpperShadowReservation(unit));
}

enum class Amd64OnlyLowering : std::uint8_t {
    InPlace,
    Trampoline,
    Unsupported,
    Kept
};

struct StubRelocation {
    std::size_t DisplacementOffset;
    std::size_t InstructionEnd;
    std::int64_t SiteTarget;
    bool AddressSize32 = false;
};

struct Amd64OnlyMatch {
    std::string InstructionName;
    std::size_t Length;
    Amd64OnlyLowering Lowering;
    std::vector<std::uint8_t> ReplacementBytes;
    std::vector<std::uint8_t> StubBody;
    std::size_t ReturnBranchOffset;
    bool Optional = false;
    std::vector<StubRelocation> Relocations = {};
    bool RelocatableWindow = false;
};

struct Amd64OnlySubstitutionReport {
    std::string InstructionName;
    Domain::FileByteOffset Offset;
    std::size_t OriginalLength;
    std::size_t ReplacementLength;
    Amd64OnlyLowering Lowering;
};

}

#endif
