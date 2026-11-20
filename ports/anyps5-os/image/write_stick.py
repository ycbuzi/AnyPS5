import hashlib, json, os, subprocess, sys

iso, disk, want_name, want_gb = sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4])

def ps(cmd):
    return subprocess.run(["powershell", "-NoProfile", "-Command", cmd], capture_output=True, text=True)

info = json.loads(ps(f"Get-Disk -Number {disk} | Select-Object FriendlyName,BusType,Size | ConvertTo-Json").stdout)
gb = info["Size"] / 2**30
if want_name not in info["FriendlyName"] or str(info["BusType"]) not in ("USB", "7") or abs(gb - want_gb) > 0.5:
    sys.exit(f"refusing: disk {disk} is {info}")
print(f"disk {disk}: {info['FriendlyName']} {gb:.1f} GB")

r = ps(f"Clear-Disk -Number {disk} -RemoveData -RemoveOEM -Confirm:$false")
if r.returncode:
    sys.exit("clear failed: " + r.stderr)
print("cleared")

size = os.path.getsize(iso)
chunk = 4 << 20
done = 0
src_hash = hashlib.sha256()
with open(iso, "rb") as f, open(rf"\\.\PhysicalDrive{disk}", "r+b", buffering=0) as d:
    while True:
        b = f.read(chunk)
        if not b:
            break
        src_hash.update(b)
        pad = (-len(b)) % 512
        d.write(b + b"\0" * pad)
        done += len(b)
        if done % (256 << 20) < chunk:
            print(f"wrote {done >> 20} / {size >> 20} MiB", flush=True)

back = hashlib.sha256()
left = size
with open(rf"\\.\PhysicalDrive{disk}", "rb", buffering=0) as d:
    while left > 0:
        n = min(chunk, left)
        b = d.read((n + 511) // 512 * 512)[:n]
        back.update(b)
        left -= n
print("verify", "OK" if back.hexdigest() == src_hash.hexdigest() else "MISMATCH", back.hexdigest())
