"""Classify each merge conflict so the mechanical ones can be cleared in bulk.

A conflict whose two sides differ only by a byte-order mark or whitespace is noise and
can be resolved to upstream safely. Everything else needs the test-ownership check:
is the side demanding a behaviour upstream's, or a harvested PR's?
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path("D:/AnyPS5")
files = subprocess.run(["git", "diff", "--name-only", "--diff-filter=U"],
                       cwd=ROOT, capture_output=True, text=True).stdout.split()

HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)(?:\|\|\|\|\|\|\|[^\n]*\n.*?)?=======\n(.*?)>>>>>>> [^\n]*\n", re.S)


def norm(text):
    return re.sub(r"\s+", "", text.replace("﻿", ""))


cosmetic, real = [], []
for name in files:
    path = ROOT / name
    try:
        body = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    hunks = HUNK.findall(body)
    if hunks and all(norm(a) == norm(b) for a, b in hunks):
        cosmetic.append((name, len(hunks)))
    else:
        differing = sum(1 for a, b in hunks if norm(a) != norm(b))
        real.append((name, len(hunks), differing))

print(f"=== COSMETIC ONLY (whitespace/BOM) -- safe to take upstream: {len(cosmetic)} ===")
for name, n in cosmetic:
    print(f"  {n:2d} hunks  {name}")
print(f"\n=== NEEDS JUDGEMENT: {len(real)} ===")
for name, n, d in sorted(real, key=lambda r: r[2]):
    print(f"  {d:2d} real of {n:2d} hunks  {name}")
