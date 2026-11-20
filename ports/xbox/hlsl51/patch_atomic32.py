import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
old = """        if (std::ranges::find(state.supportedCapabilities, spv::CapabilityInt64) == state.supportedCapabilities.end()) context.Fail(instruction, "buffer atomic fault addresses require shaderInt64");
        state.module.EmitCapability(spv::CapabilityInt64);
    }"""
new = """        const bool hasInt64 = std::ranges::find(state.supportedCapabilities, spv::CapabilityInt64) != state.supportedCapabilities.end();
        if (!hasInt64 && (bits == 64u || state.requirements.bufferAtomicLocks)) context.Fail(instruction, "64-bit or locked buffer atomic fault addresses require shaderInt64");
        if (hasInt64) state.module.EmitCapability(spv::CapabilityInt64);
    }"""
if "64-bit or locked buffer atomic" in s:
    print("already patched"); sys.exit(0)
if old not in s:
    print("anchor missing"); sys.exit(1)
open(path, "w", encoding="utf-8").write(s.replace(old, new, 1))
print("patched")
