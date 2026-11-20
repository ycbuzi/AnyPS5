import glob, os, sys
src = open("imports.py").read().split("game = sys.argv")[0]
ns = {}
exec(src, ns)
game, dll = sys.argv[1], sys.argv[2].lower()
for f in glob.glob(os.path.join(game, "*.dll")) + glob.glob(os.path.join(game, "libs", "*")):
    try:
        r = ns["imports"](f)
    except Exception:
        continue
    if dll in r:
        print(os.path.basename(f), r[dll])
