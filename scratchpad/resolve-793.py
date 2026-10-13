"""Apply the per-file conflict decisions for the 793-commit sync.

ours   - keep HEAD's side, drop upstream's
theirs - keep upstream's side, drop HEAD's
union  - keep both, HEAD's side first

Only files whose decision is mechanical appear here. The ones that need a real reading of a
contract (libSceNpSessionSignaling, the big Export.cpp and test files) are resolved by hand.
"""
import pathlib
import re
import sys

DECISIONS = {
    "core/libs/prx/libSceAgc/Acb/src/Control.cpp": "theirs",
    "core/libs/prx/libSceAgc/Unimplemented.cpp": "union",
    "core/libs/prx/libSceContentSearch/CMakeLists.txt": "ours",
    "core/libs/prx/libSceNgs2.native/include/Ngs2Types.hpp": "union",
    "core/libs/prx/libSceNpAuth/CMakeLists.txt": "union",
    "core/libs/prx/libSceNpPartner001/Export.cpp": "theirs",
    "core/libs/prx/libSceUlt/Export.cpp": "theirs",
    "core/libs/prx/libc/src/Formatting.cpp": "ours",
    "core/libs/prx/libkernel/Equeue/src/EventAccessors.cpp": "theirs",
    "core/libs/prx/libkernel/System/src/Coredump.cpp": "union",
    "core/libs/tests/GuestJson.cpp": "union",
    "core/libs/tests/GuestShare.cpp": "ours",
    "core/libs/tests/GuestStrings.cpp": "union",
}

PATTERN = re.compile(r"<<<<<<< HEAD\n(.*?)(?:\n)?=======\n(.*?)(?:\n)?>>>>>>> [^\n]*\n", re.S)

root = pathlib.Path("D:/AnyPS5")
failures = 0
for name, decision in DECISIONS.items():
    path = root / name
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    count = len(PATTERN.findall(text))
    if count == 0:
        print(f"SKIP  {name}: no conflict markers left")
        continue

    def replace(match):
        mine, theirs = match.group(1), match.group(2)
        if decision == "ours":
            body = mine
        elif decision == "theirs":
            body = theirs
        else:
            parts = [p for p in (mine, theirs) if p.strip()]
            body = "\n".join(parts)
        return body + "\n" if body else ""

    path.write_text(PATTERN.sub(replace, text), encoding="utf-8", errors="surrogateescape")
    left = len(PATTERN.findall(path.read_text(encoding="utf-8", errors="surrogateescape")))
    if left:
        print(f"FAIL  {name}: {left} markers remain")
        failures += 1
    else:
        print(f"{decision:6s} {name} ({count} hunk{'s' if count != 1 else ''})")

sys.exit(1 if failures else 0)
