import glob, json, os, sys, urllib.parse, uuid
import deploy


def main():
    host, src = sys.argv[1], sys.argv[2]
    call = deploy.portal(host)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(deploy.NAME + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    try:
        call("POST", "/api/filesystem/apps/folder?knownfolderid=LocalAppData&packagefullname=%s&path=%s&newfoldername=dxbc" % (full, urllib.parse.quote("\\LocalState")), b"")
    except Exception as error:
        print("folder:", error)
    files = sorted(glob.glob(os.path.join(src, "*.dxbc")) + glob.glob(os.path.join(src, "*.gfx")))
    for i in range(0, len(files), 20):
        boundary = uuid.uuid4().hex
        body = b""
        for f in files[i:i + 20]:
            name = os.path.basename(f)
            body += ("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n" % (boundary, name, name)).encode()
            body += open(f, "rb").read() + b"\r\n"
        body += ("--%s--\r\n" % boundary).encode()
        call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote("\\LocalState\\dxbc")), body, {"Content-Type": "multipart/form-data; boundary=" + boundary})
    listing = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote("\\LocalState\\dxbc"))))
    print("sent", len(files), "on xbox", len(listing.get("Items", [])))


if __name__ == "__main__":
    main()
