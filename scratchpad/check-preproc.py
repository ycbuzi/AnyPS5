"""Check #if/#endif balance in merged files.

A third splice class, after unbalanced braces and duplicate definitions: a conflict hunk can
straddle a preprocessor conditional, so resolving hunk-by-hunk drops the opening #ifdef and
leaves an orphan #else/#endif. That is what broke Exception.cpp's sceKernelRaiseException,
where upstream's #ifdef _WIN32 lived just above the hunk boundary.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path("D:/AnyPS5")
OPEN = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b", re.M)
CLOSE = re.compile(r"^\s*#\s*endif\b", re.M)
MIDDLE = re.compile(r"^\s*#\s*(else|elif)\b", re.M)

files = subprocess.run(["git", "diff", "--cached", "--name-only"],
                       cwd=ROOT, capture_output=True, text=True).stdout.split()
files = [f for f in files if f.endswith((".cpp", ".hpp", ".h"))]

bad = 0
for name in files:
    path = ROOT / name
    if not path.exists():
        continue
    body = path.read_text(encoding="utf-8", errors="replace")
    depth, orphan = 0, False
    for line in body.splitlines():
        if OPEN.match(line):
            depth += 1
        elif CLOSE.match(line):
            depth -= 1
            if depth < 0:
                orphan = True
                break
        elif MIDDLE.match(line) and depth == 0:
            orphan = True
            break
    if orphan or depth != 0:
        bad += 1
        print(f"  UNBALANCED {name}  (final depth {depth}{', orphan #else/#endif' if orphan else ''})")

print(f"scanned {len(files)} merged file(s); {bad} with unbalanced preprocessor blocks")
sys.exit(0)
