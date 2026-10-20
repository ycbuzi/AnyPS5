# Rebuild one file by replaying every first-parent merge in <start>..HEAD with the fixed additive
# resolver (keep both sides verbatim). Prints the diff against HEAD and writes the result to <out>.
# usage: replay_file.py <file> <start-commit> <out>
import subprocess, sys, re, tempfile, os
f, start, out = sys.argv[1:4]
def git(*a, inp=None, ok=(0,)):
    r = subprocess.run(["git", *a], capture_output=True, text=True, errors="replace", input=inp)
    return r
def show(rev):
    r = git("show", f"{rev}:{f}"); return r.stdout if r.returncode == 0 else ""
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
cur = show(start)
commits = git("rev-list", "--reverse", "--first-parent", f"{start}..HEAD").stdout.split()
for c in commits:
    parents = git("log", "-1", "--format=%P", c).stdout.split()
    if len(parents) < 2:
        if git("diff", "--quiet", f"{c}^1", c, "--", f).returncode: print("non-merge commit changed the file:", c[:9]); sys.exit(1)
        continue
    p1, p2 = parents[0], parents[1]
    base = git("merge-base", p1, p2).stdout.strip()
    if show(base) == show(p2): continue                      # this PR didn't touch the file
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, content in (("ours", cur), ("base", show(base)), ("theirs", show(p2))):
            p = os.path.join(d, name); open(p, "w").write(content); paths.append(p)
        r = git("merge-file", "-p", "--diff3", "-L", "ours", "-L", "base", "-L", "theirs", *paths)
        merged = r.stdout
        if r.returncode < 0: print("merge-file error", r.stderr); sys.exit(1)
        bad = [False]
        def keep_both(m):
            if m.group(2).strip(): bad[0] = True; return m.group(0)
            o, t = m.group(1), m.group(3)
            if o and not o.endswith("\n"): o += "\n"
            return o + t
        merged = HUNK.sub(keep_both, merged)
        if bad[0] or "<<<<<<<" in merged: print("non-additive conflict at", c[:9]); sys.exit(1)
        print(f"replayed {c[:9]} ({r.returncode} conflict hunk(s)) {git('log', '-1', '--format=%s', c).stdout.strip()[:70]}")
        cur = merged
open(out, "w").write(cur)
print("parens: open", cur.count("("), "close", cur.count(")"))
