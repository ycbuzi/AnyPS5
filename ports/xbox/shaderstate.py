import json, sys, urllib.parse
import deploy


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    package = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--package=")), deploy.NAME)
    host, sub = args[0], args[1]
    call = deploy.portal(host)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(package + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])

    def ls(path):
        q = "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote(path))
        return json.loads(call("GET", q)).get("Items", [])

    root = "\\LocalState\\" + sub.replace("/", "\\")
    done = pending = 0
    for d in ls(root):
        if not d.get("Type", 0) & 16:
            continue
        names = [f["Name"] for f in ls(root + "\\" + d["Name"])]
        if d["Name"] + ".dxbc" in names:
            done += 1
        else:
            pending += 1
            print("pending", d["Name"], " ".join(sorted(names)))
    print("done", done, "pending", pending)


if __name__ == "__main__":
    main()
