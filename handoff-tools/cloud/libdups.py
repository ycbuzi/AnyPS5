# Report function names defined more than once across a library's non-test .cpp files
# (link-time "multiple definition"), comparing HEAD against a base commit.
# usage: libdups.py <base-commit>   (run in the repo root)
import subprocess, sys, re, collections, importlib.util, pathlib
spec = importlib.util.spec_from_file_location("progress", "tools/progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
def defs(rev):
    files = subprocess.run(["git", "ls-tree", "-r", "--name-only", rev, "core/libs/prx"], capture_output=True, text=True).stdout.split()
    c = collections.Counter()
    for f in files:
        if not f.endswith(".cpp") or "/tests/" in f: continue
        lib = f.split("/")[3]
        text = subprocess.run(["git", "show", f"{rev}:{f}"], capture_output=True, text=True, errors="replace").stdout
        text = re.sub(r"//[^\n]*", "", text)
        for m in progress.DEFINITION.finditer(text): c[(lib, m.group(1))] += 1
    return c
base, head = defs(sys.argv[1]), defs("HEAD")
for k, v in sorted(head.items()):
    if v > 1 and v > base.get(k, 0): print(f"  {k[0]}: {k[1]} defined {v}x (base {base.get(k, 0)}x)")
