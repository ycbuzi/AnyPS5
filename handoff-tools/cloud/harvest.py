# Score every unmerged upstream PR head (refs/pr/N) against a base commit of our branch.
# usage: python3 -I harvest.py <repo> <worktree-dir> <base-commit> <pr-list-file> <out.tsv> <details.jsonl>
# For each PR touching core/libs/prx: trial-merge into a scratch worktree, rescan only the touched
# libraries with tools/progress.py's own scan_library, and record the change in done/total.
import subprocess, sys, json, importlib.util, pathlib, re
repo, wt, base, prlist, out_tsv, out_json = sys.argv[1:7]
wt = pathlib.Path(wt)
def git(*a, cwd=repo, check=True):
    r = subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True, errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(a)}: {r.stderr.strip()[:300]}")
    return r
spec = importlib.util.spec_from_file_location("progress", pathlib.Path(repo) / "tools" / "progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
if not wt.exists():
    git("worktree", "add", "--detach", str(wt), base)
git("reset", "-q", "--hard", base, cwd=wt); git("clean", "-fdq", cwd=wt)
prx = wt / "core" / "libs" / "prx"
cache = {}
def scan(lib):
    p = prx / lib
    if not p.is_dir():
        return {"done": 0, "todo": 0, "done_names": [], "todo_names": []}
    return progress.scan_library(p)
def bodies(lib, names):
    # return short source excerpts for the given function names in a library
    res = {}
    p = prx / lib
    if not p.is_dir(): return res
    for src in p.rglob("*.cpp"):
        if "tests" in src.relative_to(p).parts: continue
        text = src.read_text(errors="ignore")
        for m in progress.DEFINITION.finditer(text):
            if m.group(1) in names and m.group(1) not in res:
                b = text[m.end() - 1:progress.body_end(text, m.end() - 1) + 1]
                res[m.group(1)] = b[:400]
    return res
prs = [l.split()[0] for l in open(prlist) if l.strip()]
with open(out_tsv, "w") as tsv, open(out_json, "w") as js:
    tsv.write("pr\tresult\td_done\td_total\tlibs\tconflicts\n")
    for n in prs:
        ref = f"refs/pr/{n}"
        try:
            mb = git("merge-base", "upstream/main", ref, check=False).stdout.strip()
            if not mb:
                tsv.write(f"{n}\tno-merge-base\t0\t0\t\t\n"); continue
            changed = git("diff", "--name-only", mb, ref, "--", "core/libs/prx").stdout.split()
            if not changed:
                tsv.write(f"{n}\tno-prx-change\t0\t0\t\t\n"); continue
            if git("merge-base", "--is-ancestor", ref, base, check=False).returncode == 0:
                tsv.write(f"{n}\tin-stack\t0\t0\t\t\n"); continue
            libs = sorted({c.split("/")[3] for c in changed if len(c.split("/")) > 4})
            before = {}
            for lib in libs:
                if lib not in cache: cache[lib] = scan(lib)
                before[lib] = cache[lib]
            r = git("merge", "--no-commit", "--no-ff", ref, cwd=wt, check=False)
            conflicts = git("diff", "--name-only", "--diff-filter=U", cwd=wt).stdout.split()
            if conflicts:
                only_docs = all(c == "docs/dev/TechnicalDebt.md" for c in conflicts)
                result = "conflict-docs-only" if only_docs else "conflict"
            elif r.returncode != 0:
                result = "merge-failed"
            else:
                result = "clean"
            d_done = d_total = 0
            new_done = {}
            if result == "clean" or result == "conflict-docs-only":
                for lib in libs:
                    after = scan(lib)
                    d_done += after["done"] - before[lib]["done"]
                    d_total += (after["done"] + after["todo"]) - (before[lib]["done"] + before[lib]["todo"])
                    added = sorted(set(after["done_names"]) - set(before[lib]["done_names"]))
                    if added: new_done[lib] = bodies(lib, set(added))
            tsv.write(f"{n}\t{result}\t{d_done}\t{d_total}\t{','.join(libs)}\t{','.join(conflicts[:6])}\n")
            if new_done:
                js.write(json.dumps({"pr": n, "result": result, "d_done": d_done, "d_total": d_total, "new_done": new_done}) + "\n")
        except Exception as e:
            tsv.write(f"{n}\terror\t0\t0\t\t{str(e)[:200]}\n")
        finally:
            git("merge", "--abort", cwd=wt, check=False)
            git("reset", "-q", "--hard", base, cwd=wt, check=False)
            git("clean", "-fdq", cwd=wt, check=False)
        tsv.flush(); js.flush()
print("done")
