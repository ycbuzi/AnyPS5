import os, struct, sys

for f in sys.argv[1:]:
    d = open(f, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    o = pe + 24
    ch = struct.unpack_from("<H", d, pe + 22)[0]
    base = struct.unpack_from("<Q", d, o + 24)[0]
    size = struct.unpack_from("<I", d, o + 56)[0]
    dll = struct.unpack_from("<H", d, o + 70)[0]
    stack = struct.unpack_from("<Q", d, o + 72)[0]
    reloc = struct.unpack_from("<II", d, o + 112 + 5 * 8)
    tls = struct.unpack_from("<II", d, o + 112 + 9 * 8)
    entry = struct.unpack_from("<I", d, o + 16)[0]
    print(os.path.basename(f), "base", hex(base), "size", hex(size), "chars", hex(ch), "dllchars", hex(dll), "stack", hex(stack), "reloc", reloc, "tls", tls, "entry", hex(entry))
