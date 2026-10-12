"""Find exports defined in more than one source file of the same library.

check-dupes.py only looked inside a single file, so it missed the case that actually broke
the link: our harvested ExtendedMath.cpp/NumericHelpers.cpp/BsdStrings.cpp define functions
upstream has since implemented in Math.cpp/Strings.cpp/FileAndHeap.cpp. Both files are in
libc's source list, so the linker sees two definitions.

Reports, per duplicated symbol, which files define it -- that is what decides whether a
harvested file is now wholly redundant or still carries unique exports.
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "D:/AnyPS5/core/libs/prx/libc/src")
# A definition has a body brace on the same line; a declaration ends in ");".
# Requiring "no semicolon to end of line" missed one-liners such as
#   long double APS5_VABI acosl_nid_postfix(long double x) { return std::acos(x); }
# which is exactly the class the linker was reporting and this scan was not.
DEFN = re.compile(r"^[^\n/]*\bAPS5_VABI\s+(\w+)\s*\([^)]*\)[^;{]*\{", re.M)
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)

owners = collections.defaultdict(set)
for path in ROOT.rglob("*.cpp"):
    body = BLOCK_COMMENT.sub("", path.read_text(encoding="utf-8", errors="replace"))
    for name in set(DEFN.findall(body)):
        owners[name].add(path.name)

dupes = {k: v for k, v in owners.items() if len(v) > 1}
print(f"{len(owners)} exported symbols across {len(list(ROOT.rglob('*.cpp')))} files; "
      f"{len(dupes)} defined in more than one file\n")

by_pair = collections.defaultdict(list)
for name, files in sorted(dupes.items()):
    by_pair[tuple(sorted(files))].append(name)

for files, names in sorted(by_pair.items(), key=lambda kv: -len(kv[1])):
    print(f"=== {' + '.join(files)} : {len(names)} symbol(s)")
    print(f"    {', '.join(names[:12])}{' ...' if len(names) > 12 else ''}")

# for each file involved, how many of its symbols are duplicated vs unique
print("\n=== per-file: unique vs duplicated ===")
per_file = collections.defaultdict(lambda: [0, 0])
for name, files in owners.items():
    for f in files:
        per_file[f][0 if len(files) == 1 else 1] += 1
for f, (unique, dup) in sorted(per_file.items(), key=lambda kv: -kv[1][1]):
    if dup:
        print(f"  {f:28s} unique={unique:4d}  duplicated={dup:4d}"
              + ("   <-- WHOLLY REDUNDANT" if unique == 0 else ""))
