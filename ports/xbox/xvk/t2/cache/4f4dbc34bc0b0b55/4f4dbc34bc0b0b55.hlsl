static const uint3 gl_WorkGroupSize = uint3(64u, 1u, 1u);

RWByteAddressBuffer _10 : register(u0, space0);
ByteAddressBuffer _24 : register(t0, space7);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

void comp_main()
{
    _10.Store(gl_GlobalInvocationID.x * 4 + 0, gl_GlobalInvocationID.x * _24.Load(0));
}

[numthreads(64, 1, 1)]
[RootSignature("RootFlags(0), DescriptorTable(SRV(t0, space=7, numDescriptors=1), UAV(u0, space=0, numDescriptors=1))")]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
