import datetime, json, os, shutil, sys, urllib.parse
import pulltree

BACKUPS = r"C:\AnyPS5 Games\_backups\xbox"
PACKAGE = "AnyPS5.Console"
SAVE_DIRS = ("_sd", "_trophy")
KEEP = 14


def folders(call, full, path):
    items = json.loads(call("GET", "/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=%s&path=%s" % (full, urllib.parse.quote(path)))).get("Items", [])
    return [i["Name"] for i in items if i.get("Type", 0) & 16]


def backup(host, reason):
    call, full = pulltree.package_call(host, PACKAGE)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    target = os.path.join(BACKUPS, stamp + "-" + reason)
    games = folders(call, full, "\\LocalState\\games") if "games" in folders(call, full, "\\LocalState") else []
    files = 0
    for game in games:
        have = folders(call, full, "\\LocalState\\games\\" + game)
        for sub in SAVE_DIRS:
            if sub in have:
                got, _ = pulltree.pull(call, full, "\\LocalState\\games\\%s\\%s" % (game, sub), os.path.join(target, game, sub))
                files += got
    if files == 0:
        if os.path.isdir(target):
            shutil.rmtree(target)
        print("no saves on the Xbox yet (%d games checked)" % len(games))
    else:
        print("backed up %d save files from %d games to %s" % (files, len(games), target))
    old = sorted(d for d in os.listdir(BACKUPS) if os.path.isdir(os.path.join(BACKUPS, d))) if os.path.isdir(BACKUPS) else []
    for d in old[:-KEEP] if len(old) > KEEP else []:
        shutil.rmtree(os.path.join(BACKUPS, d))
        print("removed old backup", d)
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    host = args[0] if args else open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", ".host")).read().strip()
    reason = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--reason=")), "manual")
    return backup(host, reason)


if __name__ == "__main__":
    sys.exit(main())
