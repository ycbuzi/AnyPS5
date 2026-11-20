import bisect
import re
import sys


def load(map_path):
    syms, base = [], 0x180000000
    for line in open(map_path, errors="replace"):
        found = re.search(r"Preferred load address is ([0-9a-f]+)", line)
        if found:
            base = int(found.group(1), 16)
        m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s", line)
        if m:
            syms.append((int(m.group(2), 16) - base, m.group(1)))
    syms.sort()
    return syms


def main():
    sample, map_path = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    syms = load(map_path)
    keys = [s[0] for s in syms]
    totals = {}
    for line in open(sample, errors="replace"):
        parts = line.split()
        if len(parts) != 4 or parts[0] != "vulkan-1.dll":
            continue
        i = bisect.bisect_right(keys, int(parts[1], 16)) - 1
        name = syms[i][1] if i >= 0 else "?"
        totals[name] = max(totals.get(name, 0), int(parts[3]))
    modules = {}
    for line in open(sample, errors="replace"):
        parts = line.split()
        if len(parts) == 4 and not line.startswith("#"):
            modules[parts[0]] = modules.get(parts[0], 0) + int(parts[2])
    print("self time by module:", ", ".join("%s %d" % kv for kv in sorted(modules.items(), key=lambda kv: -kv[1])[:6]))
    for name, count in sorted(totals.items(), key=lambda kv: -kv[1])[:top]:
        print(count, name[:110])


if __name__ == "__main__":
    main()
