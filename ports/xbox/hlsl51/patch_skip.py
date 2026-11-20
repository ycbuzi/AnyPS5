import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
old = "std::uint32_t EmitWaveMaskBranchCondition(SpirvValueEmitContext& ctx, const BlockInfo& info) {\n    auto& state = ctx.state;\n    const auto kind = info.terminator.condition;\n"
new = """bool SkipsOverBarrier(const IrProgram& program, const BlockInfo& info) {
    const auto& term = info.terminator;
    if (term.loopHeader || term.kind != TerminatorKind::ConditionalBranch || term.mergeBlock == InvalidControlFlowId) return false;
    const IrBlock* merge = TargetBlock(program, term.mergeBlock);
    std::vector<const IrBlock*> work{TargetBlock(program, term.trueBlock), TargetBlock(program, term.falseBlock)};
    std::vector<const IrBlock*> seen;
    while (!work.empty()) {
        const IrBlock* block = work.back();
        work.pop_back();
        if (block == nullptr || block == merge || std::find(seen.begin(), seen.end(), block) != seen.end()) continue;
        seen.push_back(block);
        if (seen.size() > program.BlockOrder().size()) return false;
        for (const IrValue* inst : block->Instructions()) {
            if (inst->Opcode() == IrOpcode::Barrier) return true;
        }
        for (const IrBlock* next : block->Successors()) work.push_back(next);
    }
    return false;
}

std::uint32_t EmitWaveMaskBranchCondition(SpirvValueEmitContext& ctx, const BlockInfo& info) {
    auto& state = ctx.state;
    const auto kind = info.terminator.condition;
    if (HostSubgroupSingleLane(state) && (kind == BranchCondition::ExecZero || kind == BranchCondition::ExecNonZero) && SkipsOverBarrier(state.program, info)) {
        return ConstantBool(state, kind == BranchCondition::ExecNonZero);
    }
"""
if "SkipsOverBarrier" in s:
    print("already patched")
    sys.exit(0)
if old not in s:
    print("anchor not found")
    sys.exit(1)
s = s.replace(old, new, 1)
if "#include <algorithm>" not in s:
    s = s.replace("#include <cstdio>\n", "#include <algorithm>\n#include <cstdio>\n", 1)
if "#include <vector>" not in s:
    s = s.replace("#include <cstdio>\n", "#include <cstdio>\n#include <vector>\n", 1)
open(path, "w", encoding="utf-8").write(s)
print("patched")
