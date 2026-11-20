import base64
import os
import subprocess
import json
import sys
import time
import urllib.error
import urllib.parse

import deploy


def main():
    call = deploy.portal(sys.argv[1])
    packages = json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]
    pkg = next(p for p in packages if p["PackageFullName"].startswith(deploy.NAME + "_"))
    full, praid = pkg["PackageFullName"], pkg.get("PackageRelativeId", deploy.NAME + "!Probe")
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    try:
        call("DELETE", "/api/taskmanager/app?package=%s" % b64(full))
        print("stop accepted")
    except urllib.error.HTTPError as error:
        print("stop", error.code)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    time.sleep(int(args[1]) if len(args) > 1 else 8)
    for put in [a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--put=")]:
        local, _, sub = put.partition(":")
        r = subprocess.run([sys.executable, "putfile.py", sys.argv[1], local] + ([sub] if sub else []), capture_output=True, text=True)
        print("put", os.path.basename(local), "ok" if r.returncode == 0 else "failed")
    try:
        call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(praid), b64(full)), b"")
        print("launch accepted")
    except urllib.error.HTTPError as error:
        print("launch", error.code)


if __name__ == "__main__":
    main()
