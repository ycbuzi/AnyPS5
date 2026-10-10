#include "Translation/TranslationContext.hpp"
#include "RdnaDecoder/RdnaVectorOpDecoder.hpp"
#include "Optimization/SrtWalker/SrtEvaluator.hpp"
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ShaderRecompiler;

namespace {

void Require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error("fma nan result: " + message);
}

std::vector<const IrValue*> NanResults(const std::string& name, const std::vector<std::uint32_t>& words, bool ieee) {
    const RdnaInstruction instruction = DecodeRdnaVectorOp(std::span<const std::uint32_t>(words), 0u);
    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    TranslationContext context(program, block, 256);
    context.SetFloatMode(ShaderFloatMode{0xc0u, true, ieee, false});
    context.TranslateInstruction(instruction);
    std::vector<const IrValue*> results;
    for (const auto* value : block.Instructions()) {
        if (value->Opcode() != IrOpcode::FPNanResultFma32) continue;
        Require(value->ArgumentCount() == 4u, name + " has the wrong operands");
        Require(value->Flags<std::uint64_t>() == (ieee ? 1u : 0u), name + " carries the wrong IEEE flag");
        results.push_back(value);
    }
    return results;
}

std::uint32_t Evaluate(std::uint32_t result, std::uint32_t lhs, std::uint32_t rhs, std::uint32_t addend, bool ieee) {
    IrProgram program;
    const auto constant = [&](std::uint32_t bits) -> IrValue& {
        auto& value = program.CreateValue(IrOpcode::Void, IrType::F32);
        value.SetImmediateU32(bits);
        return value;
    };
    auto& nan = program.CreateValue(IrOpcode::FPNanResultFma32, IrType::F32, ieee ? 1u : 0u);
    for (auto* argument : {&constant(result), &constant(lhs), &constant(rhs), &constant(addend)}) nan.AddArgument(argument);
    const SrtRuntime runtime{};
    Detail::Evaluator evaluator(program.Resources(), runtime);
    std::uint32_t value = 0u;
    Require(evaluator.Evaluate(&nan, value), "the SRT evaluator refuses FPNanResultFma32");
    return value;
}

}

int main() {
    try {
        for (const bool ieee : {false, true}) {
            Require(NanResults("v_fma_f32 v5, v6, v7, v8", {0xd54b0005u, 0x04220f06u}, ieee).size() == 1u, "v_fma_f32 emits no single NaN result");
            Require(NanResults("v_fmac_f32 v5, v6, v7", {0x560a0f06u}, ieee).size() == 1u, "v_fmac_f32 emits no single NaN result");
            Require(NanResults("v_add_f32 v5, v6, v7", {0x060a0f06u}, ieee).empty(), "v_add_f32 emits an fma NaN result");
        }
        Require(Evaluate(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x00000000u, true) == 0x3f800000u, "a number result is not kept");
        Require(Evaluate(0x7fffffffu, 0x3f800000u, 0x7f812345u, 0x7fc00003u, false) == 0x7f812345u, "the first NaN source is not chosen");
        Require(Evaluate(0x7fffffffu, 0x3f800000u, 0x7f812345u, 0x7fc00003u, true) == 0x7fc12345u, "the NaN source is not quieted in IEEE mode");
        Require(Evaluate(0x7fffffffu, 0xffc12345u, 0x7f812345u, 0x00000000u, false) == 0xffc12345u, "the first source does not win");
        Require(Evaluate(0x7fffffffu, 0x7f800000u, 0x00000000u, 0x7fc00003u, false) == 0xffc00000u, "a NaN addend of an invalid product is not ignored");
        Require(Evaluate(0x7fffffffu, 0x7f800000u, 0xff800000u, 0x7fc00003u, false) == 0x7fc00003u, "the NaN addend of a valid product is not chosen");
        Require(Evaluate(0x7fffffffu, 0x7f800000u, 0x00000000u, 0x3f800000u, false) == 0xffc00000u, "an invalid product does not give the default NaN");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
