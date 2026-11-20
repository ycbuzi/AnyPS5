import base64
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import uuid

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import deploy as probe

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.join(HERE, "out", "AnyPS5Console.appx")
NAME = "AnyPS5.Console"


def package(call):
    packages = json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]
    return next((p for p in packages if p["PackageFullName"].startswith(NAME + "_")), None)


def install(call):
    boundary = uuid.uuid4().hex
    with open(PACKAGE, "rb") as f:
        body = (("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n"
                 % (boundary, os.path.basename(PACKAGE), os.path.basename(PACKAGE))).encode() + f.read() + ("\r\n--%s--\r\n" % boundary).encode())
    call("POST", "/api/app/packagemanager/package?package=" + urllib.parse.quote(os.path.basename(PACKAGE)), body,
         {"Content-Type": "multipart/form-data; boundary=" + boundary})
    for _ in range(120):
        time.sleep(2)
        state = call("GET", "/api/app/packagemanager/state")
        if state.strip():
            print("install state", state[:200])
            break


def launch(call, pkg):
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(pkg["PackageRelativeId"]), b64(pkg["PackageFullName"])), b"")


def log(call, pkg):
    try:
        return call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=console-log.txt&path=%s"
                    % (urllib.parse.quote(pkg["PackageFullName"]), urllib.parse.quote("\\LocalState"))).decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return "no log (%d)" % error.code


def main():
    host = sys.argv[1]
    call = probe.portal(host)
    call("GET", "/api/os/info")
    if "--log" not in sys.argv:
        import xsaves
        xsaves.backup(host, "before-deploy")
        install(call)
    pkg = package(call)
    if not pkg:
        print("not installed")
        return 1
    print("package", pkg["PackageFullName"])
    if "--log" not in sys.argv and "--no-launch" not in sys.argv:
        try:
            launch(call, pkg)
            print("launched")
        except urllib.error.HTTPError as error:
            print("launch", error.code, error.read()[:300])
        time.sleep(15)
    print(log(call, pkg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
