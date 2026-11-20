import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
if "g_xbox" in s:
    print("already patched")
    sys.exit(0)
s = s.replace("std::uint32_t g_subgroupSize = 0;\n", "std::uint32_t g_subgroupSize = 0;\nbool g_xbox = false;\n", 1)
anchor = "    if (g_float16WidenNans) request.request.target.float16WidenNans = true;\n"
block = """    if (g_xbox) {
        const std::uint32_t dropped[] = {spv::CapabilityInt64, spv::CapabilityInt16, spv::CapabilityInt8, spv::CapabilityFloat16, spv::CapabilityFloat64, spv::CapabilityInt64Atomics,
                                         spv::CapabilityGroupNonUniform, spv::CapabilityGroupNonUniformVote, spv::CapabilityGroupNonUniformArithmetic, spv::CapabilityGroupNonUniformBallot,
                                         spv::CapabilityGroupNonUniformShuffle, spv::CapabilityGroupNonUniformShuffleRelative, spv::CapabilityGroupNonUniformClustered, spv::CapabilityGroupNonUniformQuad,
                                         spv::CapabilityFragmentBarycentricKHR, spv::CapabilitySubgroupBallotKHR, spv::CapabilityStorageBuffer16BitAccess, spv::CapabilityStorageBuffer8BitAccess,
                                         spv::CapabilityUniformAndStorageBuffer8BitAccess, spv::CapabilityUniformAndStorageBuffer16BitAccess, spv::CapabilityPhysicalStorageBufferAddresses};
        std::erase_if(request.supportedCapabilities, [&](std::uint32_t c) { return std::find(std::begin(dropped), std::end(dropped), c) != std::end(dropped); });
        std::erase_if(request.supportedExtensions, [](const std::string& e) { return e == "SPV_KHR_fragment_shader_barycentric" || e == "SPV_KHR_physical_storage_buffer" || e == "SPV_KHR_shader_ballot" || e == "SPV_KHR_16bit_storage" || e == "SPV_KHR_8bit_storage"; });
        request.request.target.supportedCapabilities = request.supportedCapabilities;
        request.request.target.supportedExtensions = request.supportedExtensions;
        request.request.target.subgroupSize = 1;
    }
"""
if anchor not in s:
    print("anchor missing")
    sys.exit(1)
s = s.replace(anchor, block + anchor, 1)
arg = '        if (std::string(argv[i]) == "--subgroup-size" && i + 1 < argc) {\n'
s = s.replace(arg, '        if (std::string(argv[i]) == "--xbox") {\n            g_xbox = true;\n            continue;\n        }\n' + arg, 1)
if "#include <algorithm>" not in s:
    s = "#include <algorithm>\n" + s
open(path, "w", encoding="utf-8").write(s)
print("patched")
