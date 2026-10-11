"""Mine every AnyPS5 issue for what real titles actually need.

We have almost no games, but dozens of contributors are testing them and writing
down exactly what breaks and which exports are missing. That is a free
requirements database: it turns "345 functions are unimplemented" into "these
functions are what shipping titles actually hit", ranked by how many titles hit them.
"""
import json
import re
import sys
from collections import defaultdict

issues = json.load(open(sys.argv[1], encoding="utf-8"))

TITLE_ID = re.compile(r"\bPPSA\d{5}\b")
SCE_FN = re.compile(r"\bsce[A-Z]\w{3,}\b")
POSIX_FN = re.compile(r"\b(?:pthread_\w+|sem_\w+|_umtx_op|getdents|getrusage|"
                      r"shm_\w+|clock_\w+|sigaction|sigprocmask|mmap|munmap|mprotect)\b")
# titles whose IDs start PPSA99 are public homebrew: things we could actually run
HOMEBREW = re.compile(r"\bPPSA99\d{3}\b")

by_title = defaultdict(lambda: {"issues": set(), "fns": set(), "name": ""})
fn_titles = defaultdict(set)
NAME_HINT = re.compile(r"([A-Z][\w':\- ]{2,40}?)\s*\(\s*(PPSA\d{5})", re.M)

for it in issues:
    text = it["title"] + "\n" + (it.get("body") or "")
    ids = set(TITLE_ID.findall(text))
    if not ids:
        continue
    fns = set(SCE_FN.findall(text)) | set(POSIX_FN.findall(text))
    for name, tid in NAME_HINT.findall(text):
        if tid in by_title or tid in ids:
            by_title[tid]["name"] = name.strip()
    for tid in ids:
        by_title[tid]["issues"].add(it["number"])
        by_title[tid]["fns"] |= fns
        for f in fns:
            fn_titles[f].add(tid)

print(f"titles mentioned across issues : {len(by_title)}")
print(f"  of which public homebrew     : {sum(1 for t in by_title if HOMEBREW.match(t))}")
print(f"distinct functions named        : {len(fn_titles)}")

print("\n=== functions wanted by the MOST distinct titles (demand ranking) ===")
for fn, tids in sorted(fn_titles.items(), key=lambda kv: (-len(kv[1]), kv[0]))[:22]:
    if len(tids) < 2:
        continue
    print(f"  {len(tids):2d} titles  {fn:42s} {', '.join(sorted(tids)[:5])}")

print("\n=== public homebrew we could actually run ourselves ===")
for tid, info in sorted(by_title.items()):
    if HOMEBREW.match(tid):
        label = info["name"] or "?"
        print(f"  {tid}  {label[:34]:34s} issues={sorted(info['issues'])[:6]}")
