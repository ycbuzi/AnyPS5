import json, sys
import deploy


def main():
    call = deploy.portal(sys.argv[1])
    procs = json.loads(call("GET", "/api/resourcemanager/processes"))["Processes"]
    big = [p for p in procs if p.get("WorkingSetSize", 0) > 700 * 1048576 or p.get("TotalCommit", 0) > 1500 * 1048576]
    ours = [p for p in procs if "anyps5" in str(p.get("PackageFullName", "")).lower()]
    print("processes", len(procs), "large", len(big), "anyps5", len(ours))
    print("busy" if [p for p in big if p not in ours] else "idle")


if __name__ == "__main__":
    main()
