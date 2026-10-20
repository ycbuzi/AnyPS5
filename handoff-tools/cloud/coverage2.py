# Re-classify coverage.tsv hits with a stricter hollow test (a call to an Unsupported*/Unimplemented*/
# NotImplemented* helper counts as a throw) and drop PRs rejected on merit or by design.
# usage: coverage2.py <repo> <worktree> <coverage.tsv> <out.tsv>
import subprocess, sys, importlib.util, pathlib, re, collections
repo, wt, cov_in, out = sys.argv[1:5]
wt = pathlib.Path(wt)
EXCLUDED = {"1498", "2193", "2634", "1450", "2498", "2495", "2465", "1743", "1880", "2224", "2228"}
def git(*a, cwd=repo):
    return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True, errors="replace")
spec = importlib.util.spec_from_file_location("progress", pathlib.Path(repo) / "tools" / "progress.py")
progress = importlib.util.module_from_spec(spec); spec.loader.exec_module(progress)
HELPER_THROW = re.compile(r"^\w*(Unsupported|Unimplemented|NotImplemented|NotSupported)\w*\s*\(")
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
        if HELPER_THROW.match(s2) or re.fullmatch(r"if\s*\(.*\)\s*" + HELPER_THROW.pattern[1:], s2): continue
        return False
    return True
rows = [l.rstrip("\n").split("\t") for l in open(cov_in)][1:]
wanted = collections.defaultdict(lambda: collections.defaultdict(set))   # pr -> lib -> names
for pr, lib, fn, h in rows:
    if pr not in EXCLUDED: wanted[pr][lib].add(fn)
with open(out, "w") as fh:
    fh.write("pr\tlib\tfunction\thollow\n")
    for pr in sorted(wanted, key=int):
        if git("checkout", "-q", "--detach", f"refs/pr/{pr}", cwd=wt).returncode: continue
        for lib, names in wanted[pr].items():
            p = wt / "core" / "libs" / "prx" / lib
            bodies = {}
            for src in p.rglob("*.cpp"):
                if "tests" in src.relative_to(p).parts: continue
                text = src.read_text(errors="ignore")
                for m in progress.DEFINITION.finditer(text):
                    if m.group(1) in names and m.group(1) not in bodies:
                        bodies[m.group(1)] = text[m.end() - 1:progress.body_end(text, m.end() - 1) + 1]
            for name in sorted(names):
                fh.write(f"{pr}\t{lib}\t{name}\t{int(hollow(bodies.get(name, '')))}\n")
        fh.flush()
print("done")
