"""Summarise each remaining conflict as the data that actually decides it.

For every hunk: which functions each side defines, and whether upstream's names already
exist elsewhere in the merged file. That is the whole decision:

  * upstream name already defined elsewhere -> their side is a stub that would DUPLICATE
    or downgrade our real implementation; drop it
  * disjoint names, neither defined elsewhere -> union, because taking one side silently
    deletes the other's export
  * same name both sides -> a real disagreement; read the bodies
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path("D:/AnyPS5")
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
DEFN = re.compile(r"\bAPS5_VABI\s+(\w+)\s*\(")

only = sys.argv[1:]
files = subprocess.run(["git", "diff", "--name-only", "--diff-filter=U"],
                       cwd=ROOT, capture_output=True, text=True).stdout.split()
if only:
    files = [f for f in files if any(o in f for o in only)]

for name in files:
    path = ROOT / name
    body = path.read_text(encoding="utf-8", errors="replace")
    outside = HUNK.sub("", body)
    print(f"\n=== {name}")
    for i, (ours, theirs) in enumerate(HUNK.findall(body), 1):
        a, b = set(DEFN.findall(ours)), set(DEFN.findall(theirs))
        dup = {n for n in b if re.search(r"APS5_VABI\s+" + n + r"\s*\(", outside)}
        both = a & b
        verdict = "READ BODIES (same name both sides)" if both else (
            "DROP their duplicates, keep rest" if dup else (
                "UNION (disjoint)" if a and b else "inspect"))
        print(f"  hunk {i}: ours={sorted(a) or '-'}")
        print(f"          theirs={sorted(b) or '-'}")
        if dup:
            print(f"          ALREADY DEFINED ELSEWHERE: {sorted(dup)}")
        if both:
            print(f"          SAME NAME BOTH SIDES: {sorted(both)}")
        print(f"          -> {verdict}")
