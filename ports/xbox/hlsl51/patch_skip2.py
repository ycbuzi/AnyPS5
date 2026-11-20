import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
pairs = [
    ("bool SkipsOverBarrier(const IrProgram& program, const BlockInfo& info) {",
     "bool SkipsOverBarrier(const SpirvEmitterState& state, const IrProgram& program, const BlockInfo& info) {"),
    ("            if (inst->Opcode() == IrOpcode::Barrier) return true;\n        }\n        for (const IrBlock* next : block->Successors()) work.push_back(next);",
     "            if (inst->Opcode() == IrOpcode::Barrier) return true;\n            if (state.waveLdsScope == spv::ScopeWorkgroup && SharedAccessOf(inst->Opcode()) != SharedAccess::None) return true;\n        }\n        for (const IrBlock* next : block->Successors()) work.push_back(next);"),
    ("SkipsOverBarrier(state.program, info)", "SkipsOverBarrier(state, state.program, info)"),
]
for old, new in pairs:
    if new in s:
        continue
    if old not in s:
        print("anchor missing:", old[:60])
        sys.exit(1)
    s = s.replace(old, new)
open(path, "w", encoding="utf-8").write(s)
print("ok")
