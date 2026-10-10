#include "Recompiler.hpp"
#include <spirv/unified1/spirv.hpp>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// FLOAT_MODE's 16-bit rounding field. ps5-opengl (PPSA99005) runs its shaders with FLOAT_MODE 0xcc:
// f32 rounded to nearest even with denormals flushed, f16 rounded toward zero with denormals kept,
// and stopped at its first draw because v_fma_mixlo_f16 in that mode threw (upstream issue 2631).

namespace {

using namespace ShaderRecompiler;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// v_cvt_f16_f32 v1, v0; buffer_store_short v1, off, s[0:3], 0; s_endpgm
constexpr std::array<std::uint32_t, 4> CvtF16F32Store{0x7e021500u, 0xe0700000u, 0x80000100u, 0xbf810000u};
// v_fma_mixlo_f16 v1, v0, v0, v0 (f32 sources); buffer_store_short v1, off, s[0:3], 0; s_endpgm
constexpr std::array<std::uint32_t, 5> FmaMixloStore{0xcc210001u, 0x04020100u, 0xe0700000u, 0x80000100u, 0xbf810000u};

// FLOAT_MODE: [1:0] f32 rounding, [3:2] f16/f64 rounding, [5:4] f32 denormals, [7:6] f16/f64 denormals.
constexpr std::uint32_t NearestEvenKeepDenormals = 0xc0u;
constexpr std::uint32_t TowardZeroKeepDenormals = 0xccu;
constexpr std::uint32_t TowardPlusInfinityKeepDenormals = 0xc4u;

std::vector<std::uint32_t> recompile(std::span<const std::uint32_t> code, std::uint32_t floatMode) {
    const std::array<std::uint32_t, 4> userData{0x10000000u, 0x00100000u, 0x40u, 0x00027facu};
    const std::array<std::uint32_t, 6> capabilities{spv::CapabilityShader, spv::CapabilityGroupNonUniform, spv::CapabilityFloat16, spv::CapabilityFloat64, spv::CapabilityRoundingModeRTE, spv::CapabilityDenormPreserve};
    const std::array<std::string_view, 1> extensions{"SPV_KHR_storage_buffer_storage_class"};
    RecompileRequest request{};
    request.shader = {ShaderStage::Compute, 0x20000u, code, 0, {}};
    request.context.waveSize = 64;
    request.context.userDataBaseRegister = 0;
    request.context.userData = userData;
    request.context.compute = ShaderComputeStageInfo{{64u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.context.floatMode = ShaderFloatMode{floatMode, false, false, false};
    request.target.vulkanVersion = 0x00403000u;
    request.target.spirvVersion = 0x00010600u;
    request.target.subgroupSize = 64;
    request.target.bdaAbiVersion = 1;
    request.target.supportedCapabilities = capabilities;
    request.target.supportedExtensions = extensions;
    request.target.maxWorkgroupSize = {1024u, 1024u, 64u};
    request.target.maxWorkgroupInvocations = 1024;
    request.target.maxWorkgroupSharedMemoryBytes = 49152;
    request.layout = {0, 0, 0, 128};
    request.useCache = false;
    auto result = Recompile(request);
    require(!result.spirv.empty(), "the shader did not recompile");
    return std::move(result.spirv);
}

// Counts the OpFConvert instructions whose result is a 16-bit float: the native f32-to-f16
// conversion, which rounds to nearest even. Rounding toward zero must not use it.
std::size_t countF16Converts(std::span<const std::uint32_t> words) {
    std::map<std::uint32_t, std::uint32_t> floatWidths;
    std::size_t converts = 0;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> 16u;
        const auto op = static_cast<spv::Op>(words[cursor] & 0xffffu);
        require(count != 0 && cursor + count <= words.size(), "truncated SPIR-V instruction");
        if (op == spv::OpTypeFloat && count >= 3) floatWidths[words[cursor + 1]] = words[cursor + 2];
        if (op == spv::OpFConvert && count >= 4 && floatWidths[words[cursor + 1]] == 16u) ++converts;
        cursor += count;
    }
    return converts;
}

void verifyConversion() {
    require(countF16Converts(recompile(CvtF16F32Store, NearestEvenKeepDenormals)) != 0, "v_cvt_f16_f32 rounding to nearest even did not use the native f16 conversion");
    require(countF16Converts(recompile(CvtF16F32Store, TowardZeroKeepDenormals)) == 0, "v_cvt_f16_f32 rounding toward zero used the native nearest-even f16 conversion");
}

void verifyMix() {
    recompile(FmaMixloStore, NearestEvenKeepDenormals);
    require(countF16Converts(recompile(FmaMixloStore, TowardZeroKeepDenormals)) == 0, "v_fma_mixlo_f16 rounding toward zero used the native nearest-even f16 conversion");
}

void verifyUnsupportedModeStillThrows() {
    try {
        recompile(FmaMixloStore, TowardPlusInfinityKeepDenormals);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("toward an infinity") != std::string::npos, std::string("unexpected error for f16 rounding toward +infinity: ") + error.what());
        return;
    }
    throw std::runtime_error("v_fma_mixlo_f16 with f16 rounding toward +infinity recompiled although that mode is not implemented");
}

}

int main() {
    try {
        verifyConversion();
        verifyMix();
        verifyUnsupportedModeStillThrows();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Float16RoundTowardZero: %s\n", error.what());
        return 1;
    }
    std::puts("Float16RoundTowardZero: f16 results round toward zero under FLOAT_MODE 0xcc without the native nearest-even conversion");
    return 0;
}
