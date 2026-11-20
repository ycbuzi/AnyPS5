import re
import sys

BS = chr(92)
lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
mount_key = BS + "Mount" + BS
binaries = [l.split(mount_key, 1)[1] for l in lines if mount_key in l and re.search(r"\.(exe|dll)$", l, re.I)]
print("Mount binaries:", [b for b in binaries if BS not in b])
print("nested binaries:", len([b for b in binaries if BS in b]))
embedded = [l.rsplit(BS, 1)[1] for l in lines if "EmbeddedXvd" in l and l.lower().endswith(".dll")]
print("embedded dlls:", len(embedded), [e for e in embedded if re.search(r"_x\.dll$|kernelx|xgameruntime|xmem|xapu", e, re.I)])
print("top:", sorted(set(l.split(BS)[1] for l in lines if l.count(BS) >= 1)))
