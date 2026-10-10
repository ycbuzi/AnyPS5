# For every unmerged upstream PR head that touches core/libs/prx, find which of OUR remaining
# unimplemented functions it implements, and whether that implementation is hollow.
# usage: coverage.py <repo> <worktree> <todo-json> <out.tsv>
import subprocess, sys, json, importlib.util, pathlib, re
repo, wt, todo_json, out = sys.argv[1:5]
wt = pathlib.Path(wt)
def git(*a, cwd=repo):
    return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True, errors="replace")
spec = importlib.util.spec_from_file_location("progress", pathlib.Path(repo) / "tools" / "progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
hol = importlib.util.spec_from_file_location("hollow", "/tmp/claude-0/-home-user-AnyPS5/273e79d8-d6e6-502a-8da0-bb5a906a0d40/scratchpad/hollow_lib.py")
d = json.load(open(todo_json)); groups = (d["libraries"] if "libraries" in d else d)["groups"]
todo = {g["name"]: set(g["todo_names"]) for g in groups if g["todo"]}
def statements(body):
    b = body.strip().strip("{}"); b = re.sub(r"//[^\n]*", "", b); b = re.sub(r"/\*.*?\*/", "", b, flags=re.S)
    return [s.strip() for s in b.split(";") if s.strip()]
def hollow(body):
    st = statements(body)
    if not st: return True
    for s in st:
        s2 = re.sub(r"\s+", " ", s)
        if re.fullmatch(r"\(void\)\s*\w+", s2): continue
        if re.fullmatch(r"return\s+(?:-?\w+|\(\w+\)\s*-?\w+|static_cast<\w+>\(-?\w+\)|0x[0-9a-fA-F]+)", s2): continue
        if re.fullmatch(r"(?:if\s*\([^)]*\)\s*)?return\s+[A-Z_0-9x:]+", s2): continue
        if s2.startswith("throw ") or re.fullmatch(r"if\s*\(.*\)\s*throw .*", s2): continue
        if re.fullmatch(r"if\s*\([^{}]*\)\s*return\s+[\w:]+", s2): continue
        return False
    return True
if not wt.exists(): git("worktree", "add", "--detach", str(wt), "upstream/main")
prs = [l.split("\t")[0] for l in open("/tmp/claude-0/-home-user-AnyPS5/273e79d8-d6e6-502a-8da0-bb5a906a0d40/scratchpad/harvest.tsv") if not l.startswith("pr\t") and l.split("\t")[1] in ("clean", "conflict", "conflict-docs-only")]
with open(out, "w") as fh:
    fh.write("pr\tlib\tfunction\thollow\n")
    for n in prs:
        ref = f"refs/pr/{n}"
        mb = git("merge-base", "upstream/main", ref).stdout.strip()
        libs = sorted({c.split("/")[3] for c in git("diff", "--name-only", mb, ref, "--", "core/libs/prx").stdout.split() if len(c.split("/")) > 4})
        libs = [l for l in libs if l in todo]
        if not libs: continue
        if git("checkout", "-q", "--detach", ref, cwd=wt).returncode: continue
        for lib in libs:
            p = wt / "core" / "libs" / "prx" / lib
            if not p.is_dir(): continue
            g = progress.scan_library(p)
            hits = set(g["done_names"]) & todo[lib]
            if not hits: continue
            bodies = {}
            for src in p.rglob("*.cpp"):
                if "tests" in src.relative_to(p).parts: continue
                text = src.read_text(errors="ignore")
                for m in progress.DEFINITION.finditer(text):
                    if m.group(1) in hits and m.group(1) not in bodies:
                        bodies[m.group(1)] = text[m.end() - 1:progress.body_end(text, m.end() - 1) + 1]
            for name in sorted(hits):
                fh.write(f"{n}\t{lib}\t{name}\t{int(hollow(bodies.get(name, '')))}\n")
        fh.flush()
print("done")
