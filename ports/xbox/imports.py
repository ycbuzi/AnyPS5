import glob, os, struct, sys, collections
def imports(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    o = pe + 24
    dd = o + (112 if struct.unpack_from('<H', d, o)[0] == 0x20b else 96)
    rva = struct.unpack_from('<II', d, dd + 8)[0]
    secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + i * 40 + 8) for i in range(nsec)]
    def off(r):
        for vs, va, rs, ro in secs:
            if va <= r < va + max(vs, rs):
                return r - va + ro
    out = {}
    if not rva:
        return out
    p = off(rva)
    while True:
        ilt, _, _, name, iat = struct.unpack_from('<IIIII', d, p)
        if not name:
            break
        n = d[off(name):d.index(b'\0', off(name))].decode().lower()
        fns = []
        q = off(ilt or iat)
        while True:
            v = struct.unpack_from('<Q', d, q)[0]
            if not v:
                break
            if not v >> 63:
                h = off(v & 0x7fffffff)
                fns.append(d[h + 2:d.index(b'\0', h + 2)].decode())
            q += 8
        out[n] = fns
        p += 20
    return out
game = sys.argv[1]
files = glob.glob(os.path.join(game, "*.exe")) + glob.glob(os.path.join(game, "*.dll")) + glob.glob(os.path.join(game, "libs", "*.dll")) + glob.glob(os.path.join(game, "libs", "*.prx"))
own = set(os.path.basename(f).lower() for f in files)
need = collections.defaultdict(set)
for f in files:
    for m, fns in imports(f).items():
        if m not in own:
            need[m] |= set(fns)
with open(sys.argv[2], "w", newline="\n") as f:
    for m in sorted(need):
        for fn in sorted(need[m]):
            f.write("%s %s\n" % (m, fn))
print(sum(len(v) for v in need.values()), "imports from", len(need), "dlls")
