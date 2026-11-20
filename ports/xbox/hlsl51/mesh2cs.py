import json, re, sys


def convert(text):
    lines = text.splitlines()
    m = re.search(r'; Bound: (\d+)', text)
    bound = [int(m.group(1))]

    def new():
        bound[0] += 1
        return '%%m%d' % bound[0]

    entry = None
    for l in lines:
        e = re.match(r'\s*OpEntryPoint MeshEXT (%\w+) ', l)
        if e:
            entry = e.group(1)
    if entry is None:
        return None
    info = {"maxv": 0, "maxp": 0, "locs": [], "cull": False}
    outvars, decos, ptrs, invars = {}, {}, {}, set()
    for l in lines:
        s = l.strip()
        a = re.match(r'(%\w+) = OpVariable (%\w+) Output$', s)
        if a:
            outvars[a.group(1)] = a.group(2)
        a = re.match(r'(%\w+) = OpVariable %\w+ Input$', s)
        if a:
            invars.add(a.group(1))
        a = re.match(r'OpDecorate (%\w+) (.*)$', s)
        if a:
            decos.setdefault(a.group(1), []).append(a.group(2))
        a = re.match(r'(%\w+) = OpTypePointer (\w+) (%\w+)$', s)
        if a:
            ptrs[(a.group(2), a.group(3))] = a.group(1)
        a = re.match(r'OpExecutionMode %s (OutputVertices|OutputPrimitivesNV|OutputPrimitivesEXT) (\d+)' % re.escape(entry), s)
        if a:
            info["maxv" if a.group(1) == "OutputVertices" else "maxp"] = int(a.group(2))
        if re.match(r'OpExecutionMode %s Output(Lines|Points)' % re.escape(entry), s):
            raise SystemExit('mesh2cs: only triangle output is supported')
    names = {}
    for v in outvars:
        ds = decos.get(v, [])
        loc = next((int(d.split()[1]) for d in ds if d.startswith('Location ')), None)
        if 'BuiltIn Position' in ds:
            names[v] = 'xvk_pos'
        elif 'BuiltIn PrimitiveTriangleIndicesEXT' in ds:
            names[v] = 'xvk_idx'
        elif 'BuiltIn CullPrimitiveEXT' in ds:
            names[v] = 'xvk_cull'
            info["cull"] = True
        elif loc is not None and not any(d.startswith('PerPrimitive') for d in ds):
            names[v] = 'xvk_v%d' % loc
            info["locs"].append(loc)
        else:
            raise SystemExit('mesh2cs: unsupported mesh output %s %s' % (v, ds))
    if 'xvk_pos' not in names.values() or 'xvk_idx' not in names.values():
        raise SystemExit('mesh2cs: mesh shader without position or triangle indices')
    info["locs"].sort()
    remap = {}
    for (cls, pointee), pid in ptrs.items():
        if cls == 'Output' and ('Workgroup', pointee) in ptrs:
            remap[pid] = ptrs[('Workgroup', pointee)]
    u32 = next(re.match(r'\s*(%\w+) =', l).group(1) for l in lines if re.match(r'\s*%\w+ = OpTypeInt 32 0$', l))
    wu32 = ptrs.get(('Workgroup', u32))
    extra = []
    if wu32 is None:
        wu32 = new()
        extra.append('%s = OpTypePointer Workgroup %s' % (wu32, u32))
    vcount, pcount = new(), new()
    extra += ['%s = OpVariable %s Workgroup' % (vcount, wu32), '%s = OpVariable %s Workgroup' % (pcount, wu32)]
    out = []
    placed = False
    for l in lines:
        s = l.strip()
        if s in ('OpCapability MeshShadingEXT', 'OpCapability MeshShadingNV', 'OpExtension "SPV_EXT_mesh_shader"', 'OpExtension "SPV_NV_mesh_shader"'):
            continue
        if re.match(r'OpExecutionMode %s (Output\w+)' % re.escape(entry), s):
            continue
        a = re.match(r'OpDecorate (%\w+) ', s)
        if a and a.group(1) in outvars:
            continue
        a = re.match(r'OpName (%\w+) ', s)
        if a and a.group(1) in outvars:
            continue
        a = re.match(r'(%\w+) = OpTypePointer Output (%\w+)$', s)
        if a:
            if a.group(1) in remap:
                continue
            s = '%s = OpTypePointer Workgroup %s' % (a.group(1), a.group(2))
        a = re.match(r'(%\w+) = OpVariable (%\w+) Output$', s)
        if a:
            s = '%s = OpVariable %s Workgroup' % (a.group(1), a.group(2))
        if s.startswith('OpEntryPoint MeshEXT'):
            head, iface = s.split('"main"', 1)
            iface = ' '.join(x for x in iface.split() if x in invars)
            s = head.replace('OpEntryPoint MeshEXT', 'OpEntryPoint GLCompute', 1) + '"main" ' + iface
        a = re.match(r'OpSetMeshOutputsEXT (%\w+) (%\w+)$', s)
        if a:
            out += ['OpStore %s %s' % (vcount, a.group(1)), 'OpStore %s %s' % (pcount, a.group(2))]
            continue
        if not placed and re.match(r'(%\w+ = )?OpFunction ', s):
            out += extra
            placed = True
        if s.startswith('OpMemoryModel'):
            out.append(s)
            continue
        for k, v in remap.items():
            s = re.sub(re.escape(k) + r'\b', v, s)
        out.append(s)
    names[vcount] = 'xvk_vcount'
    names[pcount] = 'xvk_pcount'
    first = next(i for i, l in enumerate(out) if l.startswith('OpName') or l.startswith('OpDecorate') or l.startswith('OpMemberName'))
    out[first:first] = ['OpName %s "%s"' % (v, n) for v, n in names.items()]
    return '\n'.join(out) + '\n', info


def epilogue(hlsl, info, flip):
    t = re.search(r'\[numthreads\((\d+), (\d+), (\d+)\)\]', hlsl)
    if not t:
        raise SystemExit('mesh2cs: no numthreads')
    threads = int(t.group(1)) * int(t.group(2)) * int(t.group(3))
    n, p, locs = info["maxv"], info["maxp"], info["locs"]
    r = 4 * (1 + len(locs))
    b = n * r + p * 4
    decl = 'RWByteAddressBuffer xvk_mesh_out : register(u0, space11);\n'
    if 'cbuffer SPIRV_Cross_NumWorkgroups\n' in hlsl:
        hlsl = hlsl.replace('cbuffer SPIRV_Cross_NumWorkgroups\n', 'cbuffer SPIRV_Cross_NumWorkgroups : register(b0, space11)\n')
        groups = 'SPIRV_Cross_NumWorkgroups_1_count'
    else:
        decl += 'cbuffer xvk_mesh_groups : register(b0, space11) { uint4 xvk_groups; };\n'
        groups = 'xvk_groups'
    m = re.search(r'^\w+ main\(SPIRV_Cross_Input stage_input\)\n\{\n', hlsl, re.M)
    if not m or 'SV_GroupID' not in hlsl or 'SV_GroupIndex' not in hlsl:
        raise SystemExit('mesh2cs: main needs SV_GroupID and SV_GroupIndex inputs')
    gid = re.search(r'uint3 (\w+) : SV_GroupID', hlsl).group(1)
    lid = re.search(r'uint (\w+) : SV_GroupIndex', hlsl).group(1)
    hlsl = re.sub(r'^(\s*)(xvk_[vp]count = [^;]+;)$', r'\1if (%s == 0u) \2' % lid, hlsl, flags=re.M)
    body = ['    GroupMemoryBarrierWithGroupSync();',
            '    uint xvk_g = (stage_input.%s.z * %s.y + stage_input.%s.y) * %s.x + stage_input.%s.x;' % (gid, groups, gid, groups, gid),
            '    uint xvk_base = xvk_g * %du;' % b,
            '    uint xvk_vc = min(xvk_vcount, %du);' % n,
            '    uint xvk_pc = min(xvk_pcount, %du);' % p,
            '    for (uint xvk_v = stage_input.%s; xvk_v < xvk_vc; xvk_v += %du)' % (lid, threads),
            '    {',
            '        uint xvk_a = (xvk_base + xvk_v * %du) * 4u;' % r,
            '        xvk_mesh_out.Store4(xvk_a, asuint(xvk_pos[xvk_v]));']
    for k, loc in enumerate(locs):
        body.append('        xvk_mesh_out.Store4(xvk_a + %du, asuint(xvk_v%d[xvk_v]));' % (16 * (k + 1), loc))
    body += ['    }',
             '    for (uint xvk_p = stage_input.%s; xvk_p < %du; xvk_p += %du)' % (lid, p, threads),
             '    {',
             '        bool xvk_live = xvk_p < xvk_pc%s;' % (' && !xvk_cull[xvk_p]' if info["cull"] else ''),
             '        uint3 xvk_i = xvk_live ? min(xvk_idx[xvk_p], uint3(%du, %du, %du)) : uint3(0u, 0u, 0u);' % (n - 1, n - 1, n - 1),
             '        xvk_mesh_out.Store4((xvk_base + %du + xvk_p * 4u) * 4u, uint4(xvk_i, xvk_live ? 1u : 0u));' % (n * r),
             '    }']
    end = hlsl.index('\n}', m.end())
    hlsl = hlsl[:end] + '\n' + '\n'.join(body) + hlsl[end:]
    hlsl = decl + hlsl
    vs = ['ByteAddressBuffer xvk_mesh_in : register(t0, space11);', 'struct VSOut', '{', '    float4 pos : SV_Position;']
    for loc in locs:
        vs.append('    float4 v%d : TEXCOORD%d;' % (loc, loc))
    vs += ['};', '', 'VSOut main(uint vid : SV_VertexID)', '{',
           '    uint prim = vid / 3u;', '    uint corner = vid - prim * 3u;',
           '    uint g = prim / %du;' % p, '    uint p = prim - g * %du;' % p,
           '    uint base = g * %du;' % b,
           '    uint4 pr = xvk_mesh_in.Load4((base + %du + p * 4u) * 4u);' % (n * r),
           '    uint idx = corner == 0u ? pr.x : (corner == 1u ? pr.y : pr.z);',
           '    uint a = (base + idx * %du) * 4u;' % r,
           '    VSOut o;', '    o.pos = asfloat(xvk_mesh_in.Load4(a));']
    for k, loc in enumerate(locs):
        vs.append('    o.v%d = asfloat(xvk_mesh_in.Load4(a + %du));' % (loc, 16 * (k + 1)))
    if flip:
        vs.append('    o.pos.y = -o.pos.y;')
    vs += ['    if (pr.w == 0u) o.pos = asfloat(uint4(0x7fc00000u, 0x7fc00000u, 0x7fc00000u, 0x7fc00000u));', '    return o;', '}', '']
    info.update({"threads": threads, "record": r, "block": b})
    return hlsl, '\n'.join(vs)


if __name__ == '__main__':
    res = convert(open(sys.argv[1]).read())
    if res is None:
        sys.exit(3)
    open(sys.argv[2], 'w').write(res[0])
    json.dump(res[1], open(sys.argv[3], 'w'))
