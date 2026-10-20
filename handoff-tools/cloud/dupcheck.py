# usage: dupcheck.py <merged-ref-or-WORKTREE> <parent1> <parent2>
# Reports add_test names and export NIDs that are duplicated in the merged tree but in neither parent.
import subprocess, re, sys, collections
def files(ref, pattern):
    if ref == "WORKTREE":
        out = subprocess.run(["git", "ls-files", pattern], capture_output=True, text=True).stdout.split()
        return {f: open(f, encoding="utf-8", errors="replace").read() for f in out}
    out = subprocess.run(["git", "ls-tree", "-r", "--name-only", ref], capture_output=True, text=True).stdout.split()
    rx = re.compile(pattern.replace("*", ".*") + "$")
    res = {}
    for f in out:
        if rx.search(f):
            res[f] = subprocess.run(["git", "show", f"{ref}:{f}"], capture_output=True, text=True, errors="replace").stdout
    return res
def tests(ref):
    c = collections.Counter()
    for f, s in files(ref, "*CMakeLists.txt").items():
        for n in re.findall(r"add_test\(NAME\s+([A-Za-z0-9_${}]+)", s): c[(f, n)] += 1
    return c
def exports(ref):
    c = collections.Counter()
    for f, s in files(ref, "core/libs/prx/*.cpp").items():
        for n in re.findall(r'APS5_EXPORT[A-Z_]*\(\s*"([^"]+)"', s): c[n] += 1
    return c
merged, p1, p2 = sys.argv[1:4]
for name, fn in (("add_test", tests), ("export", exports)):
    m, a, b = fn(merged), fn(p1), fn(p2)
    bad = [k for k, v in m.items() if v > 1 and v > max(a.get(k, 0), b.get(k, 0))]
    print(f"{name}: {len(bad)} new duplicates", bad[:20])
