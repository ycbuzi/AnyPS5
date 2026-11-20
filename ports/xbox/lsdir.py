import json, sys, urllib.parse
import deploy


def main():
    call = deploy.portal(sys.argv[1])
    name = next((a.split("=", 1)[1] for a in sys.argv[2:] if a.startswith("--package=")), deploy.NAME)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    for rel in [a for a in sys.argv[2:] if not a.startswith("--package=")]:
        path = "\\LocalState" + ("\\" + rel.replace("/", "\\").strip("\\") if rel.strip("\\/") else "")
        try:
            items = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote(path))))["Items"]
            print(rel or ".", len(items), sorted(i["Name"] + ("/" if i.get("Type", 0) & 16 else "") for i in items)[:20])
        except Exception as error:
            print(rel or ".", "error", error)


if __name__ == "__main__":
    main()
