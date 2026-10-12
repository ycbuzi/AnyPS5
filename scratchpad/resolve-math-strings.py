"""Redo the libc Math.cpp and Strings.cpp conflicts correctly.

The first attempt unioned them and broke both. The reason is structural: our side of each
hunk *ends mid-function* -- its closing brace lives in the common context after the hunk --
so concatenating ours+theirs nests upstream's function inside ours and leaves the braces
unbalanced. Name-disjointness says nothing about that.

Math.cpp:    keep ours, then append upstream's three functions after the hunk's own tail.
Strings.cpp: both sides define wcstok and ours additionally defines wcscat/wcscspn/wcsncat,
             so ours is the superset and upstream's side is dropped entirely.
"""
import pathlib
import re

# `git checkout -m` relabels the markers "ours"/"theirs" rather than HEAD/origin-main,
# so match any label.
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\r?\n(.*?)>>>>>>> [^\n]*\n", re.S)

# --- Strings.cpp: ours is a strict superset ---------------------------------------
strings = pathlib.Path("D:/AnyPS5/core/libs/prx/libc/src/Strings.cpp")
body = strings.read_text(encoding="utf-8", errors="replace")
body, n = HUNK.subn(lambda m: m.group(1), body)
strings.write_text(body, encoding="utf-8")
print(f"Strings.cpp: kept ours for {n} hunk(s), markers={body.count('<<<<<<<')}, "
      f"wcstok defs={len(re.findall(r'APS5_VABI wcstok_nid_postfix', body))}")

# --- Math.cpp: ours, then upstream's three functions appended safely ---------------
math = pathlib.Path("D:/AnyPS5/core/libs/prx/libc/src/Math.cpp")
body = math.read_text(encoding="utf-8", errors="replace")
match = HUNK.search(body)
theirs = match.group(2)
body = body[:match.start()] + match.group(1) + body[match.end():]

# place upstream's block just before the final closing brace of the extern "C" block
index = body.rstrip().rfind("\n}")
body = body[:index + 1] + "\n" + theirs.strip("\n") + "\n" + body[index + 1:]
math.write_text(body, encoding="utf-8")
print(f"Math.cpp: markers={body.count('<<<<<<<')}")
for name in ("logb_nid_postfix", "__powisf2_nid_postfix", "_Fltrounds_nid_postfix",
             "_Sinh_nid_postfix", "__fpclassifyd_nid_postfix", "lrand48_nid_postfix"):
    print(f"  {name}: {len(re.findall(r'APS5_VABI ' + re.escape(name) + r'\s*\(', body))} definition(s)")
