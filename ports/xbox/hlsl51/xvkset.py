import glob, os, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
shutil.rmtree(dst, ignore_errors=True)
os.makedirs(dst)
n = {"compute": 0, "meshvs": 0}
for d in sorted(glob.glob(os.path.join(src, "*"))):
    name = os.path.basename(d)
    base = os.path.join(d, name)
    if not os.path.exists(base + ".dxbc"):
        continue
    hlsl = open(base + ".hlsl", encoding="utf-8", errors="replace").read() if os.path.exists(base + ".hlsl") else ""
    if "[numthreads(" in hlsl:
        shutil.copy(base + ".dxbc", os.path.join(dst, name + ".dxbc"))
        n["compute"] += 1
    if os.path.exists(base + ".vs.dxbc"):
        shutil.copy(base + ".vs.dxbc", os.path.join(dst, name + "m.dxbc"))
        open(os.path.join(dst, name + "m.gfx"), "w").write("kind vertex\n")
        n["meshvs"] += 1
print(n)
