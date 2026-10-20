# Stack upstream PR heads (refs/pr/N) onto the current branch, keeping only those that raise the
# library completion % on OUR tree. Adapted from the PC session's stack-prs.sh.
# - changelog-only conflicts (docs/dev/TechnicalDebt.md) are resolved by keeping both sides' lines
# - any other conflict, an empty merge, or no % gain -> the merge is backed out
# usage: stack.py <repo> <pr> [pr...]
import subprocess, sys, re, importlib.util, pathlib
repo = pathlib.Path(sys.argv[1]); prs = sys.argv[2:]
def git(*a, check=True):
    r = subprocess.run(["git", *a], cwd=repo, capture_output=True, text=True, errors="replace")
    if check and r.returncode: raise RuntimeError(f"git {' '.join(a)}: {r.stderr.strip()[:300]}")
    return r
spec = importlib.util.spec_from_file_location("progress", repo / "tools" / "progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
prx = repo / "core" / "libs" / "prx"
def totals():
    groups = [progress.scan_library(p) for p in sorted(prx.iterdir()) if p.is_dir()]
    return sum(g["done"] for g in groups), sum(g["done"] + g["todo"] for g in groups)
def union_conflicts(path):
    text = (repo / path).read_text(encoding="utf-8")
    pat = re.compile(r"<<<<<<< [^\n]*\n(.*?)(?:\|\|\|\|\|\|\| [^\n]*\n.*?)?=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
    def keep_both(m):
        ours = m.group(1).splitlines(keepends=True); theirs = m.group(2).splitlines(keepends=True)
        return "".join(ours + [l for l in theirs if l not in ours])
    new = pat.sub(keep_both, text)
    if "<<<<<<<" in new or ">>>>>>>" in new: return False
    (repo / path).write_text(new, encoding="utf-8"); return True
if git("status", "--porcelain").stdout.strip(): sys.exit("ABORT: working tree is dirty")
start = git("rev-parse", "HEAD").stdout.strip()
git("tag", "-f", "stack-prestack-cloud", start)
done, total = totals()
print(f"start {start[:8]}  {done}/{total} = {100*done/total:.2f}%")
for n in prs:
    ref = f"refs/pr/{n}"
    r = git("merge", "--no-ff", "--no-commit", ref, check=False)
    conflicts = git("diff", "--name-only", "--diff-filter=U").stdout.split()
    note = ""
    if conflicts:
        if all(c == "docs/dev/TechnicalDebt.md" for c in conflicts) and union_conflicts("docs/dev/TechnicalDebt.md"):
            git("add", "docs/dev/TechnicalDebt.md"); note = " (changelog conflict: kept both)"
        else:
            git("merge", "--abort", check=False); print(f"  {n:>5}  CONFLICT {' '.join(conflicts[:4])}"); continue
    elif r.returncode:
        git("merge", "--abort", check=False); print(f"  {n:>5}  MERGE-FAILED"); continue
    if not git("diff", "--cached", "--name-only").stdout.strip():
        git("merge", "--abort", check=False); print(f"  {n:>5}  EMPTY (already present)"); continue
    d2, t2 = totals()
    if d2 <= done or d2 * total <= done * t2:
        git("merge", "--abort", check=False); git("reset", "-q", "--hard", "HEAD", check=False)
        print(f"  {n:>5}  NO-GAIN ({d2-done:+d}/{t2-total:+d})"); continue
    subject = git("log", "-1", "--format=%s", ref).stdout.strip()
    msg = (f"Merge PR boykopovar/AnyPS5#{n}: {subject}\n\nHarvested from upstream (closed unmerged); "
           f"library completion {done}/{total} -> {d2}/{t2}.{note}\n\n"
           "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\n"
           "Claude-Session: https://claude.ai/code/session_011krDVCnnMQPaEFEqCDnKmA\n")
    git("commit", "-q", "-F", "-", check=False) if False else subprocess.run(["git", "commit", "-q", "-F", "-"], cwd=repo, input=msg, text=True, check=True)
    print(f"  {n:>5}  APPLIED  {d2-done:+d}/{t2-total:+d}  -> {d2}/{t2} = {100*d2/t2:.2f}%{note}")
    done, total = d2, t2
print(f"end   {git('rev-parse', '--short', 'HEAD').stdout.strip()}  {done}/{total} = {100*done/total:.2f}%")
