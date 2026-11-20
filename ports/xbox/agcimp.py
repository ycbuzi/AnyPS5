import sys
src = open("imports.py").read().split("game = sys.argv")[0]
ns = {}
exec(src, ns)
for m, f in ns["imports"](sys.argv[1]).items():
    print(m, len(f), " ".join(f[:8]) if not m.startswith(("api-ms", "kernel32", "lib")) else "")
