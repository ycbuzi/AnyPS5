import json
import os
import sys
import urllib.parse
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xdeploy

SRC = r"C:\AnyPS5 Games\_tools\console"
READY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xbox-ready.json")


def pc_library():
    with open(os.path.join(SRC, "library.js"), encoding="utf-8") as f:
        text = f.read()
    return json.loads(text[text.index("["):text.rindex("]") + 1])


def ready_titles():
    if not os.path.exists(READY):
        return {}
    with open(READY, encoding="utf-8") as f:
        return json.load(f)


def upload(call, pkg, name, data):
    boundary = uuid.uuid4().hex
    body = (("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n"
             % (boundary, name, name)).encode() + data + ("\r\n--%s--\r\n" % boundary).encode())
    call("POST", "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=%s&path=%s"
         % (urllib.parse.quote(pkg["PackageFullName"]), urllib.parse.quote(chr(92) + "LocalState")), body,
         {"Content-Type": "multipart/form-data; boundary=" + boundary})


def main():
    host = sys.argv[1]
    with_art = "--no-art" not in sys.argv
    call = xdeploy.probe.portal(host)
    pkg = xdeploy.package(call)
    if not pkg:
        print("console app not installed")
        return 1
    ready = ready_titles()
    games, sent = [], 0
    for g in pc_library():
        entry = {"id": g["id"], "title": g["title"]}
        info = ready.get(g["id"])
        entry["status"] = info["status"] if info else "Not converted for Xbox"
        entry["folder"] = info.get("folder", "") if info else ""
        for kind in ("icon", "bg", "shot"):
            rel = g.get(kind)
            path = os.path.join(SRC, rel.replace("/", os.sep)) if rel else None
            if not path or not os.path.exists(path):
                continue
            name = "art-%s-%s%s" % (g["id"], kind, os.path.splitext(path)[1])
            if with_art:
                with open(path, "rb") as f:
                    upload(call, pkg, name, f.read())
                sent += 1
            entry[kind] = "ms-appdata:///local/" + name
        games.append(entry)
    upload(call, pkg, "library.json", json.dumps(games).encode("utf-8"))
    print("library.json: %d games (%d ready for Xbox), %d art files sent" % (len(games), sum(1 for g in games if g["id"] in ready), sent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
