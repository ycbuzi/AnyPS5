import re

TEXDECL = re.compile(r"^(Texture1D|Texture2D|Texture2DArray|Texture3D)<((?:uint|int)4?)> (\w+)(?:\[\d+\])? : register", re.M)
SAMPDECL = re.compile(r"^(?:SamplerState|SamplerComparisonState) (\w+)(?:\[(\d+)\])? : register", re.M)
CALL = re.compile(r"\b(\w+)(\[[^\[\]]*\])?\.(Sample|SampleLevel|SampleGrad|SampleBias)\(")

COMMON = """ByteAddressBuffer spvSamplerInfo : register(t0, space9);
int spvAddress(int c, int size, uint mode, inout bool border)
{
    if (mode == 0u) return ((c % size) + size) % size;
    if (mode == 1u) { int p = ((c % (2 * size)) + 2 * size) % (2 * size); return p < size ? p : 2 * size - 1 - p; }
    if (mode == 4u) { int m = c < 0 ? -c - 1 : c; return min(m, size - 1); }
    if (mode == 3u && (c < 0 || c >= size)) border = true;
    return clamp(c, 0, size - 1);
}
uint spvMip(uint4 s, float lod, uint levels)
{
    float l = clamp(lod + asfloat(s.w), asfloat(s.y), asfloat(s.z));
    return l <= 0.0f ? 0u : min((uint)(ceil(l + 0.5f) - 1.0f), levels - 1u);
}
float spvImplicitLod(float3 dx, float3 dy)
{
    return log2(max(max(length(dx), length(dy)), 1e-20f));
}
"""

DIMS = {
    "Texture1D": ("float", 1, "uint w, levels; t.GetDimensions(0u, w, levels); float3 size0 = float3(w, 1.0f, 1.0f);",
                  "uint mw, ml; t.GetDimensions(mip, mw, ml); float3 size = float3(mw, 1.0f, 1.0f);",
                  "t.Load(int2(x, mip))"),
    "Texture2D": ("float2", 2, "uint w, h, levels; t.GetDimensions(0u, w, h, levels); float3 size0 = float3(w, h, 1.0f);",
                  "uint mw, mh, ml; t.GetDimensions(mip, mw, mh, ml); float3 size = float3(mw, mh, 1.0f);",
                  "t.Load(int3(x, y, mip))"),
    "Texture2DArray": ("float3", 2, "uint w, h, layers, levels; t.GetDimensions(0u, w, h, layers, levels); float3 size0 = float3(w, h, 1.0f);",
                       "uint mw, mh, mlayers, ml; t.GetDimensions(mip, mw, mh, mlayers, ml); float3 size = float3(mw, mh, 1.0f);",
                       "t.Load(int4(x, y, clamp((int)floor(uv.z + 0.5f), 0, (int)mlayers - 1), mip))"),
    "Texture3D": ("float3", 3, "uint w, h, d, levels; t.GetDimensions(0u, w, h, d, levels); float3 size0 = float3(w, h, d);",
                  "uint mw, mh, md, ml; t.GetDimensions(mip, mw, mh, md, ml); float3 size = float3(mw, mh, md);",
                  "t.Load(int4(x, y, z, mip))"),
}


def helper(tex, elem, method):
    coord, axes, dims0, dims, load = DIMS[tex]
    name = "spv%s_%s_%s" % (method, tex, elem)
    pad = {1: "float3(%s, 0.0f, 0.0f)", 2: "float3(%s, 0.0f)", 3: "%s"}[axes]
    pick = {1: "uv", 2: "uv.xy", 3: "uv.xyz"}[axes]
    if tex == "Texture1D":
        pick = "uv"
    if method == "SampleLevel":
        extra, lod = ", float lod", "lod"
    elif method == "SampleBias":
        extra, lod = ", float bias", "spvImplicitLod(ddx(%s) * size0, ddy(%s) * size0) + bias" % (pad % pick, pad % pick)
    elif method == "SampleGrad":
        gtype = "float" if axes == 1 else "float%d" % axes
        extra, lod = ", %s gx, %s gy" % (gtype, gtype), "spvImplicitLod(%s * size0, %s * size0)" % (pad % "gx", pad % "gy")
    else:
        extra, lod = "", "spvImplicitLod(ddx(%s) * size0, ddy(%s) * size0)" % (pad % pick, pad % pick)
    one = "1" if elem.endswith("4") else ""
    border = "%s4" % elem.rstrip("4")
    lines = [
        "%s %s(%s<%s> t, uint slot, %s uv%s)" % (elem, name, tex, elem, coord, extra),
        "{",
        "    uint4 s = spvSamplerInfo.Load4(slot * 16u);",
        "    " + dims0,
        "    uint mip = spvMip(s, %s, levels);" % lod,
        "    " + dims,
        "    float3 tc = ((s.x >> 24) & 1u) != 0u ? %s : %s * size;" % (pad % pick, pad % pick),
        "    bool border = false;",
        "    int x = spvAddress((int)floor(tc.x), (int)size.x, s.x & 0xFu, border);",
        "    int y = spvAddress((int)floor(tc.y), (int)size.y, (s.x >> 4) & 0xFu, border);",
        "    int z = spvAddress((int)floor(tc.z), (int)size.z, (s.x >> 8) & 0xFu, border);",
        "    uint color = (s.x >> 16) & 0xFu;",
        "    if (border) return (%s)(color >= 4u ? %s(1, 1, 1, 1) : %s(0, 0, 0, color >= 2u ? 1 : 0));" % (elem, border, border),
        "    return %s;" % load,
        "}",
    ]
    del one
    return name, "\n".join(lines)


def args_until_close(text, start):
    depth, i, parts, cur = 1, start, [], start
    while i < len(text):
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
            if depth == 0:
                parts.append(text[cur:i].strip())
                return parts, i + 1
        elif c == "," and depth == 1:
            parts.append(text[cur:i].strip())
            cur = i + 1
        i += 1
    raise ValueError("unbalanced call")


def lower(text, layout):
    textures = {m.group(3): (m.group(1), m.group(2)) for m in TEXDECL.finditer(text)}
    if not textures:
        return text, None
    slots, total = {}, 0
    for m in SAMPDECL.finditer(text):
        slots[m.group(1)] = total
        total += int(m.group(2) or 1)
    helpers, out, last = {}, [], 0
    for m in CALL.finditer(text):
        if m.group(1) not in textures or m.start() < last:
            continue
        tex, elem = textures[m.group(1)]
        if not elem.endswith("4"):
            raise ValueError("integer texture element type %s is not handled" % elem)
        args, end = args_until_close(text, m.end())
        sm = re.match(r"^(\w+)(?:\[(.*)\])?$", args[0])
        if not sm or sm.group(1) not in slots:
            raise ValueError("sampler expression not understood: " + args[0])
        if len(args) > {"Sample": 2, "SampleLevel": 3, "SampleBias": 3, "SampleGrad": 4}[m.group(3)]:
            raise ValueError("integer %s with offset or clamp is not handled" % m.group(3))
        slot = "%du + (uint)(%s)" % (slots[sm.group(1)], sm.group(2)) if sm.group(2) else "%du" % slots[sm.group(1)]
        name, code = helper(tex, elem, m.group(3))
        helpers[name] = code
        out.append(text[last:m.start()])
        out.append("%s(%s%s, %s, %s)" % (name, m.group(1), m.group(2) or "", slot, ", ".join(args[1:])))
        last = end
    if not helpers:
        return text, None
    out.append(text[last:])
    body = "".join(out)
    info = [{"name": n, "info_slot": s} for n, s in slots.items()]
    return COMMON + "\n".join(helpers.values()) + "\n" + body, info
