"""Take upstream's side of a hunk, minus function definitions that already exist elsewhere.

The common shape in this sync: upstream's block adds several exports, some of which we
already implement further down the same file. Taking their side duplicates those; taking
ours drops the genuinely new ones. The answer is their side with the duplicates removed.

    python theirs-minus.py FILE name1 name2 ...
"""
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
drop = set(sys.argv[2:])
HUNK = re.compile(r"<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
body = path.read_text(encoding="utf-8", errors="replace")


def strip_defs(text):
    """Remove whole `... APS5_VABI name(...) { ... }` blocks for the named functions."""
    for name in drop:
        pattern = re.compile(
            r"(?:^[^\n]*\bAPS5_VABI\s+" + re.escape(name) + r"\s*\([^\n]*\{\n.*?^\}\n\n?)",
            re.S | re.M)
        text, n = pattern.subn("", text)
        print(f"    dropped {name}: {n} block(s)")
    return text


def pick(match):
    return strip_defs(match.group(2))


merged = HUNK.sub(pick, body)
path.write_text(merged, encoding="utf-8")
print(f"  {path.name}: {merged.count('<<<<<<<')} marker(s) left")
for name in drop:
    print(f"    {name}: {len(re.findall(r'APS5_VABI\s+' + re.escape(name) + r'\s*\(', merged))} definition(s) now")
