# For every stack2.py merge that resolved "additive" conflicts, verify no line the PR added was lost:
# at merge M (parent P, PR head R with merge-base B): count_M(line) >= count_P(line) + count_added(line).
# usage: dropcheck.py <range-start-commit>   (run in repo root)
import subprocess, sys, re, collections
def git(*a): return subprocess.run(["git", *a], capture_output=True, text=True, errors="replace").stdout
start = sys.argv[1]
merges = git("log", "--format=%H%x00%s%x00%b", "--first-parent", f"{start}..HEAD").split("\n")
for entry in git("log", "--format=%H%x01%B%x02", "--first-parent", f"{start}..HEAD").split("\x02"):
    if "\x01" not in entry: continue
    sha, body = entry.strip().split("\x01", 1)
    m = re.search(r"Additive conflicts resolved by keeping both sides: (.+?)\.(?:\n|$)", body)
    pr = re.search(r"AnyPS5#(\d+)", body)
    if not m or not pr: continue
    files = [f.strip() for f in m.group(1).split(",")]
    ref = f"refs/pr/{pr.group(1)}"
    base = git("merge-base", "upstream/main", ref).strip()
    for f in files:
        added = collections.Counter(l[1:] for l in git("diff", base, ref, "--", f).splitlines() if l.startswith("+") and not l.startswith("+++"))
        before = collections.Counter(git("show", f"{sha}^1:{f}").splitlines())
        after = collections.Counter(git("show", f"{sha}:{f}").splitlines())
        dropped = {l: before[l] + n - after[l] for l, n in added.items() if after[l] < before[l] + n}
        status = "OK " if not dropped else "DROPPED"
        print(f"{status} PR {pr.group(1):>5} {sha[:9]} {f}")
        for l, n in dropped.items(): print(f"      -{n}x  {l[:150]}")
