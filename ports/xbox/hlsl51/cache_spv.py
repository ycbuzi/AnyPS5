import glob, os, struct, sys

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
count = 0
for f in sorted(glob.glob(os.path.join(src, "*.bin"))):
    b = open(f, "rb").read()
    start = 0
    n = 0
    while True:
        i = b.find(struct.pack("<I", 0x07230203), start)
        if i < 8:
            break
        words = struct.unpack_from("<Q", b, i - 8)[0]
        if words < 5 or i + words * 4 > len(b):
            start = i + 4
            continue
        open(os.path.join(dst, "%s_%d.spv" % (os.path.basename(f)[:16], n)), "wb").write(b[i:i + words * 4])
        n += 1
        count += 1
        start = i + words * 4
print("extracted", count)
