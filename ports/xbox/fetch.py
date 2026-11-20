import json
import sys
import time
import urllib.error
import urllib.parse

import deploy


def main():
    call = deploy.portal(sys.argv[1])
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(deploy.NAME + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    folder = urllib.parse.quote("\LocalState")
    for _ in range(40):
        try:
            report = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=probe.json&path=%s" % (full, folder))
            open("out/probe-%s.json" % time.strftime("%Y%m%d-%H%M%S"), "wb").write(report)
            print(report.decode())
            return 0
        except urllib.error.HTTPError:
            time.sleep(3)
    print("no probe.json")
    return 1


if __name__ == "__main__":
    sys.exit(main())
