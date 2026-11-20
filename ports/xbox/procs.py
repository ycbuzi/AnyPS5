import json, sys
import deploy


def main():
    call = deploy.portal(sys.argv[1])
    procs = json.loads(call("GET", "/api/resourcemanager/processes"))["Processes"]
    for p in procs:
        name = p.get("ImageName", "")
        if "probe" in name.lower() or "anyps5" in str(p.get("PackageFullName", "")).lower():
            print(name, "cpu", round(p.get("CPUUsage", 0), 1), "ws MB", p.get("WorkingSetSize", 0) // 1048576, "threads", p.get("ThreadCount", "?"))


if __name__ == "__main__":
    main()
