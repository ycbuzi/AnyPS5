"""Resolve merge conflicts where both sides simply added different functions.

Most Export.cpp conflicts in an AnyPS5 sync are not disagreements: ours added one export
and upstream added another at the same point in the file. The resolution is the union, and
taking either side alone silently drops an implementation -- the exact regression shape that
has cost this project a dozen build cycles.

Only hunks where both sides define *disjoint* sets of function names are touched. Anything
else is left conflicted for a human decision. The linker is the backstop: a duplicate
definition fails loudly, a dropped one fails silently much later.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path("D:/AnyPS5")
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
# a definition line like:  int APS5_VABI sceFoo(...) {   /   void Bar(...) {
DEFN = re.compile(r"^[A-Za-z_][\w:<>,*&\s]*?\b(\w+)\s*\([^;]*\)\s*\{", re.M)

apply_changes = "--apply" in sys.argv
files = subprocess.run(["git", "diff", "--name-only", "--diff-filter=U"],
                       cwd=ROOT, capture_output=True, text=True).stdout.split()

resolved, skipped = [], []
for name in files:
    path = ROOT / name
    body = path.read_text(encoding="utf-8", errors="replace")
    changed = False
    out, last = [], 0
    for m in HUNK.finditer(body):
        ours, theirs = m.group(1), m.group(2)
        a, b = set(DEFN.findall(ours)), set(DEFN.findall(theirs))
        # union only when both sides actually define things and share no name
        if a and b and not (a & b):
            out.append(body[last:m.start()])
            out.append(ours if ours.endswith("\n") else ours + "\n")
            out.append(theirs)
            last = m.end()
            changed = True
    if changed:
        out.append(body[last:])
        merged = "".join(out)
        if "<<<<<<<" in merged:
            skipped.append((name, "partially resolvable"))
        else:
            resolved.append(name)
            if apply_changes:
                path.write_text(merged, encoding="utf-8")
    else:
        skipped.append((name, "no disjoint-union hunks"))

print(f"=== fully resolvable as a union: {len(resolved)} ===")
for n in resolved:
    print(f"  {n}")
print(f"\n=== left for judgement: {len(skipped)} ===")
for n, why in skipped:
    print(f"  {n}  ({why})")
if not apply_changes:
    print("\n(dry run -- pass --apply to write)")
