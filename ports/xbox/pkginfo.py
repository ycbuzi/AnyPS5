import json, sys
import deploy

call = deploy.portal(sys.argv[1])
for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]:
    if p["PackageFullName"].startswith(deploy.NAME + "_"):
        print(p["PackageFullName"], p.get("Version"))
