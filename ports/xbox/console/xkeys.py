import os
import sys
import time
import urllib.parse
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xdeploy


def main():
    host, keys = sys.argv[1], " ".join(sys.argv[2:])
    call = xdeploy.probe.portal(host)
    pkg = xdeploy.package(call)
    before = len(xdeploy.log(call, pkg).splitlines())
    boundary = uuid.uuid4().hex
    body = ("--%s\r\nContent-Disposition: form-data; name=\"keys.txt\"; filename=\"keys.txt\"\r\nContent-Type: text/plain\r\n\r\n%s\r\n--%s--\r\n" % (boundary, keys, boundary)).encode()
    call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s"
         % (urllib.parse.quote(pkg["PackageFullName"]), urllib.parse.quote(chr(92)+"LocalState")), body,
         {"Content-Type": "multipart/form-data; boundary=" + boundary})
    for _ in range(40):
        time.sleep(2)
        lines = xdeploy.log(call, pkg).splitlines()
        if any("keys done" in line for line in lines[before:]):
            break
    print("\n".join(lines[before:]))


if __name__ == "__main__":
    main()
