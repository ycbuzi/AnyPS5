import os
import re
import shutil
import sys

SRC = r"C:\AnyPS5 Games\_tools\console"
HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]
shutil.rmtree(out, ignore_errors=True)
os.makedirs(os.path.join(out, "Assets"))
for name in ("logo.png", "logo-mark.png"):
    shutil.copy2(os.path.join(SRC, name), out)
for folder, target in (("os", "ospages"),):
    shutil.copytree(os.path.join(SRC, folder), os.path.join(out, target), ignore=shutil.ignore_patterns("*.py", "__pycache__", "*.before-*"))
OS_TEXT = [
    ("bluetooth.js", '    R.sh("DualSense Wireless Controller");\n    R.note("To put a DualSense in pairing mode, hold the Create button and the PS button at the same time until the light bar blinks. Then choose Search for Accessories and select the controller when it appears.");',
     '    R.sh("Xbox Wireless Controller");\n    R.note("The Xbox pairs controllers itself. Press the pair button on the console, then hold the pair button on the controller until the Xbox button flashes fast. Controllers paired to the Xbox work in AnyPS5.");'),
]
for name, old, new in OS_TEXT:
    path = os.path.join(out, "ospages", name)
    with open(path, encoding="utf-8") as f:
        code = f.read()
    if old not in code:
        print("os text not found:", name, old[:60])
        sys.exit(1)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(code.replace(old, new))
shutil.copy2(os.path.join(HERE, "xbox.js"), out)
shutil.copy2(os.path.join(HERE, "xboxhost.js"), out)
with open(os.path.join(out, "library.js"), "w", encoding="utf-8", newline="\n") as f:
    f.write("window.LIBRARY = window.XBOX_LIBRARY || [];\n")
shutil.copy2(os.path.join(HERE, "AppxManifest.xml"), out)
with open(os.path.join(SRC, "index.html"), encoding="utf-8") as f:
    page = f.read()
page = page.replace("src=\"os/", "src=\"ospages/")
page = page.replace("<head>\n", "<head>\n<script src=\"xbox.js\"></script>\n<script src=\"xboxhost.js\"></script>\n", 1)
page = page.replace("] ?? 0;", "] || 0;")
XBOX_TEXT = [
    ('{ label: H.paused ? "Pipeline paused" : "Pipeline running", svg: H.paused ? \'<path d="M8 5v14l11-7z"/>\' : \'<path d="M9 6v12M15 6v12"/>\', run: () => send({ pause: !H.paused }), act: () => H.paused },', 'H.platform === "xbox" ? { label: "Reload library", svg: \'<path d="M4 12a8 8 0 0 1 14-5.3M20 12a8 8 0 0 1-14 5.3M18 3v4h-4M6 21v-4h4"/>\', run: () => send({ rescan: true }) } : { label: H.paused ? "Pipeline paused" : "Pipeline running", svg: H.paused ? \'<path d="M8 5v14l11-7z"/>\' : \'<path d="M9 6v12M15 6v12"/>\', run: () => send({ pause: !H.paused }), act: () => H.paused },'),
    ("if (shot) C.push({ label: \"Captures\"", "if (shot && H.platform !== \"xbox\") C.push({ label: \"Captures\""),
    ("Press the PS button on your controller, or any key.", "Press any button on your controller."),
    ("<p><b>PS button</b> (Xbox: Guide) control center; in a game, the game menu</p>", "<p><b>View</b> control center; in a game, the game menu</p>"),
    ("<b>Cross / A</b> select · <b>Circle / B</b> back", "<b>A</b> select · <b>B</b> back"),
    ("<b>Options / Menu</b> game options (···)", "<b>Menu</b> game options (···)"),
    ("<p><b>Create / View</b> Create menu: screenshots and captures</p>", "<p><b>Xbox button</b> the Xbox guide: captures, power, settings</p>"),
    ("<b>L1 / R1</b> switch tabs", "<b>LB / RB</b> switch tabs"),
    ("In a game: <b>PS button &gt; Close game</b>, or hold View + Menu on an Xbox pad", "In a game: <b>View &gt; Close game</b>"),
    ("control center (like the PS button)", "control center (like View on the controller)"),
    ("is still running. PS button > Close game first.", "is still running. View > Close game first."),
    ("create: b(8)", "menu: b(8)"),
    ("""{ label: "Screenshot", svg: '<rect x="3" y="6" width="18" height="13" rx="2"/><circle cx="12" cy="12.5" r="3.5"/><path d="M9 6l1.5-2h3L15 6"/>', run: () => { closeCc(true); send({ shot: true }); } },""",
     """H.platform === "xbox" ? { label: "Settings", svg: '<circle cx="12" cy="12" r="3.2"/><path d="M12 2.8v2.4M12 18.8v2.4M2.8 12h2.4M18.8 12h2.4M5.5 5.5l1.7 1.7M16.8 16.8l1.7 1.7M5.5 18.5l1.7-1.7M16.8 7.2l1.7-1.7"/>', run: () => { closeCc(true); openSettings(); } } : { label: "Screenshot", svg: '<rect x="3" y="6" width="18" height="13" rx="2"/><circle cx="12" cy="12.5" r="3.5"/><path d="M9 6l1.5-2h3L15 6"/>', run: () => { closeCc(true); send({ shot: true }); } },"""),
    (" The test pipeline keeps working.", ""),
    ("function allCats() { const out = CATS.slice();", "function allCats() { const out = CATS.filter(c => c[0] !== \"pipe\" && c[0] !== \"capture\");"),
    ("return base.concat(extra);", "return base.concat(extra).filter(s => [\"Shortcuts for Create Button\", \"Streaming\", \"Other Windows\"].indexOf(s.name) < 0);"),
    ("Swap Cross and Circle", "Swap A and B"),
    ("makes Circle select and Cross go back", "makes B select and A go back"),
    ("""R.sh("Buttons"); R.push({ label: "PS / Guide Button Opens", val: S.psAction === "home" ? "Home Screen" : "Control Center", run: () => { S.psAction = S.psAction === "home" ? "cc" : "home"; saveSettings(); } }); R.note("In a game the PS button always opens the game menu. PlayStation controllers held by Steam Input or DS4Windows show as in use and their PS button cannot be read.");""",
     """R.sh("Buttons"); R.note("View opens the control center, Menu opens options, Y opens search, LB and RB switch tabs. The Xbox button opens the Xbox guide.");"""),
    ('"Turn Off AnyPS5", (g ? "Closes " + g + " and " : "Closes ") + "AnyPS5."', '"Turn Off AnyPS5", (g ? "Closes " + g + " and " : "Closes ") + "AnyPS5 and goes back to the Xbox Home."'),
    ("if (!seen) setTimeout(openHelp, 1200);", "if (!seen) setTimeout(() => { if (screen === \"home\" && !helpOn) openHelp(); }, 1200);"),
    ('profiles: [["Select", "ok"], ["Help", "create"]], home: [["Select", "ok"], ["Options", "options"], ["Control center", "menu"], ["Create", "create"]]', 'profiles: [["Select", "ok"]], home: [["Select", "ok"], ["Options", "options"], ["Control center", "menu"], ["Search", "search"]]'),
    ('const KEYNAME = { ok: ["Enter", "A"], back: ["Esc", "B"], menu: ["Q", "PS"], options: ["O", "☰"], help: ["H", "⧉"], r1: ["Tab", "R1"], create: ["C", "⧉"], del: ["Del", "□"], space: ["Space", "△"], enter: ["Enter", "R2"] };', 'const KEYNAME = { ok: ["Enter", "A"], back: ["Esc", "B"], menu: ["Q", "⧉"], options: ["O", "☰"], help: ["H", "⧉"], r1: ["Tab", "RB"], create: ["C", "⧉"], del: ["Del", "X"], space: ["Space", "Y"], enter: ["Enter", "RT"], search: ["/", "Y"] };'),
    ("  C.push({ glyph: '<path d=\"M4 6h6v4H4zM14 14h6v4h-6zM10 8h4v8\"/>', label: \"Test pipeline\"", "  if (H.platform !== \"xbox\") C.push({ glyph: '<path d=\"M4 6h6v4H4zM14 14h6v4h-6zM10 8h4v8\"/>', label: \"Test pipeline\""),
]
for old, new in XBOX_TEXT:
    if old not in page:
        print("xbox text not found:", old[:60])
        sys.exit(1)
    page = page.replace(old, new)
sys.path.insert(0, HERE)
import csscompat
page = re.sub(r"<style>([\s\S]*?)</style>", lambda m: "<style>" + csscompat.fix(m.group(1)) + "</style>", page)
scripts = []


def extract(match):
    name = "inline-%d.js" % len(scripts)
    scripts.append(name)
    with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(match.group(1))
    return "<script src=\"%s\"></script>" % name


page = re.sub(r"<script>([\s\S]*?)</script>", extract, page)
modern = []
for name in scripts + [os.path.join("ospages", n) for n in os.listdir(os.path.join(out, "ospages")) if n.endswith(".js")]:
    with open(os.path.join(out, name), encoding="utf-8") as f:
        code = f.read()
    for token in re.findall(r"\?\?|\?\.(?!\d)", code):
        modern.append("%s: %s" % (name, token))
if modern:
    print("syntax the Xbox web host cannot parse:", modern[:10])
    sys.exit(1)
with open(os.path.join(out, "index.html"), "w", encoding="utf-8", newline="\n") as f:
    f.write(page)
print("inline scripts moved to files:", len(scripts))
try:
    from PIL import Image
    mark = Image.open(os.path.join(SRC, "logo-mark.png")).convert("RGBA")
    for name, w, h in (("StoreLogo.png", 50, 50), ("Square150x150Logo.png", 150, 150), ("Square44x44Logo.png", 44, 44), ("Wide310x150Logo.png", 310, 150), ("SplashScreen.png", 620, 300)):
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        side = int(min(w, h) * 0.7)
        icon = mark.copy()
        icon.thumbnail((side, side))
        canvas.paste(icon, ((w - icon.width) // 2, (h - icon.height) // 2), icon)
        canvas.save(os.path.join(out, "Assets", name))
    print("assets from logo")
except Exception as error:
    print("logo assets failed:", error)
    sys.exit(1)
print("staged", out)
