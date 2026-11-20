import re
import sys

PATTERNS = [
    re.compile(r"[A-Za-z]:\\Users\\[^\s'\"]*"),
    re.compile(r"AnyPS5\.(Console|XboxProbe)_[^\s'\"\\/]*"),
    re.compile(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b"),
    re.compile(r"\b([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b"),
]


def scrub(line):
    for pattern in PATTERNS:
        line = pattern.sub("<x>", line)
    return line


if __name__ == "__main__":
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    for line in sys.stdin:
        out = scrub(line.rstrip("\n"))
        print(out[:limit] if limit else out)
