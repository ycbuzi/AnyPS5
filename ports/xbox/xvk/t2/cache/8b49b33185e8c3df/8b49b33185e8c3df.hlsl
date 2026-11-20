static const float2 _18[3] = { (-1.0f).xx, float2(1.0f, -1.0f), float2(-1.0f, 1.0f) };

static float4 gl_Position;
static int gl_VertexIndex;
struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(_18[gl_VertexIndex], 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)")]
SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
