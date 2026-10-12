"""Cheap syntax sanity check for files resolved by union.

The union resolver is not statement-aware: when a conflict boundary falls inside a
multi-line call, splicing both sides together leaves an unterminated statement. That is
exactly how libc/CMakeLists.txt ended up with `set_tests_properties(` and no closing paren.
Counting delimiters catches it in a second instead of a 20-minute build.
"""
import pathlib
import re
import sys

STRINGS = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'')
LINE_COMMENT = re.compile(r"//[^\n]*|^\s*#[^\n]*", re.M)
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
CMAKE_COMMENT = re.compile(r"#[^\n]*")

for name in sys.argv[1:]:
    path = pathlib.Path(name)
    if not path.exists():
        print(f"  MISSING {name}")
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    if path.suffix in (".cmake",) or path.name == "CMakeLists.txt":
        body = CMAKE_COMMENT.sub("", STRINGS.sub('""', text))
        pairs = {"(": ")"}
    elif path.suffix == ".md":
        print(f"  skipped (markdown) {name}")
        continue
    else:
        body = BLOCK_COMMENT.sub("", text)
        body = LINE_COMMENT.sub("", body)
        body = STRINGS.sub('""', body)
        pairs = {"(": ")", "{": "}"}
    bad = []
    for opener, closer in pairs.items():
        delta = body.count(opener) - body.count(closer)
        if delta != 0:
            bad.append(f"{opener}{closer} off by {delta:+d}")
    print(f"  {'UNBALANCED' if bad else 'ok        '} {name}" + (f"   [{', '.join(bad)}]" if bad else ""))
