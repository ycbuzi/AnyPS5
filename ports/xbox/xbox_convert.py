import json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
RELINKER = r"C:\AnyPS5-work\build-sse-win\core\relinker\relinker.exe"
LIBSET = os.path.join(HERE, "libset")
XVK = os.path.join(HERE, "xvk", "bin", "xvk.dll")
OUT = r"C:\AnyPS5-work\xbox-games"
GAMES = r"D:\AnyPS5 Games"
COPY_DIRS = ("app0", "data", "download0", "mods")
COPY_FILES = ("game.json", "info.txt")
SKIP_DIRS = {"_sd", "_trophy", "shader_cache", "decrypted", "fakelib", ".build"}


def copy_tree(src, dst, force=False):
    count = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(".old-")]
        rel = os.path.relpath(root, src)
        target = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(target, exist_ok=True)
        for name in files:
            s, d = os.path.join(root, name), os.path.join(target, name)
            if not force and os.path.exists(d) and os.path.getsize(d) == os.path.getsize(s) and os.path.getmtime(d) >= os.path.getmtime(s):
                continue
            shutil.copy2(s, d)
            count += 1
    return count


def relink(game_dir, cfg, stage, exe_name):
    dump = cfg["dump"]
    eboot = cfg.get("eboot", "eboot.bin")
    if not os.path.exists(os.path.join(dump, eboot)):
        own = os.path.join(game_dir, "app0")
        if os.path.exists(os.path.join(own, eboot)):
            dump = own
        else:
            raise SystemExit("eboot not found: " + os.path.join(dump, eboot))
    patches = sorted(p for p in os.listdir(os.path.join(game_dir, "mods", "patches")) if p.endswith(".toml") and not p.startswith("_")) if os.path.isdir(os.path.join(game_dir, "mods", "patches")) else []
    args = ["--no-patches"]
    if patches:
        merged = os.path.join(stage, "mods.patches.toml")
        with open(merged, "w", encoding="utf-8") as f:
            for p in patches:
                f.write("# from mods\\patches\\%s\n" % p)
                f.write(open(os.path.join(game_dir, "mods", "patches", p), encoding="utf-8").read() + "\n")
        args = ["--patches", merged]
    for m in cfg.get("moduleDirs") or []:
        if m and m.strip("/\\") not in ("prx", "sce_module", "sce_modules"):
            args += ["--module-dir", m]
    if cfg.get("skipSceModule"):
        args.append("--skip-sce-module")
    exe = os.path.join(stage, exe_name + ".exe")
    cmd = [RELINKER, "--windows", "--windows-gui", "--registry", "--to-sse"] + args + [eboot, exe]
    start = time.time()
    run = subprocess.run(cmd, cwd=dump, capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(os.path.join(stage, "relink.txt"), "w", encoding="utf-8") as f:
        f.write(" ".join(cmd[1:]) + "\n" + run.stdout + run.stderr)
    if run.returncode != 0 or not os.path.exists(exe):
        tail = (run.stdout + run.stderr).strip().splitlines()[-8:]
        raise SystemExit("relink failed (%d):\n%s" % (run.returncode, "\n".join(tail)))
    print("relinked %s.exe for SSE in %.0f s (%.1f MB)" % (exe_name, time.time() - start, os.path.getsize(exe) / 1048576), flush=True)
    return exe


def convert(title_dir):
    game_dir = os.path.realpath(title_dir if os.path.isabs(title_dir) else os.path.join(GAMES, title_dir))
    cfg = json.load(open(os.path.join(game_dir, "game.json"), encoding="utf-8-sig"))
    folder = os.path.basename(game_dir.rstrip("\\/"))
    exe_name = cfg.get("exeName") or cfg["title"]
    target = os.path.join(OUT, folder)
    stage = os.path.join(target, ".xbuild")
    os.makedirs(stage, exist_ok=True)
    for p in (RELINKER, LIBSET, XVK):
        if not os.path.exists(p):
            raise SystemExit("missing: " + p)
    exe = relink(game_dir, cfg, stage, exe_name)
    copied = 0
    for d in COPY_DIRS:
        if os.path.isdir(os.path.join(game_dir, d)):
            copied += copy_tree(os.path.join(game_dir, d), os.path.join(target, d))
    for f in COPY_FILES:
        if os.path.exists(os.path.join(game_dir, f)):
            shutil.copy2(os.path.join(game_dir, f), os.path.join(target, f))
    converted = copy_tree(os.path.join(stage, "app0"), os.path.join(target, "app0"), True) if os.path.isdir(os.path.join(stage, "app0")) else 0
    libs = copy_tree(LIBSET, os.path.join(target, "libs"))
    overrides = os.path.join(game_dir, "mods", "libs")
    if os.path.isdir(overrides) and os.listdir(overrides):
        print("note: mods\\libs overrides are PC builds and are not used on the Xbox:", ", ".join(os.listdir(overrides)))
    shutil.copy2(XVK, os.path.join(target, "vulkan-1.dll"))
    for old in os.listdir(target):
        if old.lower().endswith(".exe") and old != exe_name + ".exe":
            os.remove(os.path.join(target, old))
    shutil.move(exe, os.path.join(target, exe_name + ".exe"))
    os.makedirs(os.path.join(target, "imports"), exist_ok=True)
    for f in os.listdir(stage):
        if f.endswith(".registry.json") or f.endswith(".needed.json"):
            shutil.copy2(os.path.join(stage, f), os.path.join(target, "imports", f))
    size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(target) for f in fs)
    print("%s ready for the Xbox in %s: %d game files, %d converted modules and %d libraries copied, %.0f MB total" % (folder, target, copied, converted, libs, size / 1048576))
    return target


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: xbox_convert.py <game folder name or path>")
    convert(sys.argv[1])
