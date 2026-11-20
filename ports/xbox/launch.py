import base64
import json
import sys
import urllib.error
import urllib.parse

import deploy


def main():
    call = deploy.portal(sys.argv[1])
    packages = json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]
    pkg = next(p for p in packages if p["PackageFullName"].startswith(deploy.NAME + "_"))
    full, praid = pkg["PackageFullName"], pkg.get("PackageRelativeId", deploy.NAME + "!Probe")
    print("package", full, "praid", praid)
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    try:
        call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(praid), b64(full)), b"")
        print("launch accepted")
    except urllib.error.HTTPError as error:
        print("launch", error.code, error.read()[:400])
    try:
        listing = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s"
                                  % (urllib.parse.quote(full), urllib.parse.quote("\LocalState"))))
        print("LocalState:", [item.get("Name") for item in listing.get("Items", [])])
    except urllib.error.HTTPError as error:
        print("LocalState listing", error.code)


if __name__ == "__main__":
    main()
