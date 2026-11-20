import json, os, sys, time, urllib.parse, uuid
import deploy

SKIP_EXT = {".log", ".bmp", ".png", ".md"}
KEEP_DIRS = {"_sd", "_trophy", "shader_cache"}
SKIP_NAMES = {"Rebuild.cmd", "ai-pad.txt", "anyps5-report.json", "anyps5-recipe.json", "xvk.log"}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    name = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--package=")), deploy.NAME)
    host, src, dest = args[0], args[1], args[2].replace("/", "\\")
    call = deploy.portal(host)
    pkg = next(p for p in json.loads(call("GET", "/api/app/packagemanager/packages"))["InstalledPackages"] if p["PackageFullName"].startswith(name + "_"))
    full = urllib.parse.quote(pkg["PackageFullName"])
    q = urllib.parse.quote

    def mkdir(parent, name):
        try:
            call("POST", "/api/filesystem/apps/folder?knownfolderid=LocalAppData&packagefullname=%s&path=%s&newfoldername=%s" % (full, q(parent), q(name)), b"")
        except Exception:
            pass

    def listing(path):
        try:
            items = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, q(path)))).get("Items", [])
            return {i.get("Name"): i.get("FileSize", -1) for i in items}
        except Exception:
            return {}

    root = "\\LocalState"
    for part in [p for p in dest.split("\\") if p]:
        mkdir(root, part)
        root += "\\" + part
    sent = skipped = total_bytes = 0
    start = time.time()
    for here, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in KEEP_DIRS]
        rel = os.path.relpath(here, src)
        remote = root if rel == "." else root + "\\" + rel.replace("/", "\\")
        for d in dirs:
            mkdir(remote, d)
        have = listing(remote)
        todo = []
        for name in files:
            if rel == "." and (name in SKIP_NAMES or os.path.splitext(name)[1].lower() in SKIP_EXT):
                continue
            path = os.path.join(here, name)
            if have.get(name) == os.path.getsize(path):
                skipped += 1
                continue
            todo.append(path)
        batch, size = [], 0
        for path in todo + [None]:
            if path is not None and (not batch or (len(batch) < 20 and size + os.path.getsize(path) < (48 << 20))):
                batch.append(path)
                size += os.path.getsize(path)
                continue
            if batch:
                boundary = uuid.uuid4().hex
                body = bytearray()
                for f in batch:
                    body += ("--%s\r\nContent-Disposition: form-data; name=\"f\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n" % (boundary, os.path.basename(f))).encode()
                    body += open(f, "rb").read() + b"\r\n"
                body += ("--%s--\r\n" % boundary).encode()
                for attempt in range(3):
                    try:
                        call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, q(remote)), bytes(body), {"Content-Type": "multipart/form-data; boundary=" + boundary})
                        break
                    except Exception as error:
                        print("retry", remote, error, flush=True)
                        time.sleep(5)
                sent += len(batch)
                total_bytes += size
                print("%s sent %d files %.1f MB %.0f s" % (remote, sent, total_bytes / 1048576, time.time() - start), flush=True)
            batch, size = ([path], os.path.getsize(path)) if path is not None else ([], 0)
    print("done: sent %d, already there %d, %.1f MB in %.0f s" % (sent, skipped, total_bytes / 1048576, time.time() - start), flush=True)


if __name__ == "__main__":
    main()
