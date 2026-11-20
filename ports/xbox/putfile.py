import json, os, sys, urllib.parse, uuid
import deploy


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    name = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--package=")), deploy.NAME)
    host, local = args[0], args[1]
    sub = args[2] if len(args) > 2 else ""
    call = deploy.portal(host)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    path = "\\LocalState"
    for part in [p for p in sub.replace("/", "\\").split("\\") if p]:
        try:
            call("POST", "/api/filesystem/apps/folder?knownfolderid=LocalAppData&packagefullname=%s&path=%s&newfoldername=%s" % (full, urllib.parse.quote(path), urllib.parse.quote(part)), b"")
        except Exception:
            pass
        path += "\\" + part
    name = os.path.basename(local)
    boundary = uuid.uuid4().hex
    body = ("--%s\r\nContent-Disposition: form-data; name=\"f\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n" % (boundary, name)).encode() + open(local, "rb").read() + ("\r\n--%s--\r\n" % boundary).encode()
    call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote(path)), body, {"Content-Type": "multipart/form-data; boundary=" + boundary})
    print("sent", name, "to", path)


if __name__ == "__main__":
    main()
