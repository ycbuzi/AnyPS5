import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "console"))
OUT = r"C:\AnyPS5-work\xbox-games"
READY = os.path.join(HERE, "console", "xbox-ready.json")
PACKAGE = "AnyPS5.Console"


def host():
    return open(os.path.join(HERE, "out", ".host")).read().strip()


def run(args):
    h = host()
    r = subprocess.run([sys.executable] + args, cwd=HERE, capture_output=True, text=True, encoding="utf-8", errors="replace")
    text = (r.stdout + r.stderr).replace(h, "<xbox>")
    if r.returncode:
        raise SystemExit("failed: %s\n%s" % (" ".join(args[:2]), text[-1500:]))
    return text


def add(folder, status, convert):
    if convert:
        import xbox_convert
        xbox_convert.convert(folder)
    local = os.path.join(OUT, os.path.basename(folder.rstrip("\\/")))
    name = os.path.basename(local)
    cfg = json.load(open(os.path.join(local, "game.json"), encoding="utf-8-sig"))
    start = time.time()
    out = run(["pushtree.py", host(), local, "games\\" + name, "--package=" + PACKAGE])
    print(out.strip().splitlines()[-1] if out.strip() else "pushed")
    if os.path.isdir(os.path.join(local, "shader_cache")):
        run(["pushtree.py", host(), os.path.join(local, "shader_cache"), "games\\%s\\shader_cache" % name, "--package=" + PACKAGE])
    ready = json.load(open(READY, encoding="utf-8")) if os.path.exists(READY) else {}
    ready[cfg["titleId"]] = {"status": status, "folder": name}
    with open(READY, "w", encoding="utf-8") as f:
        json.dump(ready, f, indent=1)
    print(run(["console/xlibrary.py", host(), "--no-art"]).strip().splitlines()[-1])
    print("%s is on the Xbox (%.0f s)" % (name, time.time() - start))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        raise SystemExit("usage: xbox_add_game.py <game folder> [--convert] [--status=Plays|Starts|Testing]")
    status = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--status=")), "Testing")
    add(args[0], status, "--convert" in sys.argv)
