cbuffer U : register(b0, space0)
{
    float4 _12_color : packoffset(c0);
};


static float4 o;

struct SPIRV_Cross_Output
{
    float4 o : SV_Target0;
};

void frag_main()
{
    o = _12_color;
}

[RootSignature("RootFlags(0), DescriptorTable(CBV(b0, space=0, numDescriptors=1))")]
SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.o = o;
    return stage_output;
}
