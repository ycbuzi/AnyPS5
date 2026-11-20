import collections, glob, os, re, subprocess, sys
cross = r"C:\AnyPS5-work\xbox-probe\3rdparty\SPIRV-Cross\build\spirv-cross.exe"
files = sorted(glob.glob(os.path.join(sys.argv[1], "*.spv")))
buckets = collections.Counter()
example = {}
ok = 0
for f in files:
    r = subprocess.run([cross, f, "--hlsl", "--shader-model", sys.argv[2] if len(sys.argv) > 2 else "51", "--output", os.devnull], capture_output=True, text=True)
    if r.returncode == 0:
        ok += 1
        continue
    msg = (r.stderr.strip().splitlines() or ["?"])[-1]
    key = re.sub(r"\d+", "N", re.sub(r"\(name: [^)]*\)", "", msg))[:140]
    buckets[key] += 1
    example.setdefault(key, os.path.basename(f))
print("ok %d / %d" % (ok, len(files)))
for k, v in buckets.most_common(15):
    print(v, k, "|", example[k])
