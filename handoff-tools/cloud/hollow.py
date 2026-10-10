# Classify each PR's newly-implemented functions as hollow (only returns a constant/error, throws,
# or ignores its args) or real. usage: hollow.py harvest-new.jsonl harvest.tsv
import sys, json, re
rows = {l.split("\t")[0]: l.rstrip("\n").split("\t") for l in open(sys.argv[2]) if not l.startswith("pr\t")}
def statements(body):
    b = body.strip()
    if b.startswith("{"): b = b[1:]
    if b.endswith("}"): b = b[:-1]
    b = re.sub(r"//[^\n]*", "", b)
    b = re.sub(r"/\*.*?\*/", "", b, flags=re.S)
    return [s.strip() for s in re.split(r";", b) if s.strip()]
def hollow(body):
    st = statements(body)
    if not st: return True
    simple = 0
    for s in st:
        s2 = re.sub(r"\s+", " ", s)
        if re.fullmatch(r"\(void\)\s*\w+", s2): simple += 1; continue
        if re.fullmatch(r"return\s+(?:-?\w+|\(\w+\)\s*-?\w+|static_cast<\w+>\(-?\w+\)|0x[0-9a-fA-F]+)", s2): simple += 1; continue
        if re.fullmatch(r"(?:if\s*\([^)]*\)\s*)?return\s+[A-Z_0-9x]+", s2): simple += 1; continue
        if s2.startswith("throw ") or re.fullmatch(r"if\s*\(.*\)\s*throw .*", s2): simple += 1; continue
        if re.fullmatch(r"if\s*\([^{}]*\)\s*return\s+[\w:]+", s2): simple += 1; continue
    return simple == len(st)
for line in open(sys.argv[1]):
    d = json.loads(line)
    fns = [(lib, n, b) for lib, m in d["new_done"].items() for n, b in m.items()]
    h = [f for f in fns if hollow(f[2])]
    real = [f for f in fns if not hollow(f[2])]
    r = rows.get(d["pr"], [])
    print(f'{d["pr"]}\t{d["result"]}\td_done={d["d_done"]}\td_total={d["d_total"]}\treal={len(real)}\thollow={len(h)}\tlibs={",".join(sorted(d["new_done"]))}')
