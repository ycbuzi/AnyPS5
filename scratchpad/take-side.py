"""Resolve conflict hunks in place by keeping one side, preserving the rest of the merge.

`git checkout --ours FILE` during a merge restores the whole pre-merge file and silently
discards every non-conflicting upstream change in it. That is the same silent-revert shape
that has cost this project a dozen build cycles, so resolve the hunks, not the file.

    python take-side.py ours   path [path...]
    python take-side.py theirs path [path...]
"""
import pathlib
import re
import sys

HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
side = sys.argv[1]
if side not in ("ours", "theirs"):
    raise SystemExit("first argument must be 'ours' or 'theirs'")
group = 1 if side == "ours" else 2

for name in sys.argv[2:]:
    path = pathlib.Path(name)
    body = path.read_text(encoding="utf-8", errors="replace")
    count = len(HUNK.findall(body))
    merged = HUNK.sub(lambda m: m.group(group), body)
    path.write_text(merged, encoding="utf-8")
    print(f"  {name}: kept {side} for {count} hunk(s), {merged.count('<<<<<<<')} marker(s) left")
