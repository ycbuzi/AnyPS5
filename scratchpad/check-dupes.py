"""Find functions defined twice within one file after a merge.

Two merge resolutions produced redefinitions that only the compiler caught, each costing a
full build cycle: upstream had since implemented logb and __powisf2, so the harvested copies
became duplicates. Scanning for repeated APS5_VABI definitions takes seconds.

Conditional blocks mean a repeated name is not always a bug -- libkernel registers one test
target in both an if(WIN32) and an elseif(Linux) branch -- so this reports candidates to
look at, not verdicts.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path("D:/AnyPS5")
DEFN = re.compile(r"^[^\n/]*\bAPS5_VABI\s+(\w+)\s*\([^;]*$", re.M)
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)

files = subprocess.run(["git", "diff", "--cached", "--name-only"],
                       cwd=ROOT, capture_output=True, text=True).stdout.split()
files = [f for f in files if f.endswith((".cpp", ".hpp"))]

total = 0
for name in files:
    path = ROOT / name
    if not path.exists():
        continue
    body = BLOCK_COMMENT.sub("", path.read_text(encoding="utf-8", errors="replace"))
    # drop preprocessor-conditional contents is overkill; just count definitions
    seen = {}
    for match in DEFN.finditer(body):
        seen.setdefault(match.group(1), []).append(body[:match.start()].count("\n") + 1)
    dupes = {k: v for k, v in seen.items() if len(v) > 1}
    if dupes:
        total += len(dupes)
        print(f"\n{name}")
        for fn, lines in sorted(dupes.items()):
            print(f"  {fn}: lines {lines}")

print(f"\nscanned {len(files)} merged file(s); {total} repeated definition(s) to review")
sys.exit(0)
