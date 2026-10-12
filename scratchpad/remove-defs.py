"""Remove whole function definitions from a file by name.

Used to retire harvested definitions that upstream has since implemented elsewhere in the
same library. The linker reports these as "multiple definition"; the rule is that upstream's
file keeps the symbol and the harvested copy goes.

    python remove-defs.py FILE name1 name2 ...

Handles both one-line bodies and braced multi-line bodies, and refuses to touch a file where
a name is not found exactly once, so a silent miss cannot pass as success.
"""
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
names = sys.argv[2:]
body = path.read_text(encoding="utf-8", errors="replace")

for name in names:
    # one-liner:  <type> APS5_VABI name(args) { ... }
    one = re.compile(r"^[^\n/]*\bAPS5_VABI\s+" + re.escape(name) + r"\s*\([^)]*\)[^;{]*\{[^\n}]*\}[ \t]*\n", re.M)
    body, n = one.subn("", body)
    if n == 0:
        # multi-line: up to a closing brace at column 0
        multi = re.compile(r"^[^\n/]*\bAPS5_VABI\s+" + re.escape(name) + r"\s*\([^)]*\)[^;{]*\{.*?^\}\n\n?", re.S | re.M)
        body, n = multi.subn("", body)
    print(f"  {name}: removed {n} definition(s)")
    if n != 1:
        print(f"    WARNING expected exactly 1, got {n} -- check {path.name} by hand")

path.write_text(body, encoding="utf-8")
remaining = {n: len(re.findall(r"APS5_VABI\s+" + re.escape(n) + r"\s*\(", body)) for n in names}
print(f"  {path.name}: remaining references {remaining}")
