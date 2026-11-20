import collections, glob, json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bary
import intsample
import mesh2cs

cross = r"C:\AnyPS5-work\xbox-probe\3rdparty\SPIRV-Cross\build\spirv-cross.exe"
fxc = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
spvtools = os.environ.get("XVK_SPIRV_TOOLS", r"C:\AnyPS5-work\android-sdk\ndk\29.0.14206865\shader-tools\windows-x86_64")
lower64 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lower64.py")
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
MODELS = {"vert": "vs_5_1", "frag": "ps_5_1", "comp": "cs_5_1", "geom": "gs_5_1"}
DECL = re.compile(r"^(?:globallycoherent )?\w+(?:<[^>]*>)? \w+(?:\[(\d*)\])? : register\(([tusb])(\d+), space(\d+)\);?$", re.M)
KIND = {"t": "SRV", "u": "UAV", "b": "CBV", "s": "Sampler"}
FLIP = ["--flip-vert-y"] if os.environ.get("XVK_FLIP") else []
FLIP += ["--fixup-clipspace"] if os.environ.get("XVK_FIXUP_CLIP") else []


PACK = re.compile(r"^((?:globallycoherent )?\w+(?:<[^>]*>)? (\w+)(?:\[(\d*)\])? : register\()([tusb])(\d+), space(\d+)(\);?)$", re.M)


def pack(text):
    decls = sorted((m for m in PACK.finditer(text) if m.group(6) != "11"), key=lambda m: (m.group(4), int(m.group(6)), int(m.group(5))))
    nxt, layout, new = collections.Counter(), [], {}
    for m in decls:
        key = (m.group(4), int(m.group(6)))
        count = int(m.group(3)) if m.group(3) else (0 if m.group(3) == "" else 1)
        layout.append({"name": m.group(2), "kind": KIND[m.group(4)], "space": key[1], "binding": int(m.group(5)), "count": count, "register": nxt[key]})
        new[m.start()] = "%s%s%d, space%d%s" % (m.group(1), m.group(4), nxt[key], key[1], m.group(7))
        nxt[key] += count or 1
    out, last = [], 0
    for m in PACK.finditer(text):
        if m.start() not in new:
            continue
        out.append(text[last:m.start()])
        out.append(new[m.start()])
        last = m.end()
    out.append(text[last:])
    return "".join(out), layout


def rootsig(text, profile):
    spans = collections.defaultdict(list)
    unbounded = []
    for m in DECL.finditer(text):
        reg, slot, space = m.group(2), int(m.group(3)), int(m.group(4))
        if space == 11:
            continue
        if m.group(1) == "":
            unbounded.append("%s(%s%d, space=%d, numDescriptors=unbounded)" % (KIND[reg], reg, slot, space))
            continue
        count = int(m.group(1) or 1)
        spans[(reg, space)].append([slot, slot + count])
    views, samplers = [], []
    for (reg, space), items in sorted(spans.items()):
        merged = []
        for lo, hi in sorted(items):
            if merged and lo <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], hi)
            else:
                merged.append([lo, hi])
        for lo, hi in merged:
            entry = "%s(%s%d, space=%d, numDescriptors=%d)" % (KIND[reg], reg, lo, space, hi - lo)
            (samplers if reg == "s" else views).append(entry)
    parts = ["RootFlags(%s)" % ("ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT" if profile.startswith("vs") else "0")]
    if views:
        parts.append("DescriptorTable(%s)" % ", ".join(views))
    if samplers:
        parts.append("DescriptorTable(%s)" % ", ".join(samplers))
    for u in unbounded:
        parts.append("DescriptorTable(%s)" % u)
    if "register(u0, space11)" in text:
        parts.append("RootConstants(num32BitConstants=4, b0, space=11)")
        parts.append("UAV(u0, space=11)")
    return ", ".join(parts)


ok, buckets, example = 0, collections.Counter(), {}
files = sorted(glob.glob(os.path.join(src, "*.spv")))
for f in files:
    base = os.path.join(dst, os.path.splitext(os.path.basename(f))[0])
    mesh = None
    if os.path.exists(lower64):
        d = subprocess.run([os.path.join(spvtools, "spirv-dis.exe"), "--raw-id", f, "-o", base + ".in.spvasm"], capture_output=True, text=True)
        if d.returncode == 0 and "OpEntryPoint MeshEXT" in open(base + ".in.spvasm").read():
            try:
                conv = mesh2cs.convert(open(base + ".in.spvasm").read())
            except SystemExit as e:
                key = str(e)[:120]
                buckets[key] += 1
                example.setdefault(key, os.path.basename(f))
                continue
            mesh = conv[1]
            open(base + ".in.spvasm", "w").write(conv[0])
            a = subprocess.run([os.path.join(spvtools, "spirv-as.exe"), "--target-env", "vulkan1.1", base + ".in.spvasm", "-o", base + ".cs.spv"], capture_output=True, text=True)
            if a.returncode:
                key = "mesh2cs as: " + re.sub(r"\d+", "N", (a.stderr.strip().splitlines() or ["?"])[-1])[:120]
                buckets[key] += 1
                example.setdefault(key, os.path.basename(f))
                continue
            f = base + ".cs.spv"
            buckets["(mesh2cs)"] += 1
        lw = subprocess.run([sys.executable, lower64, base + ".in.spvasm", base + ".low.spvasm"], capture_output=True, text=True) if d.returncode == 0 else d
        if lw.returncode == 0:
            a = subprocess.run([os.path.join(spvtools, "spirv-as.exe"), "--target-env", "vulkan1.1", base + ".low.spvasm", "-o", base + ".low.spv"], capture_output=True, text=True)
            if a.returncode:
                key = "lower64 as: " + re.sub(r"\d+", "N", (a.stderr.strip().splitlines() or ["?"])[-1])[:120]
                buckets[key] += 1
                example.setdefault(key, os.path.basename(f))
                continue
            f = base + ".low.spv"
            buckets["(lower64)"] += 1
        elif lw.returncode != 3:
            key = "lower64: " + re.sub(r"\d+", "N", ((lw.stderr or lw.stdout).strip().splitlines() or ["?"])[-1])[:120]
            buckets[key] += 1
            example.setdefault(key, os.path.basename(f))
            continue
    r = subprocess.run([cross, f] + FLIP + ["--hlsl", "--shader-model", "51", "--output", base + ".hlsl"], capture_output=True, text=True)
    gs = None
    if r.returncode and ("barycentrics" in r.stderr or "non-float textures" in r.stderr):
        r = subprocess.run([cross, f] + FLIP + ["--hlsl", "--shader-model", "67", "--output", base + ".hlsl"], capture_output=True, text=True)
        if r.returncode == 0:
            lowered = bary.lower(open(base + ".hlsl", encoding="utf-8").read())
            if lowered:
                open(base + ".hlsl", "w", encoding="utf-8").write(lowered[0])
                gs = lowered[1]
    if r.returncode:
        key = "spirv-cross: " + re.sub(r"\d+", "N", (r.stderr.strip().splitlines() or ["?"])[-1])[:120]
        buckets[key] += 1
        example.setdefault(key, os.path.basename(f))
        continue
    if gs is not None:
        open(base + ".gs.hlsl", "w", encoding="utf-8").write(gs)
        g = subprocess.run([fxc, "/nologo", "/T", "gs_5_1", "/E", "main", "/O3", "/Fo", base + ".gs.dxbc", base + ".gs.hlsl"], capture_output=True, text=True)
        if g.returncode:
            lines = [l for l in (g.stderr + g.stdout).splitlines() if "error" in l]
            key = "gs: " + re.sub(r"\d+", "N", re.sub(r"^.*?error", "error", lines[0]) if lines else "?")[:120]
            buckets[key] += 1
            example.setdefault(key, os.path.basename(f))
            continue
        buckets["(bary lowered)"] += 1
    text = open(base + ".hlsl", encoding="utf-8").read()
    text = re.sub(r"\.(Load|Store)<uint([234]?)>\(", lambda t: ".%s%s(" % (t.group(1), t.group(2)), text)
    for g in re.finditer(r"^(?:globallycoherent )?RWByteAddressBuffer (\w+)\[\] : register\(u\d+, space10\);$", text, re.M):
        if not re.search(r"\b%s\[[^;]*?\]\.(Store|Interlocked)" % g.group(1), text):
            text = text.replace(g.group(0), "ByteAddressBuffer %s[] : register(t0, space10);" % g.group(1))
    if mesh is not None:
        text, vs = mesh2cs.epilogue(text, mesh, bool(FLIP))
        open(base + ".vs.hlsl", "w", encoding="utf-8").write(vs.replace("VSOut main(", '[RootSignature("RootFlags(0), SRV(t0, space=11)")]\nVSOut main(', 1))
        v = subprocess.run([fxc, "/nologo", "/T", "vs_5_1", "/E", "main", "/O3", "/Fo", base + ".vs.dxbc", base + ".vs.hlsl"], capture_output=True, text=True)
        if v.returncode:
            key = "mesh vs: " + re.sub(r"\d+", "N", ([l for l in (v.stderr + v.stdout).splitlines() if "error" in l] or ["?"])[0])[:120]
            buckets[key] += 1
            example.setdefault(key, os.path.basename(f))
            continue
        json.dump(mesh, open(base + ".mesh.json", "w"))
    pre = []
    if "uaddCarry(" in text:
        pre.append("uint uaddCarry(uint a, uint b, out uint carry) { uint r = a + b; carry = r < a ? 1u : 0u; return r; }")
    if "usubBorrow(" in text:
        pre.append("uint usubBorrow(uint a, uint b, out uint borrow) { borrow = a < b ? 1u : 0u; return a - b; }")
    if "umulExtended(" in text or "imulExtended(" in text:
        pre.append("void umulExtended(uint a, uint b, out uint hi, out uint lo) { uint al = a & 0xFFFFu, ah = a >> 16, bl = b & 0xFFFFu, bh = b >> 16; uint ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh; uint mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu); lo = a * b; hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16); }")
        pre.append("void imulExtended(int a, int b, out int hi, out int lo) { uint h, l; umulExtended(asuint(a), asuint(b), h, l); h -= (a < 0 ? asuint(b) : 0u) + (b < 0 ? asuint(a) : 0u); hi = asint(h); lo = asint(l); }")
    types = dict((v, t) for t, v in re.findall(r"^\s*(\w+) (_\d+);$", text, re.M))
    text = re.sub(r"^(\s*)(_\d+) = \{ (.*) \};$", lambda a: "%s{ %s %s_init = { %s }; %s = %s_init; }" % (a.group(1), types.get(a.group(2), "?"), a.group(2), a.group(3), a.group(2), a.group(2)) if a.group(2) in types else a.group(0), text, flags=re.M)
    if "clockRealtime2x32EXT(" in text or "clock2x32ARB(" in text:
        pre.append("static uint g_clock = 0u;")
        pre.append("uint2 clockRealtime2x32EXT() { g_clock += 1024u; return uint2(g_clock, 0u); }")
        pre.append("uint2 clock2x32ARB() { return clockRealtime2x32EXT(); }")
    for m in re.finditer(r"^SamplerComparisonState (\w+)\[(\d+)\] : register\(s(\d+), space(\d+)\);", text, re.M):
        name = m.group(1)
        if re.search(r"\.Sample(Level|Grad|Bias)?\(%s\[" % name, text):
            text = text.replace(m.group(0), m.group(0) + "\nSamplerState %s_plain[%s] : register(s%s, space%d);" % (name, m.group(2), m.group(3), int(m.group(4)) + 1))
            text = re.sub(r"\.(Sample|SampleLevel|SampleGrad|SampleBias|Gather)\(%s\[" % name, lambda s: ".%s(%s_plain[" % (s.group(1), name), text)
    try:
        text, info = intsample.lower(text, None)
    except ValueError as e:
        key = "intsample: " + re.sub(r"\d+", "N", str(e))[:120]
        buckets[key] += 1
        example.setdefault(key, os.path.basename(f))
        continue
    if "IsHelperLane()" in text:
        pre.append("bool IsHelperLane() { return false; }")
    text, layout = pack(text)
    if info:
        layout.append({"sampler_info": info})
    json.dump(layout, open(base + ".layout.json", "w", encoding="utf-8"))
    stage = subprocess.run([cross, f, "--reflect"], capture_output=True, text=True).stdout
    m = re.search(r'"mode"\s*:\s*"(\w+)"', stage)
    profile = MODELS.get(m.group(1) if m else "comp", "cs_5_1")
    sig = rootsig(text, profile)
    text = re.sub(r"^(\w+) main\(", lambda a: '[RootSignature("%s")]\n%s main(' % (sig, a.group(1)), text, count=1, flags=re.M)
    open(base + ".hlsl", "w", encoding="utf-8").write("\n".join(pre) + ("\n" if pre else "") + text)
    for opt in ("/O3", "/Od"):
        r = subprocess.run([fxc, "/nologo", "/T", profile, "/E", "main", opt, "/enable_unbounded_descriptor_tables", "/Fo", base + ".dxbc", base + ".hlsl"], capture_output=True, text=True)
        if r.returncode == 0 or "internal error" not in (r.stderr + r.stdout):
            break
    if r.returncode == 0:
        ok += 1
        continue
    lines = [l for l in (r.stderr + r.stdout).splitlines() if "error" in l]
    msg = re.sub(r"^.*?error", "error", lines[0]) if lines else "?"
    key = re.sub(r"\d+", "N", msg)[:150]
    buckets[key] += 1
    example.setdefault(key, os.path.basename(f))
print("dxbc ok %d / %d" % (ok, len(files)))
for k, v in buckets.most_common(10):
    print(v, k, "|", example.get(k, ""))
