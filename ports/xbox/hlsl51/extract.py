import glob, os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
n = 0
for game in sys.argv[2:]:
    tag = os.path.basename(game.rstrip("/\\"))[:24].replace(" ", "_")
    for f in glob.glob(os.path.join(game, "shader_cache", "*", "*.bin")):
        b = open(f, "rb").read()
        start = 0
        while True:
            i = b.find(b"\x03\x02\x23\x07", start)
            if i < 8:
                break
            words = struct.unpack_from("<Q", b, i - 8)[0]
            if 5 <= words and i + words * 4 <= len(b):
                open(os.path.join(out, "%s_%s_%d.spv" % (tag, os.path.basename(f)[:12], i)), "wb").write(b[i:i + words * 4])
                n += 1
                start = i + words * 4
            else:
                start = i + 4
print(n)
