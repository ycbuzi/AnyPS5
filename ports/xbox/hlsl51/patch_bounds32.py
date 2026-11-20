import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
old = """    if (std::ranges::find(state.supportedCapabilities, spv::CapabilityInt64) == state.supportedCapabilities.end()) context.Fail(instruction, "scalar buffer bounds require shaderInt64");
    state.module.EmitCapability(spv::CapabilityInt64);
    const auto resource = descriptor(context, instruction, 0u);
    const auto u64 = TypeScalarU64(state);"""
new = """    if (std::ranges::find(state.supportedCapabilities, spv::CapabilityInt64) == state.supportedCapabilities.end()) {
        if (context.Memory(instruction).gpuDescriptor) context.Fail(instruction, "scalar buffer bounds through a GPU descriptor require shaderInt64");
        const auto resource = descriptor(context, instruction, 0u);
        const auto u32 = TypeU32(state);
        const auto boolean = TypeBool(state);
        const auto product = state.module.AllocateId();
        const auto productLow = state.module.AllocateId();
        const auto productHigh = state.module.AllocateId();
        state.module.AddFunction(spv::OpUMulExtended, TypeU32Pair(state), product, resource.stride, resource.records);
        state.module.AddFunction(spv::OpCompositeExtract, u32, productLow, product, 0u);
        state.module.AddFunction(spv::OpCompositeExtract, u32, productHigh, product, 1u);
        const auto strideZero = equal(state, resource.stride, ConstantU32(state, 0u));
        const auto sizeLow = Select(state, u32, strideZero, resource.records, productLow);
        const auto sizeHigh = Select(state, u32, strideZero, ConstantU32(state, 0u), productHigh);
        const auto address = EmitAddU32(state, context.Arg(instruction, 1u), ConstantU32(state, context.Memory(instruction).offset));
        const auto noCarry = Binary(state, spv::OpUGreaterThanEqual, boolean, address, context.Arg(instruction, 1u));
        const auto byte = EmitAndConstant(state, address, ~3u);
        const auto endLow = EmitAddU32(state, byte, ConstantU32(state, 4u));
        const auto endHigh = Select(state, u32, Binary(state, spv::OpULessThan, boolean, endLow, byte), ConstantU32(state, 1u), ConstantU32(state, 0u));
        const auto below = Binary(state, spv::OpULessThan, boolean, endHigh, sizeHigh);
        const auto equalHigh = Binary(state, spv::OpIEqual, boolean, endHigh, sizeHigh);
        const auto lowFits = Binary(state, spv::OpULessThanEqual, boolean, endLow, sizeLow);
        const auto fits = Binary(state, spv::OpLogicalOr, boolean, below, Binary(state, spv::OpLogicalAnd, boolean, equalHigh, lowFits));
        return readBuffer(context, instruction, byte, 32u, both(state, noCarry, both(state, resource.valid, fits)));
    }
    state.module.EmitCapability(spv::CapabilityInt64);
    const auto resource = descriptor(context, instruction, 0u);
    const auto u64 = TypeScalarU64(state);"""
if "scalar buffer bounds through a GPU descriptor" in s:
    print("already patched")
    sys.exit(0)
if old not in s:
    print("anchor missing")
    sys.exit(1)
open(path, "w", encoding="utf-8").write(s.replace(old, new, 1))
print("patched")
