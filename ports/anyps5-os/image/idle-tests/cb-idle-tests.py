#!/usr/bin/env python3
"""Chromebook idle game tests (Blu 11-17: every device always working).

Runs every 5 minutes from anyps5-idle-tests.timer. While nobody uses the console (no controller button, stick or key
input for IDLE_MIN minutes, as anyps5-pad-watch records it, and no game running), it starts the next library game through the OS host, lets it run RUN_S
seconds and closes it. A game started by Blu is never touched; when a controller connects during a test, the game is
left running for Blu and the test ends there.
A game whose test hangs the GPU (i915 "GPU HANG") is skipped until the toolchain changes; when the GPU is wedged and
nobody has touched the console for 2 minutes, the device reboots, since nothing can render until it does.
The host records Plays / Stops early itself; this script adds one line per run to LOG.
Each test run is also a perf test (Blu 11-17): the game gets APS5_TRACE_SUMMARY_EVERY with APS5_TRACE_DIR set to
/home/anyps5/perf-runs/<run> (the game runs as anyps5), devsample.py records CPU, RAM, heat and clocks, and perf_report.py writes FPS, the bottleneck, device
limits, log problems and changes against the last run to perf-history.jsonl and perf-report.md.
"""
import glob
import gzip
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

HOST = "http://127.0.0.1:8790"
DIR = "/root/idle-tests"
STATE = DIR + "/state.json"
LOG = DIR + "/runs.log"
IDLE_MIN = 10
RUN_S = 600
ENV_FILE = "game-env.txt"
DEVICE = "chromebook"
RUNS = "/home/anyps5/perf-runs"
ACTIVE = "/run/anyps5-input-active"
VERSION = "/opt/anyps5/VERSION"
DIAG = DIR + "/diagnose.json"
ERRORS = DIR + "/gpu-errors"


def msg(body):
    req = urllib.request.Request(HOST + "/msg", json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        data = r.read()
    out = json.loads(data) if data else {}
    if not isinstance(out, dict) or out.get("ok") is False:
        raise RuntimeError("host refused %s: %s" % (list(body), str(out)[:200]))
    return out.get("reply") if isinstance(out.get("reply"), dict) else out


def library():
    with urllib.request.urlopen(HOST + "/library.js", timeout=20) as r:
        text = r.read().decode("utf-8")
    return json.loads(text[text.index("=") + 1:].strip().rstrip(";"))


def load():
    try:
        with open(STATE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save(st):
    tmp = STATE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(st, f, indent=1)
    os.replace(tmp, STATE)


def log(line):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + line + "\n")


def start_perf(folder, exe, st, extra=None):
    run = "%s/%s-%s" % (RUNS, time.strftime("%Y%m%d-%H%M%S"), re.sub(r"[^A-Za-z0-9]+", "-", os.path.basename(folder))[:40])
    os.makedirs(run, exist_ok=True)
    subprocess.run(["chown", "anyps5:anyps5", RUNS, run], check=False)
    path = os.path.join(folder, ENV_FILE)
    try:
        with open(path, encoding="utf-8") as f:
            orig = f.read()
    except OSError:
        orig = None
    with open(path, "w", encoding="utf-8") as f:
        f.write((orig.rstrip("\n") + "\n" if orig else "") + "APS5_TRACE_SUMMARY_EVERY=10\nAPS5_TRACE_DIR=%s\n" % run
                + "".join("%s=%s\n" % kv for kv in sorted((extra or {}).items())))
    st["env"] = {"path": path, "orig": orig}
    return run


def restore_env(st):
    e = st.pop("env", None)
    if not e:
        return
    try:
        if e["orig"] is None:
            os.remove(e["path"])
        else:
            with open(e["path"], "w", encoding="utf-8") as f:
                f.write(e["orig"])
    except OSError as x:
        log("could not restore %s: %s" % (e["path"], x))


def start_sampler(run, exe):
    subprocess.run(["systemd-run", "--unit=anyps5-devsample", "--collect", "--quiet", "-p", "Nice=10", sys.executable, DIR + "/devsample.py",
                    run + "/samples.jsonl", "--seconds", str(RUN_S + 60), "--match", os.path.basename(exe)], timeout=20, check=False)


def finish_perf(test, ran, ended):
    subprocess.run(["systemctl", "stop", "anyps5-devsample"], timeout=20, check=False, stderr=subprocess.DEVNULL)
    run = test.get("run")
    if not run:
        return
    r = subprocess.run([sys.executable, DIR + "/perf_report.py", "--device", DEVICE, "--game", os.path.basename(test["folder"]), "--dir", run,
                        "--log", os.path.join(test["folder"], "last-run.log"), "--history", DIR + "/perf-history.jsonl",
                        "--report", DIR + "/perf-report.md", "--ran", str(ran), "--ended", ended], capture_output=True, text=True, timeout=120)
    try:
        rec = json.loads(r.stdout)
        p = rec.get("perf") or {}
        log("%s: perf FPS %s (low %s), %s-bound; %s" % (test["folder"], p.get("fps_median", "-"), p.get("fps_p10", "-"), p.get("bound", "-"),
                                                     "; ".join(rec.get("changes", []) + rec.get("device_stats", {}).get("flags", []))))
    except ValueError:
        log("%s: perf report failed: %s" % (test["folder"], (r.stderr or "")[-200:]))


def input_idle(now):
    try:
        return now - os.path.getmtime(ACTIVE)
    except OSError:
        return float("inf")


def gpu_wedged():
    for path in glob.glob("/sys/kernel/debug/dri/*/i915_wedged"):
        try:
            if open(path).read().strip() not in ("", "0"):
                return True
        except OSError:
            pass
    return False


def gpu_hangs():
    r = subprocess.run(["dmesg"], capture_output=True, text=True, timeout=20)
    return r.stdout.count("GPU HANG")


def toolchain():
    try:
        for line in open(VERSION, encoding="utf-8"):
            if line.startswith("commit="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def save_gpu_error(folder):
    os.makedirs(ERRORS, exist_ok=True)
    for path in glob.glob("/sys/class/drm/card*/error"):
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            continue
        if len(data) < 200:
            continue
        name = "%s/%s-%s.gz" % (ERRORS, time.strftime("%Y%m%d-%H%M%S"), re.sub(r"[^A-Za-z0-9]+", "-", os.path.basename(str(folder or "none")))[:40])
        with gzip.open(name, "wb") as f:
            f.write(data)
        log("saved the GPU error state to %s" % name)
        return


def end_test(st, test, ran, ended, now):
    finish_perf(test, ran, ended)
    st.setdefault("tested", {})[test["folder"]] = now
    if gpu_hangs() > test.get("hangs", 0):
        st.setdefault("gpuHang", {})[test["folder"]] = toolchain()
        log("%s: hung the GPU; skipped until the toolchain changes" % test["folder"])
    st["test"] = None


def main():
    os.makedirs(DIR, exist_ok=True)
    st = load()
    if not st.get("test"):
        restore_env(st)
    now = time.time()
    try:
        host = msg({"state": True})
    except Exception as e:
        log("host not answering: %s" % e)
        return 0
    playing = host.get("playing")
    test = st.get("test")
    active = input_idle(now) < 60
    if gpu_wedged():
        if not st.get("wedgeSaved"):
            save_gpu_error(test["folder"] if test else playing)
            st["wedgeSaved"] = True
        if test:
            end_test(st, test, int(now - test["start"]), "GPU wedged", now)
        save(st)
        if input_idle(now) >= 120 and (not playing or test):
            log("GPU wedged (reset failed); rebooting")
            subprocess.run(["systemctl", "reboot"], timeout=20, check=False)
        elif not st.get("wedgeLogged"):
            log("GPU wedged (reset failed); waiting for the console to be idle before rebooting")
            st["wedgeLogged"] = True
            save(st)
        return 0
    st.pop("wedgeLogged", None)
    st.pop("wedgeSaved", None)
    if test:
        if playing or now - test["start"] > 1800:
            restore_env(st)
        mine = playing is not None and test.get("title") and test["title"] in str(playing)
        ran = int(now - test["start"])
        if not playing:
            log("%s: stopped by itself after %d s" % (test["folder"], ran))
            end_test(st, test, ran, "exited", now)
        elif not mine:
            subprocess.run(["systemctl", "stop", "anyps5-devsample"], timeout=20, check=False, stderr=subprocess.DEVNULL)
            st["test"] = None
        elif active:
            log("%s: console input after %d s; the game is Blu's now, left running" % (test["folder"], ran))
            end_test(st, test, ran, "handed to Blu", now)
        elif ran >= RUN_S:
            msg({"closegame": True})
            log("%s: still running after %d s, closed" % (test["folder"], ran))
            end_test(st, test, ran, "closed", now)
        save(st)
        return 0
    if playing or host.get("suspended") or input_idle(now) < IDLE_MIN * 60:
        save(st)
        return 0
    current = toolchain()
    hung = st.get("gpuHang", {})
    games = [g for g in library() if g.get("exe") and g.get("folder") and hung.get(g["folder"]) != current]
    if not games:
        save(st)
        return 0
    tested = st.get("tested", {})
    games.sort(key=lambda g: tested.get(g["folder"], 0))
    g, extra = games[0], None
    try:
        with open(DIAG, encoding="utf-8") as f:
            diag = json.load(f)
        os.remove(DIAG)
        match = [x for x in library() if x.get("exe") and x.get("folder") == diag.get("folder")]
        if match:
            g, extra = match[0], diag.get("env") or {}
            log("%s: diagnostic run with %s" % (g["folder"], " ".join("%s=%s" % kv for kv in sorted(extra.items()))))
    except (OSError, ValueError):
        pass
    run = start_perf(g["folder"], g["exe"], st, extra)
    save(st)
    try:
        note = msg({"play": g["folder"]}) or {}
    except Exception:
        restore_env(st)
        raise
    if "toast" in note:
        restore_env(st)
        log("%s: did not start: %s" % (g["folder"], note["toast"]))
        tested[g["folder"]] = now
        st["tested"] = tested
    else:
        st["test"] = {"folder": g["folder"], "title": g.get("title", ""), "start": now, "run": run, "hangs": gpu_hangs()}
        start_sampler(run, g["exe"])
        log("%s: started" % g["folder"])
    save(st)
    return 0


if __name__ == "__main__":
    sys.exit(main())
