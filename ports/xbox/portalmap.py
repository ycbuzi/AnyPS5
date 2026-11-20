import json
import re
import sys
import urllib.error
from pathlib import Path

from deploy import portal
from survey_summary import scrub

HERE = Path(__file__).resolve().parent

ENDPOINTS = [
    "/api/os/info", "/api/os/devicefamily", "/api/os/machinename",
    "/api/app/packagemanager/packages", "/api/resourcemanager/processes", "/api/resourcemanager/systemperf",
    "/api/networking/ipconfig", "/api/filesystem/apps/knownfolders", "/api/etw/providers", "/api/etw/customproviders",
    "/api/debug/dump/usermode/dumps", "/api/debug/dump/kernel/crashcontrol", "/api/power/state", "/api/power/battery",
    "/api/wpr/status", "/api/diagnostics/hardware",
    "/ext/xboxsettings", "/ext/fiddler", "/ext/xboxlive/sandbox", "/ext/app/deployinfo", "/ext/remoteinput",
    "/ext/media/status", "/ext/tracing", "/ext/devhome",
]

SKIP_SUBSTRINGS = ("/ext/user", "/ext/smb", "screenshot", "networkcredential")


def keys(body):
    try:
        data = json.loads(body)
    except ValueError:
        return "non-json"
    if isinstance(data, dict):
        return {k: (len(v) if isinstance(v, (list, dict)) else type(v).__name__) for k, v in data.items()}
    if isinstance(data, list):
        return "list[%d]" % len(data)
    return type(data).__name__


def main():
    host = (HERE / "out" / ".host").read_text().strip()
    call = portal(host)
    report = {}
    detail = {}
    for path in ENDPOINTS:
        if any(s in path for s in SKIP_SUBSTRINGS):
            continue
        try:
            body = call("GET", path)
            report[path] = {"status": 200, "bytes": len(body), "shape": keys(body)}
            detail[path] = body
        except urllib.error.HTTPError as e:
            report[path] = {"status": e.code}
        except Exception as e:
            report[path] = {"error": type(e).__name__}
    procs = detail.get("/api/resourcemanager/processes")
    if procs:
        names = sorted({p.get("ImageName", "") for p in json.loads(procs).get("Processes", [])})
        report["_process_names"] = names
    folders = detail.get("/api/filesystem/apps/knownfolders")
    if folders:
        report["_known_folders"] = json.loads(folders).get("KnownFolders")
    settings = detail.get("/ext/xboxsettings")
    if settings:
        report["_setting_names"] = [s.get("Name") for s in json.loads(settings).get("Settings", [])]
    packages = detail.get("/api/app/packagemanager/packages")
    if packages:
        report["_package_count"] = len(json.loads(packages).get("InstalledPackages", []))
    (HERE / "out" / "portalmap.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    raw = HERE / "out" / "portal"
    shared = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    raw.mkdir(exist_ok=True)
    if shared:
        shared.mkdir(parents=True, exist_ok=True)
    names = set()
    try:
        info = json.loads(detail.get("/api/os/info", b"{}"))
        names.add(info.get("ComputerName", ""))
    except ValueError:
        pass
    for path, body in detail.items():
        leaf = path.strip("/").replace("/", "_") + ".json"
        (raw / leaf).write_bytes(body)
        if shared:
            text = body.decode("utf-8", "replace")
            try:
                text = json.dumps(json.loads(text), indent=1)
            except ValueError:
                pass
            for n in names:
                if n:
                    text = text.replace(n, "<console-name>")
            text = re.sub(r"(?i)\b[0-9a-f]{2}([-:])(?:[0-9a-f]{2}\1){4}[0-9a-f]{2}\b", "<mac>", text)
            text = re.sub(r"(?i)\b[0-9a-f]{1,4}(?::[0-9a-f]{0,4}){2,7}\b", "<ipv6>", text)
            text = re.sub(r"[A-Za-z0-9.-]+_[0-9.]+_[a-z0-9]+__[a-z0-9]{13}", "<package-full-name>", text)
            (shared / leaf).write_text(scrub(text), encoding="utf-8")
    print("saved %d endpoint bodies" % len(detail))


if __name__ == "__main__":
    main()
