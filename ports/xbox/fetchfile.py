import json, sys, time, urllib.error, urllib.parse
import deploy


def main():
    package = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--package=")), deploy.NAME)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    call = deploy.portal(args[0])
    name = args[1]
    wait = int(args[2]) if len(args) > 2 else 0
    time.sleep(wait)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(package + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    sub, _, leaf = name.replace("/", "\\").rpartition("\\")
    folder = urllib.parse.quote("\\LocalState" + ("\\" + sub if sub else ""))
    data = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=%s&path=%s" % (full, urllib.parse.quote(leaf), folder))
    open("out/" + leaf, "wb").write(data)
    print(data.decode(errors="replace")[:3000])


if __name__ == "__main__":
    main()
