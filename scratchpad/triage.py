"""Rank every open AnyPS5 issue against what this machine can actually do.

Capabilities: Windows 11, MinGW toolchain, RTX 4070 SUPER + GTX 1650 SUPER + Radeon 610M
(RDNA2, proprietary driver), DualSense, DOOM PPSA99666 playable, full build + suite.
Not available: Linux, macOS, Intel GPU, AMDVLK, PSVR2, a PS5 console, retail title dumps.
"""
import json
import re
import sys

PATH = sys.argv[1]
issues = json.load(open(PATH, encoding="utf-8"))

BLOCKED = [
    (r"\blinux\b|fedora|cachyos|ubuntu|lavapipe|radv\b|mesa\b", "linux-only"),
    (r"\bmacos\b|moltenvk|apple silicon|metal\b", "macos-only"),
    (r"\bintel\b.*\b(gpu|arc|anv)\b|\bmesa anv\b", "intel-gpu"),
    (r"amdvlk|\b780m\b|rdna ?3", "amdvlk/rdna3"),
    (r"psvr|\bvr\b tracker|hmd2", "needs-psvr2"),
]
# retail title dumps we do not have
TITLE = re.compile(r"PPSA(?!99)\d{5}|ratchet|astro ?bot|demon'?s souls|ghost of y|cyberpunk|"
                   r"tlou|wolverine|sifu|stray\b|hades\b|palworld|resident evil|re4\b|"
                   r"cult of the lamb|silksong|rift apart", re.I)
WINDOWS = re.compile(r"\bwindows\b|mingw|winsock|0xc000|amdvlk64|\.dll\b|ansi code page", re.I)
HOSTSIDE = re.compile(r"relinker|libc\b|libkernel|tools/|progress\.py|\btest(s)?\b|ctest|cmake", re.I)
READY = re.compile(r"fix branch ready|branch ready|implementation ready|fix available|branch is ready", re.I)
OURGPU = re.compile(r"\bnvidia\b|rtx|gtx|geforce|rdna ?2|radeon 6|\b0x164e\b", re.I)

rows = []
for it in issues:
    body = (it.get("body") or "")
    text = it["title"] + "\n" + body
    nc = len(it["comments"])
    reasons, score = [], 0

    # A platform is only a blocker when the ISSUE ITSELF is about that platform.
    # Merely mentioning Linux ("not tested on Linux", a CI note) must not disqualify
    # a Windows issue, which the first version of this script got wrong.
    title = it["title"]
    windows_issue = bool(re.search(r"\bwindows\b|mingw|0xc000|amdvlk64|ansi code page", title, re.I))
    blocked = None
    if not windows_issue:
        for pat, why in BLOCKED:
            if re.search(pat, title, re.I):
                blocked = why
                break
        if blocked is None:
            # fall back to the body only when the title is platform-neutral
            for pat, why in BLOCKED:
                if re.search(pat, body, re.I) and not WINDOWS.search(body):
                    blocked = why
                    break
    if not blocked and TITLE.search(title):
        blocked = "needs retail title"

    if READY.search(text):
        reasons.append("fix-ready(harvest)")
        score += 2
    if WINDOWS.search(text):
        reasons.append("windows")
        score += 4
    if HOSTSIDE.search(text):
        reasons.append("host-side")
        score += 3
    if OURGPU.search(text):
        reasons.append("our-gpu")
        score += 2
    if nc == 0:
        reasons.append("0-comments")
        score += 3
    if blocked:
        score -= 10

    rows.append((score, it["number"], nc, blocked, ",".join(reasons), it["title"][:74]))

rows.sort(key=lambda r: (-r[0], -r[1]))
print(f"{'score':>5} {'issue':>6} {'c':>3}  {'blocked':<18} flags / title")
print("-" * 118)
for s, n, nc, b, f, t in rows[:30]:
    print(f"{s:5d} #{n:<5} {nc:3d}  {(b or ''):<18} {f}")
    print(f"{'':35}{t}")
print()
print(f"actionable (score>0, not blocked): {sum(1 for r in rows if r[0] > 0 and not r[3])}")
print(f"blocked                          : {sum(1 for r in rows if r[3])}")
