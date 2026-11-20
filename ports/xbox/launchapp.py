import base64, json, sys, urllib.parse
import deploy


def launch(host, name, app):
    call = deploy.portal(host)
    pkgs = [p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_")]
    if not pkgs:
        raise SystemExit("package %s not installed" % name)
    pkg = max(pkgs, key=lambda p: [int(x) for x in p["PackageFullName"].split("_")[1].split(".")])
    full = pkg["PackageFullName"]
    praid = pkg.get("PackageRelativeId", "").split("!")[0] + "!" + app
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(praid), b64(full)), b"")
    print("launched %s!%s, version %s, %d installed copies" % (name, app, full.split("_")[1], len(pkgs)))


def stop(host, name):
    call = deploy.portal(host)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_"))
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    call("DELETE", "/api/taskmanager/app?package=%s" % b64(pkg["PackageFullName"]))
    print("stopped", name)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--stop" in sys.argv:
        stop(args[0], args[1])
    else:
        launch(args[0], args[1], args[2])
