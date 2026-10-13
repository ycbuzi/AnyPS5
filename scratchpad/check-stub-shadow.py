"""Find NotImplemented stubs that collide with a real implementation in the same library.

A sync re-introduces upstream's Unimplemented.cpp stub for a function we (or upstream) have since
implemented in a sibling file. The link then fails with "multiple definition", one symbol per build
attempt, which is the slowest possible way to find them. This lists all of them at once.

Only definitions count, not declarations: a definition has its opening brace on the same line.
"""
import collections
import pathlib
import re

DEFN = re.compile(r"^[^\n/]*\bAPS5_VABI\s+(\w+)\s*\([^)]*\)[^;{]*\{", re.M)
root = pathlib.Path("D:/AnyPS5/core/libs/prx")

findings = []
for library in sorted(p for p in root.iterdir() if p.is_dir()):
    stubs = {}
    real = collections.defaultdict(list)
    for source in library.rglob("*.cpp"):
        if "build" in source.parts:
            continue
        text = source.read_text(encoding="utf-8", errors="replace")
        names = set(DEFN.findall(text))
        if source.name == "Unimplemented.cpp":
            for name in names:
                stubs[name] = source
        else:
            for name in names:
                real[name].append(source)
    for name, stub in sorted(stubs.items()):
        if name in real:
            where = ", ".join(str(p.relative_to(library)) for p in real[name])
            findings.append(f"{library.name}: {name} stubbed in Unimplemented.cpp, implemented in {where}")

print("\n".join(findings) if findings else "clean: no stub shadows a real implementation")
print(f"\n{len(findings)} collision(s) across {sum(1 for p in root.iterdir() if p.is_dir())} libraries")
