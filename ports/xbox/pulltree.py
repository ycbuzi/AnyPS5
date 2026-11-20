import json, os, sys, time, urllib.parse
import deploy


def package_call(host, name):
    call = deploy.portal(host)
    pkg = next((p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_")), None)
    if not pkg:
        raise SystemExit("package %s not installed" % name)
    return call, urllib.parse.quote(pkg["PackageFullName"])


def pull(call, full, remote, local, skip_dirs=()):
    q = urllib.parse.quote
    items = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, q(remote)))).get("Items", [])
    os.makedirs(local, exist_ok=True)
    got = 0
    total = 0
    for item in items:
        name = item.get("Name", "")
        if item.get("Type", 0) & 16:
            if name in skip_dirs:
                continue
            n, b = pull(call, full, remote + "\\" + name, os.path.join(local, name), skip_dirs)
            got += n
            total += b
            continue
        target = os.path.join(local, name)
        size = item.get("FileSize", -1)
        if os.path.exists(target) and os.path.getsize(target) == size:
            continue
        for attempt in range(3):
            try:
                data = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=%s&path=%s" % (full, q(name), q(remote)))
                break
            except Exception as error:
                print("retry", name, error, flush=True)
                time.sleep(5)
        else:
            raise SystemExit("could not read " + remote + "\\" + name)
        with open(target, "wb") as f:
            f.write(data)
        got += 1
        total += len(data)
    return got, total


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    host, remote, local = args[0], args[1], args[2]
    name = next((f.split("=", 1)[1] for f in flags if f.startswith("--package=")), deploy.NAME)
    skip = tuple(next((f.split("=", 1)[1] for f in flags if f.startswith("--skip=")), "").split(",")) if any(f.startswith("--skip=") for f in flags) else ()
    call, full = package_call(host, name)
    start = time.time()
    root = "\\LocalState" + ("\\" + remote.replace("/", "\\").strip("\\") if remote.strip("\\/") else "")
    got, total = pull(call, full, root, local, skip)
    print("pulled %d files, %.1f MB in %.0f s" % (got, total / 1048576, time.time() - start), flush=True)


if __name__ == "__main__":
    main()
