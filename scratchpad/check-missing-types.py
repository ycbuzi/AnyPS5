"""Find type definitions upstream's SceTypes.hpp has that ours lost in the merge.

The SceTypes.hpp conflict was resolved by hand-writing the merged block, which silently
dropped struct ContentExportCallbackParam from upstream's side and only surfaced three
builds later as "does not name a type". Compare the two files' declared names directly.
"""
import re
import subprocess

DECL = re.compile(r"^\s*(?:struct|class|enum(?:\s+class)?|union)\s+(\w+)", re.M)
ALIAS = re.compile(r"^\s*using\s+(\w+)\s*=", re.M)

ours = open("D:/AnyPS5/core/libs/SceTypes.hpp", encoding="utf-8", errors="replace").read()
theirs = subprocess.run(["git", "show", "origin/main:core/libs/SceTypes.hpp"],
                        cwd="D:/AnyPS5", capture_output=True, text=True).stdout


def names(text):
    return set(DECL.findall(text)) | set(ALIAS.findall(text))


o, t = names(ours), names(theirs)
missing = sorted(t - o)
extra = sorted(o - t)
print(f"ours={len(o)} upstream={len(t)}")
print(f"\n=== in upstream, MISSING from ours ({len(missing)}) ===")
for n in missing:
    print(f"  {n}")
print(f"\n=== only in ours ({len(extra)}) -- harvested additions, expected ===")
for n in extra[:20]:
    print(f"  {n}")
