import re

FIELD = re.compile(r"^\s*((?:(?:nointerpolation|noperspective|linear|centroid|sample)\s+)*)(\w+)\s+(\w+)\s*:\s*(\w+);\s*$")


def split_semantic(semantic):
    m = re.match(r"^(.*?)(\d*)$", semantic)
    return m.group(1), m.group(2) or "0"


def lower(text):
    m = re.search(r"struct SPIRV_Cross_Input\n\{\n(.*?)\n\};", text, re.S)
    if not m or "SV_Barycentrics" not in m.group(1):
        return None
    vertex = set(re.findall(r"GetAttributeAtVertex\(stage_input\.(\w+),", text))
    ps, gs_in, gs_body, position = [], [], [], None
    for line in m.group(1).split("\n"):
        f = FIELD.match(line)
        if not f:
            raise ValueError("input field not understood: " + line.strip())
        quals, kind, name, semantic = f.groups()
        if semantic == "SV_Barycentrics":
            mode = "noperspective " if "NoPersp" in name else ""
            ps.append("    %s%s %s : BARYCENTRIC%d;" % (mode, kind, name, 1 if mode else 0))
            gs_body.append("        o.%s = float3(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);" % name)
        elif name in vertex:
            base, index = split_semantic(semantic)
            gs_in.append("    %s %s : %s;" % (kind, name, semantic))
            for v in range(3):
                ps.append("    nointerpolation %s %s_v%d : V%d%s%s;" % (kind, name, v, v, base, index))
                gs_body.append("        o.%s_v%d = v[%d].%s;" % (name, v, v, name))
        elif semantic == "SV_Position":
            position = name
            ps.append(line)
        elif semantic.startswith("SV_"):
            ps.append(line)
        else:
            gs_in.append("    %s%s %s : %s;" % (quals, kind, name, semantic))
            ps.append(line)
            gs_body.append("        o.%s = v[i].%s;" % (name, name))
    out_text = text[:m.start(1)] + "\n".join(ps) + text[m.end(1):]
    out_text = re.sub(r"GetAttributeAtVertex\(stage_input\.(\w+), (\d)\)", lambda g: "stage_input.%s_v%s" % (g.group(1), g.group(2)), out_text)
    gs_out = list(ps)
    if position is None:
        gs_out.append("    float4 gs_position : SV_Position;")
        position = "gs_position"
    gs_out = [l for l in gs_out if not re.search(r":\s*SV_(IsFrontFace|SampleIndex|Coverage|PrimitiveID)\s*;", l)]
    gs = ["struct GsIn", "{", "    float4 gs_position : SV_Position;"] + gs_in + ["};", "", "struct GsOut", "{"] + gs_out + ["};", "",
          "[maxvertexcount(3)]", "void main(triangle GsIn v[3], inout TriangleStream<GsOut> stream)", "{", "    [unroll] for (uint i = 0; i < 3; i++)", "    {",
          "        GsOut o;", "        o.%s = v[i].gs_position;" % position] + gs_body + ["        stream.Append(o);", "    }", "}", ""]
    return out_text, "\n".join(gs)
