import re
from survey_summary import scrub

print(scrub(open("out/diag.json").read()))
procs = [l.split(" ", 1)[1] for l in open("out/diag-procs.txt", encoding="utf-8").read().splitlines() if " " in l]
print("procs", len(procs), sorted(set(procs))[:80])
user = re.compile(r"U:\\Users\\[^\\]+", re.I)
pub = re.compile(r"_[0-9a-z]{13}\b")
for line in open("out/diag-pkgs.txt", encoding="utf-8").read().splitlines():
    name, _, path = line.partition("|")
    path = pub.sub("_<hash>", user.sub(r"U:\\Users\\<u>", scrub(path)))
    print(name[:40], "|", path[:110])
