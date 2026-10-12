"""Resolve a conflicted file hunk by hunk, each with its own decision.

Real files need mixed resolutions: an include block wants a union, a constant block wants a
union so both naming styles keep compiling, and a function block wants one side because the
other would duplicate an implementation that already exists elsewhere in the file.

    python resolve-hunks.py FILE union,union,ours

Directives, one per hunk, in order: union | ours | theirs
"""
import pathlib
import re
import sys

HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
path = pathlib.Path(sys.argv[1])
plan = sys.argv[2].split(",")
body = path.read_text(encoding="utf-8", errors="replace")
hunks = HUNK.findall(body)
if len(hunks) != len(plan):
    raise SystemExit(f"{path.name} has {len(hunks)} hunk(s) but {len(plan)} directive(s)")

index = [0]


def pick(match):
    how = plan[index[0]].strip()
    index[0] += 1
    ours, theirs = match.group(1), match.group(2)
    if how == "ours":
        return ours
    if how == "theirs":
        return theirs
    if how == "union":
        if ours and not ours.endswith("\n"):
            ours += "\n"
        return ours + theirs
    raise SystemExit(f"unknown directive {how!r}")


merged = HUNK.sub(pick, body)
path.write_text(merged, encoding="utf-8")
print(f"  {path.name}: {','.join(plan)} -> {merged.count('<<<<<<<')} marker(s) left")
