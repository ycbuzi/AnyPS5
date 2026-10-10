# Like stack.py, but also resolves PURELY ADDITIVE conflicts (diff3 base section empty) in any file
# by keeping both sides, then rejects the PR if: a function becomes defined twice in a file, a test
# name or export NID becomes duplicated, or a changed .cpp fails `g++ -fsyntax-only` (where it
# compiled before). Keeps a PR only if library completion % rises on our tree.
# usage: stack2.py <repo> <pr> [pr...]
import subprocess, sys, re, importlib.util, pathlib, collections
repo = pathlib.Path(sys.argv[1]); prs = sys.argv[2:]
SYN = "/tmp/claude-0/-home-user-AnyPS5/273e79d8-d6e6-502a-8da0-bb5a906a0d40/scratchpad/syncheck.sh"
def git(*a, check=True, inp=None):
    r = subprocess.run(["git", "-c", "merge.conflictStyle=diff3", *a], cwd=repo, capture_output=True, text=True, errors="replace", input=inp)
    if check and r.returncode: raise RuntimeError(f"git {' '.join(a)}: {r.stderr.strip()[:300]}")
    return r
spec = importlib.util.spec_from_file_location("progress", repo / "tools" / "progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
prx = repo / "core" / "libs" / "prx"
def totals():
    groups = [progress.scan_library(p) for p in sorted(prx.iterdir()) if p.is_dir()]
    return sum(g["done"] for g in groups), sum(g["done"] + g["todo"] for g in groups)
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
def resolve_additive(path):
    text = (repo / path).read_text(encoding="utf-8", errors="replace")
    ok = [True]
    def keep_both(m):
        ours, base, theirs = m.group(1), m.group(2), m.group(3)
        o = ours.splitlines(keepends=True); t = theirs.splitlines(keepends=True)
        if base.strip():
            # Only the changelog may be merged when both sides edited the same lines: keep our
            # version of the edited bullets and add the bullets the other side introduced.
            if not path.endswith("TechnicalDebt.md"): ok[0] = False; return m.group(0)
            b = base.splitlines(keepends=True)
            return "".join(o + [l for l in t if l not in b and l not in o])
        if o and not o[-1].endswith("\n"): o[-1] += "\n"
        # Only the changelog gets per-line de-duplication (both sides may add the same bullet).
        # Code keeps every line of both sides: a repeated structural line such as a closing
        # parenthesis is not a duplicate, and dropping it broke libc/CMakeLists.txt on 2026-10-10.
        if path.endswith(".md"): return "".join(o + [l for l in t if l not in o])
        return "".join(o + t)
    new = HUNK.sub(keep_both, text)
    if not ok[0] or "<<<<<<<" in new or ">>>>>>>" in new: return False
    (repo / path).write_text(new, encoding="utf-8"); return True
def dup_defs(path):
    if not path.endswith(".cpp"): return []
    text = (repo / path).read_text(encoding="utf-8", errors="replace")
    c = collections.Counter(m.group(1) for m in progress.DEFINITION.finditer(text))
    return [n for n, v in c.items() if v > 1]
def dup_tests_exports():
    tests, exports = collections.Counter(), collections.Counter()
    for f in git("ls-files", "*CMakeLists.txt").stdout.split():
        text = (repo / f).read_text(errors="replace")
        for n in re.findall(r"add_test\(NAME\s+([A-Za-z0-9_${}]+)", text): tests[(f, n)] += 1
        # a target created twice (both sides of an add/add conflict kept) fails at configure time
        for n in re.findall(r"add_(?:library|executable|test_executable)\(\s*([A-Za-z0-9_${}]+)", text): tests[(f, "target:" + n)] += 1
    for f in git("ls-files", "core/libs/prx/*.cpp").stdout.split():
        for n in re.findall(r'APS5_EXPORT[A-Z_]*\(\s*"([^"]+)"', (repo / f).read_text(errors="replace")): exports[n] += 1
    return {k for k, v in tests.items() if v > 1}, {k for k, v in exports.items() if v > 1}
def syntax_fails(files):
    bad = []
    for f in files:
        if not f.endswith(".cpp") or not (repo / f).exists(): continue
        out = subprocess.run([SYN, str(repo), f], capture_output=True, text=True).stdout
        if out.startswith("FAIL"): bad.append(out.strip())
    return bad
if git("status", "--porcelain").stdout.strip(): sys.exit("ABORT: working tree is dirty")
start = git("rev-parse", "HEAD").stdout.strip(); git("tag", "-f", "stack-prestack-cloud2", start)
done, total = totals(); base_tests, base_exports = dup_tests_exports()
print(f"start {start[:8]}  {done}/{total} = {100*done/total:.2f}%")
for n in prs:
    ref = f"refs/pr/{n}"
    def back_out(msg):
        git("merge", "--abort", check=False); git("reset", "-q", "--hard", "HEAD", check=False); print(f"  {n:>5}  {msg}")
    r = git("merge", "--no-ff", "--no-commit", ref, check=False)
    conflicts = git("diff", "--name-only", "--diff-filter=U").stdout.split()
    resolved = []
    if conflicts:
        failed = [c for c in conflicts if not resolve_additive(c)]
        if failed: back_out(f"CONFLICT (not additive) {' '.join(failed[:3])}"); continue
        for c in conflicts: git("add", c)
        resolved = conflicts
    elif r.returncode: back_out("MERGE-FAILED"); continue
    changed = git("diff", "--cached", "--name-only").stdout.split()
    if not changed: back_out("EMPTY"); continue
    dd = [f"{c}:{','.join(dup_defs(c))}" for c in changed if dup_defs(c)]
    if dd: back_out(f"DUPLICATE-DEFINITION {' '.join(dd)[:150]}"); continue
    t, e = dup_tests_exports()
    if (t - base_tests) or (e - base_exports): back_out(f"DUPLICATE test/export {list(t - base_tests)[:2]} {list(e - base_exports)[:2]}"); continue
    syn = syntax_fails([c for c in changed if c.startswith("core/libs/prx/") or c.startswith("core/libs/tests/")])
    if syn:
        # only count it as a break if the file compiled before this merge
        git("stash", "-q", check=False)
        before = syntax_fails([s.split()[1] for s in syn])
        git("stash", "pop", "-q", check=False)
        new_breaks = [s for s in syn if s.split()[1] not in {b.split()[1] for b in before}]
        if new_breaks: back_out(f"SYNTAX {new_breaks[0][:160]}"); continue
    d2, t2 = totals()
    if d2 <= done or d2 * total <= done * t2: back_out(f"NO-GAIN ({d2-done:+d}/{t2-total:+d})"); continue
    subject = git("log", "-1", "--format=%s", ref).stdout.strip()
    note = f" Additive conflicts resolved by keeping both sides: {', '.join(resolved)}." if resolved else ""
    msg = (f"Merge PR boykopovar/AnyPS5#{n}: {subject}\n\nHarvested from upstream (closed unmerged); "
           f"library completion {done}/{total} -> {d2}/{t2}.{note}\n\n"
           "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\n"
           "Claude-Session: https://claude.ai/code/session_011krDVCnnMQPaEFEqCDnKmA\n")
    subprocess.run(["git", "commit", "-q", "-F", "-"], cwd=repo, input=msg, text=True, check=True)
    print(f"  {n:>5}  APPLIED  {d2-done:+d}/{t2-total:+d}  -> {d2}/{t2} = {100*d2/t2:.2f}%{'  (additive: ' + ','.join(resolved) + ')' if resolved else ''}")
    done, total = d2, t2
print(f"end   {git('rev-parse', '--short', 'HEAD').stdout.strip()}  {done}/{total} = {100*done/total:.2f}%")
