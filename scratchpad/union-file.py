"""Force a union resolution on named conflicted files: keep both sides of every hunk.

Used where both sides added different declarations or test blocks and the merged
implementation already contains both sets of functions, so testing only one side would
leave the other silently uncovered.
"""
import pathlib
import re
import sys

HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)

for name in sys.argv[1:]:
    path = pathlib.Path(name)
    body = path.read_text(encoding="utf-8", errors="replace")
    count = 0

    def join(match):
        global count
        count += 1
        ours, theirs = match.group(1), match.group(2)
        if ours and not ours.endswith("\n"):
            ours += "\n"
        return ours + theirs

    merged = HUNK.sub(join, body)
    path.write_text(merged, encoding="utf-8")
    left = merged.count("<<<<<<<")
    print(f"  {name}: unioned {count} hunk(s), {left} marker(s) left")
