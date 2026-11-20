import base64
import http.cookiejar
import json
import os
import ssl
import sys
import time
import urllib.parse
import urllib.request
import uuid

SECRETS = r"C:\AnyPS5-work\secrets\xbox-device-portal.txt"
HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.environ.get("XPROBE_PACKAGE", os.path.join(HERE, "out", "AnyPS5Probe.appx"))
NAME = os.environ.get("XPROBE_NAME", "AnyPS5.XboxProbe")


def portal(host):
    with open(SECRETS, encoding="utf-8-sig") as f:
        lines = [x.strip() for x in f.read().splitlines() if x.strip()]
    user, password = lines[0], lines[1]
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    jar = http.cookiejar.CookieJar()
    auth = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    auth.add_password(None, "https://%s:11443/" % host, user, password)
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx), urllib.request.HTTPCookieProcessor(jar),
                                         urllib.request.HTTPBasicAuthHandler(auth), urllib.request.ProxyHandler({}))
    base = "https://%s:11443" % host

    def call(method, path, data=None, headers=None):
        token = next((c.value for c in jar if c.name == "CSRF-Token"), None)
        h = dict(headers or {})
        if token:
            h["X-CSRF-Token"] = token
        req = urllib.request.Request(base + path, data=data, headers=h, method=method)
        with opener.open(req, timeout=120) as r:
            return r.read()

    return call


def main():
    host = sys.argv[1]
    call = portal(host)
    json.loads(call("GET", "/api/os/info"))
    for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]:
        if p["PackageFullName"].startswith(NAME + "_") and "--update" not in sys.argv:
            call("DELETE", "/api/app/packagemanager/package?package=" + urllib.parse.quote(p["PackageFullName"]))
    boundary = uuid.uuid4().hex
    with open(PACKAGE, "rb") as f:
        body = (("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n"
                 % (boundary, os.path.basename(PACKAGE), os.path.basename(PACKAGE))).encode() + f.read() + ("\r\n--%s--\r\n" % boundary).encode())
    call("POST", "/api/app/packagemanager/package?package=" + urllib.parse.quote(os.path.basename(PACKAGE)), body,
         {"Content-Type": "multipart/form-data; boundary=" + boundary})
    for _ in range(60):
        state = call("GET", "/api/app/packagemanager/state")
        if state.strip():
            break
        time.sleep(2)
    packages = json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"]
    pkg = next(p for p in packages if p["PackageFullName"].startswith(NAME + "_"))
    full, praid = pkg["PackageFullName"], pkg.get("PackageRelativeId", NAME + "!Probe")
    b64 = lambda s: urllib.parse.quote(base64.b64encode(s.encode()).decode(), safe="")
    call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(praid), b64(full)), b"")
    report = None
    for _ in range(30):
        time.sleep(2)
        try:
            report = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=probe.json&path=%s"
                          % (urllib.parse.quote(full), urllib.parse.quote("\\LocalState")))
            break
        except urllib.error.HTTPError:
            continue
    if report is None:
        print("installed %s but no probe.json came back" % full)
        try:
            listing = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s"
                                      % (urllib.parse.quote(full), urllib.parse.quote("\\LocalState"))))
            print("LocalState:", [item.get("Name") for item in listing.get("Items", [])])
        except urllib.error.HTTPError as error:
            print("LocalState listing failed:", error.code)
        return 1
    for name in ("pso.json",):
        for _ in range(30):
            try:
                extra = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=%s&path=%s"
                             % (urllib.parse.quote(full), name, urllib.parse.quote("\\LocalState")))
                print(name, extra.decode("utf-8", "replace")[:600])
                break
            except urllib.error.HTTPError:
                time.sleep(2)
        else:
            print(name, "did not come back")
    out = os.path.join(HERE, "out", "probe-%s.json" % time.strftime("%Y%m%d-%H%M%S"))
    with open(out, "wb") as f:
        f.write(report)
    print(json.dumps(json.loads(report), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
