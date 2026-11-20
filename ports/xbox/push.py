import base64
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import uuid

import deploy


def main():
    host, payload = sys.argv[1], sys.argv[2]
    call = deploy.portal(host)
    call("GET", "/api/os/info")
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(deploy.NAME + "_"))
    full = pkg["PackageFullName"]
    quote = urllib.parse.quote
    folder = quote("\\LocalState")
    for name in ("loader.json", "payload.json"):
        try:
            call("DELETE", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=%s&path=%s" % (quote(full), name, folder))
        except urllib.error.HTTPError:
            pass
    b64 = lambda s: quote(base64.b64encode(s.encode()).decode(), safe="")
    try:
        call("DELETE", "/api/taskmanager/app?package=%s" % b64(full))
        time.sleep(3)
    except urllib.error.HTTPError as error:
        print("stop", error.code)
    boundary = uuid.uuid4().hex
    with open(payload, "rb") as f:
        body = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"payload.dll\"\r\nContent-Type: application/octet-stream\r\n\r\n" % boundary).encode()
                + f.read() + ("\r\n--%s--\r\n" % boundary).encode())
    call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (quote(full), folder), body,
         {"Content-Type": "multipart/form-data; boundary=" + boundary})
    b64 = lambda s: quote(base64.b64encode(s.encode()).decode(), safe="")
    try:
        call("POST", "/api/taskmanager/app?appid=%s&package=%s" % (b64(pkg["PackageRelativeId"]), b64(full)), b"")
    except urllib.error.HTTPError as error:
        print("launch", error.code, error.read()[:200])
    results = {}
    for _ in range(40):
        time.sleep(3)
        for name in ("loader.json", "payload.json"):
            if name in results:
                continue
            try:
                results[name] = call("GET", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&filename=%s&path=%s" % (quote(full), name, folder)).decode()
            except urllib.error.HTTPError:
                pass
        if "loader.json" in results:
            break
    stamp = time.strftime("%Y%m%d-%H%M%S")
    with open(os.path.join(deploy.HERE, "out", "push-%s.json" % stamp), "w", encoding="utf-8") as f:
        json.dump(results, f)
    print(json.dumps(results))
    return 0 if "loader.json" in results else 1


if __name__ == "__main__":
    sys.exit(main())
