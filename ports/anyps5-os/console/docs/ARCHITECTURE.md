# AnyPS5 console (native) architecture

Native C++20 home screen of AnyPS5 OS. SDL3 + SDL3_ttf + SDL3_image + nlohmann/json, nothing else.
It replaces `_tools/console/index.html`; that page's CSS/JS stays the visual and behaviour spec.

## Layout

```
src/main.cpp            arguments -> ui::Options -> ui::App::run
src/core/               App, Theme, Draw, Text, Images, Anim, Input, Pointer, Router, Screen, Toast
src/host/               HostClient (HTTP + SSE + fixture), Library, Settings, Http
src/platform/           fonts.cpp (font paths, video driver), posix/net.cpp, win/net.cpp
src/widgets/            shared drawing pieces (avatar, clock, pill, focus glow)
src/screens/            one screen per file, registered with ANYPS5_SCREEN
assets/                 logo-mark.png, icons/<name>.svg (copied next to the binary at build time)
tests/fixtures/         basic.json + art/ for the offscreen harness
```

CMake globs `src/core src/host src/widgets src/screens`, so a new screen is just a new `.cpp` (re-run cmake once).
Rules: no comments in code, small files, never special-case one game or ID, platform `#ifdef`s only in `src/platform`.

## Units and theme

Everything is drawn in pixels; convert the page's rem with `app().theme.r(x)`.
`rem = min(W/120, H/67.5)` (16 px at 1920x1080), the page's root font size.
CSS `top: Y; transform: translateY(-50%)` text means "vertical centre at Y": use `VAlign::Middle`.
`theme.W()`, `theme.H()` are the output size. Colours: `col::fg dim faint line panel ok warn bad page`,
`avatarColors()` (the page's COLORS gradients), helpers `rgba(r,g,b,a)`, `hex(0xRRGGBB,a)`, `white(a)`, `black(a)`, `alpha(c,a)`, `mix`.
`theme.reduceMotion/boldText/highContrast` follow Settings (`a11y.motion/bold/contrast`).

## Writing a screen

```cpp
#include "core/App.h"
#include "core/Images.h"
#include "core/Text.h"
#include "widgets/Widgets.h"
namespace ui {
class Library : public Screen {
public:
    bool overlay() const override { return false; }
    void enter() override { }
    void update(double dt) override { }
    bool action(const Action& a) override {
        if (a.is("back")) { app().router.go("home"); return true; }
        return false;
    }
    void host(const json& m) override { if (m.value("libraryLoaded", false)) { } }
    std::vector<Hint> legend() const override { return {{"Play", "ok"}, {"Back", "back"}}; }
    void draw(Draw& d) override {
        const Theme& t = app().theme;
        d.rect({0, 0, t.W(), t.H()}, hex(0x15171d));
        text::draw(d, "Game Library", t.r(10.8f), t.r(5.4f), {t.r(2.4f), Weight::Light, white(), Align::Left, VAlign::Middle});
    }
};
ANYPS5_SCREEN(Library, "library");
}
```

- Screens are created once (lazily) and kept, so members keep state between visits. `enter()` runs on every open.
- `id` is the registered name. Ids the core already opens: `boot users home` (core), `help cc create search library gallery
  hub gamemenu viewer passcode adduser settings trophies notifications collection gameinfo` (screen agents). `app().open(id)` does nothing
  (logs) while an id is not registered, so screens can be built in any order.
- `overlay() == false`: full screen; `open()` replaces the stack with a 0.45 s cross-fade + 101.2% zoom (the page's `.screen`).
  `overlay() == true`: drawn above the screen below it, fades in 0.2 s; close it with `app().router.pop()` or `close(id)`.
- `action()` gets every input action while the screen is on top. Return true when handled. Unhandled `ps`, `help`, `create`
  fall back to opening `cc`, `help`, `create` (when someone is logged in).
- `host(m)` gets every host message (all created screens, not only the top), plus the core's own
  `{"libraryLoaded":true}` and `{"mediaLoaded":true}` after a reload.
- `legend()` is the bottom-right button hint row (the page's LEG table); keys use action names (ok back ps options create
  square triangle r1 l1 r2 l2 enter space help). Keyboard/pad labels switch on `app().input.lastPad`.
- `typing()` true turns on SDL text input while the screen is on top; typed text arrives as `Action{"text", "<utf8>"}`,
  `space`, `square` (Backspace), `enter` (Return).

## Router (`app().router`)

`go(id)`, `push(id)`, `open(id)` (picks go/push from `overlay()`), `pop()`, `close(id)`, `isOpen(id)`, `top()`, `base()`,
`baseId()`, `get(id)` (the instance, for calling a screen's own methods after `dynamic_cast`). `Router::has(id)`.

## App (`ui::app()`)

| member | what |
|---|---|
| `theme` | sizes, rem, a11y flags |
| `host` | HostClient |
| `settings` | the page's `S` object (`settings.S` is the json) |
| `library` | parsed `/library.js` |
| `H` | latest host state (`host:true` message merged: `settings playing paused pads build program root drives`) |
| `me` | logged-in profile json (null before login) |
| `login` | profile waiting for the passcode page (set by user select before `open("passcode")`) |
| `focusGame` | raw json of the game selected on home (null on the library/media tiles) |
| `viewer` | `{items:[{img,title,sub,mid}], index}` set by home before `open("viewer")` |
| `media` | parsed `cache/media.js` (`{items:[...]}`) |
| `ingame` | a game is running and the host showed us over it |
| `toasts` | `toasts.push(text, src, iconUrl)`, `toasts.history()` (newest first, 30) for the notifications list |
| `input` | `lastPad`, `swapOkBack`, hold speed |

Functions: `open(id)`, `enterHome(profile)`, `saveSettings()` (POST `{"settings": S}`), `applySettings()` (after changing
`settings.S`), `reloadLibrary()`, `reloadMedia()`, `play(game)` (the page's play(): resume / still-running toast /
`{"play": folder, "user", "legacy"}`), `playing()` (exe name from H), `playingGame()`, `clockText()` (12/24 h from
`S.clock24`), `wallNow()`, `meUid()`, `toast(text, src, icon)`, `quit()`.

## Drawing (`ui::Draw`, passed to `draw()`)

All coordinates in pixels, `Rect` = `SDL_FRect {x, y, w, h}`. Every call respects the alpha stack.

| call | CSS equivalent |
|---|---|
| `rect(r, c)` | plain box |
| `round(r, radius, c)` | `border-radius` box (anti-aliased edge) |
| `roundGradient(r, radius, angleDeg, a, b)` | `linear-gradient(<angle>, a, b)` inside a rounded box (avatars) |
| `linear(r, angleDeg, {{pos, color}, ...})` | `linear-gradient` with stops (0deg = to top, 90 = to right, 180 = to bottom) |
| `field(r, cols, rows, f(x,y)->Color)` | any radial/conic gradient; cache it with `cached()` |
| `outline(r, radius, w, c)` | `box-shadow: 0 0 0 w c` (ring outside the box) |
| `stroke(r, radius, w, c)` | `border: w solid c` (inside) |
| `shadow(r, radius, blur, c, dx, dy, spread)` | `box-shadow: dx dy blur spread c` |
| `soft(r, radius, halfBlur, c)` | glow around a box (`box-shadow: 0 0 2*halfBlur c`) |
| `circle(cx, cy, r, c)`, `dot(cx, cy, r, inner, outer)` | circle; radial dot (bokeh, soft glow) |
| `line(x1, y1, x2, y2, w, c)` | separator lines |
| `image(tex, dst, Fit::Cover/Contain/Fill, alpha, radius, tint, focusX, focusY)` | `background-size: cover/contain` + radius; focus = `background-position` |
| `icon(tex, cx, cy, size, tint)` | centred icon |
| `pushClip(r)/popClip()` | `overflow: hidden` (rectangular) |
| `pushAlpha(a)/popAlpha()` | `opacity` for a group |
| `beginLayer()/endLayer(alpha, scale, dx, dy)` | group opacity/scale composited correctly (costs a render target) |
| `cached(key, w, h, paint)` | paint once into a texture (backgrounds); `dropCache(key)` |

Widgets (`widgets/Widgets.h`): `avatar(d, cx, cy, dia, profile, letterPx)`, `initial(profile)`, `clock(d, right, cy, px)`,
`pill(d, r, label, px, selected)` (the Play pill), `focusGlow(d, r, radius)` (the page's `.sel` glow: 0.19rem white ring +
1.6rem blue glow).

## Text (`ui::text`)

`text::draw(d, str, x, y, TextStyle{size, weight, color, align, valign, shadow, upper})` returns the width.
`Weight::Light/Regular/Medium/Semibold/Bold` (CSS 300/400/500/600/700). `VAlign::Top` = top of the line box,
`Middle` = centred on y, `Baseline`. `shadow` (px) draws a soft dark text-shadow.
`width(s, Font{size, weight})`, `lineHeight(font)`, `ellipsize(s, font, maxW)`, `wrap(s, font, maxW, maxLines)` (last
line gets an ellipsis), `drawLines(d, lines, x, y, lineH, style)` (CSS line-height = lineH), `upper(s)`.
Fonts come from system paths (`platform::fontPaths`): Noto Sans on Linux, Arial on macOS, Segoe UI on Windows, plus symbol
fallbacks. Never download fonts.

## Images (`ui::images`)

`images::get(url)` returns the texture or nullptr while loading (decode on 2 worker threads, upload on the UI thread);
just call it every frame. URLs are the library's relative paths (`art/PPSA.../icon.jpg`), fetched from the host
(`GET /art/...`) or, in fixture mode, from the fixture's folder. `failed(url)`.
`images::icon(name, px)` loads `assets/icons/<name>.svg` at that pixel size (white strokes; tint with `Draw::icon`).
Add icons by copying the page's inline `<svg>` body into `assets/icons/<name>.svg` with the root
`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">`.
`images::asset("logo-mark.png")` loads any other file in `assets/`.

## Animation (`ui::Tween`, `core/Anim.h`)

`Tween t; t.snap(v); t.set(target, seconds, ease, delay); t.restart(from, to, seconds, ease, delay); t.value(); t.done();`
Eases: `easeOut` = cubic-bezier(.2,.8,.2,1) (the page's main curve), `easeCss` = `ease`, `easeInOut`, `easeArt`
= (.2,.7,.2,1), `easeLinear`, or `cubicBezier(...)`. `now()` is the app clock (fixed 1/60 s steps in the harness).
Reduce Motion makes every new tween instant. Keep a Tween per animated property, like a CSS transition.

## Input (`ui::Action`)

`a.name`: `ok back up down left right ps options create l1 r1 l2 r2 l3 r3 square triangle zoom help tab fullscreen`,
digits `0`-`9` (keyboard, not typing), and while typing `text` (`a.text`), `space`, `enter`, `square` (Backspace).
`a.repeat` for held keys (keyboard OS repeat; pad directions repeat after 380 ms every 110 ms, Settings hold speed).
`a.pad` = came from a gamepad. Keyboard: arrows/WASD, Enter/Space ok, Esc/Backspace back, Q ps, O options, C create,
H help, Z zoom, / triangle, Delete square, PgUp/PgDn l1/r1, F11 fullscreen. Pad: south ok, east back (swapped when
`a11y.swap`), west square, north triangle, View/Back create, Menu/Start options, Guide ps, shoulders l1/r1, triggers l2/r2.

## Pointer (`core/Pointer.h`, mouse, touchpad, touchscreen)

```cpp
#include "core/Pointer.h"
pointer::target(r, [this, i] { focus_ = i; }, [this, i] { focus_ = i; action({"ok", ""}); }, radius);
pointer::scrollArea(listRect, [this](float rows) { scrollBy(rows); });
```

- Call `target(rect, onHover, onClick, radius)` from `draw()` every frame for each clickable thing; draw order is z order
  (the last target drawn under the pointer wins). `onHover` runs once when the pointer moves onto it (set your focus index
  so the focus glow follows); `onClick` runs on left click / tap / touchpad tap (after `onHover` if it was not hovered yet),
  normally the same code as `ok`. Either may be `nullptr`. `radius` rounds the hit corners.
- Callbacks run after the frame is drawn, so capture by value (`[this, i]`), never `[&]`.
- `scrollArea(rect, onScroll)`: wheel / two-finger touchpad / touchscreen swipe over it calls `onScroll(rows)` (fractional
  rows, + = down). Without a scroll area under the pointer the wheel turns into `up`/`down` actions, one per notch.
- Targets are cleared each frame and clipped by `pushClip` and skipped under `pushAlpha(0)`. Only the top screen (and the
  core's legend) can register them: screens below an overlay and screens fading out are ignored automatically.
- Right click = `back`, middle click = `options`, mouse back button = `back`, two-finger touchpad tap = `back`.
- `pointer::active()` is true while the mouse/touch was the last input (key and pad input clear it).
- Free targets from the core: `widgets::focusGlow` makes the focused element clickable (`ok`), the legend badges send their
  action (click "Back" = `back`).
- Cursor: shown on mouse/touchpad movement, hidden after 3 s idle or on key/pad input. Full screen (kmsdrm) and the harness
  draw it as a sprite last in the frame; `--windowed` uses the system cursor; `ANYPS5_HW_CURSOR=1` uses SDL's cursor
  under kmsdrm instead of the sprite.
- Touch: SDL's evdev turns the touchscreen into direct finger events (tap = click, swipe = scroll) and the touchpad into
  indirect finger events (one finger moves the cursor, tap = click, two fingers scroll); the touchpad's physical click
  arrives as a mouse button. Mouse events SDL synthesises from touch are off. At startup the log lists
  `pointer: touch device '...'` / `pointer: mouse '...'`; a device missing there was dropped by SDL (for example
  `Failed to get evdev touchscreen name`) and gives no input.

## Host (`app().host`, `host/HostClient.h`)

- `send(json msg, reply)`: POST /msg with a page message (`{"play":...}`, `{"resume":true}`, `{"power":...}`, `{"shot":true}` ...).
- `sys(area, action, args, reply)`: POST /sys -> reply `{ok, data, error, unsupported}` on the UI thread.
- `get(path, (status, body))`: GET on a worker thread, callback on the UI thread.
- `fetchBytes(url)`: blocking GET (worker threads only).
- Host messages (GET /events, server-sent events) arrive on the UI thread as `Screen::host(m)`; the core handles
  `toast`, `host` (state + first settings load), `library`, `mediaChanged`, `trophy`, `ingame`, `ps` first.
- Requests carry `Host: 127.0.0.1:8790` (and `Origin` on POST) as host.py requires. `--host URL` changes the address.

`host::Library`: `all` (vector of `host::Game {id title icon bg shot status lastRun folder exe fps saves raw}`),
`visible(showNoArt)`, `recent(showNoArt)`, `byId`, `byExeName`. `host::parseJsAssignment(text)` parses `window.X = ...;`.

`host::Settings`: `S` json, `load(o)`, `save(host)`, `profiles()`, `profileByUid/Name`, `isTestUser(p)`,
`a11y(k)`, `a11yv(k, default)`, `setA11y`, `noteOpt(k, default)`, `setNoteOpt`, `get(k, default)`, `clock24()`.
After changing `S`: `app().saveSettings(); app().applySettings();`.

## Game hand-off (Linux, `core/Handoff.cpp`, host `session.py`)

Without a compositor only one process can hold the DRM master, so the console gives the screen to the game and takes it back:

0. Launch card (`core/Launch.cpp`): Play shows a full-screen card (art, title, "Starting...", quit keys). The host sends
   `{"launch":{title,titleId,phase}}`: `starting`, `preparing` (with `percent`, `detail`, every 0.5 s while a collection
   install/build of that folder runs; Back hides the card), `started` (`pid`), `failed`. The host starts the game behind an
   exec gate (`/bin/sh -c 'read -r _; exec "$0" </dev/null'` on a stdin pipe), so the process exists before the display moves;
   the console keeps the card for at least 2 s and until `started`, then answers the release. Closing the gate pipe execs the game.
1. `{"play": folder}` -> host checks the game, publishes `{"display":"release"}` and waits (15 s max) for the console's
   `{"display":"released"}`. The console destroys renderer + window and quits `SDL_INIT_VIDEO` (the DRM fd closes, master
   drops), keeps running headless: host events, gamepads (SDL gamepad subsystem without video), no sounds.
2. Host starts the game in its folder (saves are cwd-relative) with `SDL_VIDEODRIVER=kmsdrm ANYPS5_FULLSCREEN=borderless`
   (kept if already set), output in `<game>/last-run.log`, then publishes `{"ingame":true,"handoff":true,"title"}` (no cc).
3. Game exits -> `{"ingame":false}` -> the console re-inits video and redraws; a non-zero exit adds the toast
   `<title> crashed (SIGSEGV). <last log line>. Log: <path>` (or `stopped (exit N)`). Launch failure -> `{"display":"acquire"}`.
   The console also takes the screen back by itself if no game started 20 s after a release.
4. While headless (keyboards in /dev/input rescanned every 2 s, so a late-plugged one works): PS button short press or Ctrl+Alt+Esc / Ctrl+Alt+Home
   or the Chromebook Overview key (top row, 5th key, evdev `KEY_SCALE` 120; also PS in the console UI under kmsdrm) -> `{"suspend":true}` (host SIGSTOPs the game's
   process group, publishes `{"suspended":true}`); holding PS (or the chord) 1 s -> `{"closegame":true}`.
   On `suspended` the console re-opens video and checks DRM master (`platform::drmMaster`). With master it opens cc
   (Resume -> `{"resume":true}` -> host repeats the release handshake, then SIGCONT, `{"suspended":false}`).
   Without it (every Vulkan game today: a stopped process keeps its master, SDL reports `kmsdrm not available`) the
   console sends `closegame` and, back on screen, toasts that the PS button closes games on AnyPS5 OS. Close on a stopped
   game sends SIGTERM + SIGCONT, SIGKILL after 5 s.

Back on screen after a game the console toasts how long it ran (`<title> ran for 3 min 12 s` or `closed after ...`).

Renderer re-creation: `Draw::reset` (bumps `Draw::generation()`; caches holding textures outside Draw compare it),
`images::setRenderer`, `text::dropTextures`, `pointer::init`. A new texture cache must forget its textures on a new
generation. Tests: `python -m unittest discover -s tests` in `os-host`, `sh tools/handoff-test.sh <you>` (offscreen).

## Fixture (`--host-file tests/fixtures/basic.json`)

```json
{
  "now": "2026-11-16T21:04:00",
  "state": { "host": true, "settings": { "profiles": [...] }, "playing": null, "paused": false, "pads": [] },
  "library": [ { "id": "PPSA22520", "title": "...", "icon": "art/PPSA22520/icon.jpg", "bg": "...", "shot": "...", "status": "Plays", "fps": 60, "lastRun": "...", "folder": "...", "exe": "...", "saves": 1 } ],
  "media": { "items": [ { "src": "...", "thumb": "...", "game": "...", "id": "..." } ] },
  "sys": { "<area>.<action>": { "ok": true, "data": {} } },
  "files": { "cache/downloads.json": { } },
  "events": [ { "toast": "Hello" } ]
}
```

`state` is delivered as the first host message, then each of `events`. `GET library.js` / `cache/media.js` are built from
`library` / `media`; other GETs read `files[path]` or a file relative to the fixture. `sys` answers by `"area.action"`,
missing ones reply `{ok:false, unsupported:true}`. Messages the app sends are printed as `sent {...}` on exit.
Add your own fixture files next to basic.json when a screen needs more data.

## Build, run, shoot (WSL Arch Linux on the PC)

Same OS as the Chromebook. Packages from pacman (sdl3 sdl3_ttf sdl3_image nlohmann-json noto-fonts), found with
`find_package`; no FetchContent, nothing built from source. Plain x86-64 (`-march=x86-64-v2`, set in CMake): never
`-march=native` or AVX, the Chromebook's N6000 has none. Do not use the Mac. Build out of tree, one folder per agent.

From Git Bash on Windows (`MSYS_NO_PATHCONV=1` stops Git Bash rewriting the `/mnt/...` paths):

```sh
MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- bash -lc 'cd "/mnt/c/AnyPS5 Games/_tools/console-native" && sh tools/build.sh <you>'
MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- bash -lc 'cd "/mnt/c/AnyPS5 Games/_tools/console-native" && sh tools/shot.sh <you> /mnt/c/<scratch>/home.png "ok,ok,right"'
```

`tools/build.sh <you>` = `cmake -S <src> -B /root/acn/<you>/build -G Ninja -DCMAKE_BUILD_TYPE=Release` (first time) +
`ninja -j8`. Re-run cmake (`rm /root/acn/<you>/build/build.ninja`) after adding a `.cpp`.
`tools/shot.sh <you> <out.png> [keys] [fixture] [extra args]` runs
`SDL_VIDEODRIVER=offscreen /root/acn/<you>/build/anyps5-console --offscreen 1920x1080 --host-file <fixture> --keys <keys> --shot <out.png>`.
Write the PNG under `/mnt/c/...` (your scratchpad) and look at it with the Read tool next to the page's CSS and
PS5-UI-SPEC.md. Sample pixels with Windows Python + PIL when a colour matters.

Harness: the offscreen run uses SDL's software renderer and is deterministic: fixed 1/60 s steps, the fixture's `now`,
2 s of boot frames (`--boot-ms`), each key then 400 ms (`--key-ms`), 1.7 s settle (`--settle-ms`) before the shot.
Keys are action names; `wait`, `wait:1200`, `text:abc`, `pad:ok` (pad-labelled legend), `event:N` (delivers the
fixture's `later[N]` host message now, see `handoff.json`), `pshold` (held PS while headless), and pointer steps
`move:x,y` (pixels, fires hover), `click:x,y` (move + left click), `wheel:n` (n notches at the pointer, + = down);
a key step hides the cursor again, like a real key press. `--start <id>` starts on another
screen (with `me` unset). Boot needs one key (`ok`) to reach user select, a second `ok` logs in and shows home.
Draw only through `Draw` (it works around a software-renderer bug with centre-fan quads); never call
`SDL_RenderGeometry` from a screen.

Real HTTP/SSE without host.py: `python tools/mockhost.py tests/fixtures/basic.json 8790 /tmp/<you>-mock.log &` in the WSL,
then `--host http://127.0.0.1:8790` instead of `--host-file`. It serves `/library.js`, `/events` (state, then a toast),
`/art/...`, logs every request (Host header check) and POST body.

Live: `./anyps5-console --host http://127.0.0.1:8790` (full screen; kmsdrm on a Linux console without X/Wayland),
`--windowed 1280x720` for a window.
