import glob, os, struct, sys
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for f in glob.glob(os.path.join(src, "*.spv")):
    w = list(struct.unpack("<%dI" % (os.path.getsize(f) // 4), open(f, "rb").read()))
    out, i, var, deco = w[:5], 5, [], None
    while i < len(w):
        op, n = w[i] & 0xFFFF, w[i] >> 16
        ins = w[i:i + n]
        if op == 32 and ins[2] == 9:
            ins[2] = 12
        if op == 59 and ins[3] == 9:
            ins[3] = 12
            var.append(ins[2])
        if op == 71 and deco is None:
            deco = len(out)
        out += ins
        i += n
    extra = []
    for v in var:
        extra += [(4 << 16) | 71, v, 34, 7, (4 << 16) | 71, v, 33, 15, (3 << 16) | 71, v, 24]
    if deco is not None:
        out[deco:deco] = extra
    open(os.path.join(dst, os.path.basename(f)), "wb").write(struct.pack("<%dI" % len(out), *out))
