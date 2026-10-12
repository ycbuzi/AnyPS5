# Tested compatibility

Every row is one or more reports on the hosts they were run on, not a general claim about the title. A result on a fork branch or with local patches says nothing about stock `main`, and a result on one GPU vendor often does not carry to the other.

**Status** is the furthest state any report establishes:

- **Playable** — gameplay is reached and sustained at a usable frame rate, and the report calls it playable.
- **In game** — gameplay is reached and sustained, but named blockers remain (crashes, missing rendering, stutter) and the report stops short of calling it playable.
- **In game, not playable** — gameplay is reached and the report says it is not playable: a single-digit frame rate, or the 3D scene is never drawn.

**Build** is what the result needed:

- **`main`** — stock `main`, no open pull requests, no local patches, no environment switches.
- **`main` + open PRs** — only pull requests that are open in this repository.
- **fork branch** — a branch on a contributor's fork, which may not correspond to any pull request.
- **local patches** — changes that exist only in the reporter's tree.

`?` means not tested or not reported.

| Game | ID | Status | Windows | Linux | Build | Evidence |
|------|----|--------|---------|-------|-------|----------|
| Astro's Playroom | PPSA01325 | In game, not playable | ? | First level `cpuplaza_mini_04`, jump and punch answer, 4-11 FPS | `main` + open PRs | [#3232](https://github.com/boykopovar/AnyPS5/discussions/3232), [#2476](https://github.com/boykopovar/AnyPS5/issues/2476) |
| Demon's Souls | PPSA01341 | In game, not playable | ? (PPSA01342 reaches the main menu only, see below) | Tutorial level (Outpost Passage) with HUD, 2.0-2.2 FPS, 3D scene wrong | fork branch + local patches | [#1283](https://github.com/boykopovar/AnyPS5/discussions/1283), [#2552](https://github.com/boykopovar/AnyPS5/issues/2552) |
| Dreaming Sarah | PPSA02929 | Playable | In game, playable, 58-60 FPS | ? | `main` (release v0.1.1) | [#3003](https://github.com/boykopovar/AnyPS5/discussions/3003), [#3018](https://github.com/boykopovar/AnyPS5/discussions/3018) |
| One Night Stand | PPSA03030 | Playable | Runs correctly, 49-60 FPS, no crash or failed presentation | ? | `main` (`ccf9ff90`) | [#3356](https://github.com/boykopovar/AnyPS5/issues/3356) |
| Silt | PPSA03306 | In game | Menu, new game and several areas with sound, 44-52 FPS | Stops on an undefined symbol in `Il2CppUserAssemblies` | `main` + local patches, `APS5_IGNORE_MSAA=1` | [#2810](https://github.com/boykopovar/AnyPS5/discussions/2810), [#2126](https://github.com/boykopovar/AnyPS5/discussions/2126) |
| 3DMiniGolf | PPSA03647 | In game | Menus, first course, first hole completed | ? | `main` + open PRs + local patches | [#2431](https://github.com/boykopovar/AnyPS5/discussions/2431) |
| My Friend Peppa Pig | PPSA03706 | In game | Gameplay at about 30 FPS; closing the window sometimes aborts | ? | fork branch (`main` + [#419](https://github.com/boykopovar/AnyPS5/pull/419) + [#2169](https://github.com/boykopovar/AnyPS5/pull/2169)) | [#3506](https://github.com/boykopovar/AnyPS5/issues/3506) |
| Grand Theft Auto V | PPSA04264 | Playable | North Yankton prologue in Performance mode, 27-28 FPS, with rendering, music, speech and HUD | ? | fork branch | [#1203](https://github.com/boykopovar/AnyPS5/discussions/1203), [#4432](https://github.com/boykopovar/AnyPS5/issues/4432) |
| Sonic Origins Plus | PPSA05324 | Playable | ? | Sonic 1 Green Hill and Marble Zone Act 1, and a Knuckles game, 51-60 FPS at 960x540 | fork branches + open PRs + local patches | [#2021](https://github.com/boykopovar/AnyPS5/discussions/2021) |
| Uncharted: Legacy of Thieves Collection | PPSA05684 | In game, not playable | ? | Menu 6.4-6.7 FPS; Uncharted 4 chapter 1 boat level renders and plays at about 1 FPS | `main` + open PRs + local patches | [#4001](https://github.com/boykopovar/AnyPS5/discussions/4001) |
| PAC-MAN WORLD Re-PAC | PPSA06399 | In game | Title screen, menus and levels render, audio works, a level completed; aborts on the level-complete transition | ? | `main` + local patches | [#3443](https://github.com/boykopovar/AnyPS5/discussions/3443) |
| Hot Shots Golf 2 | PPSA06699 | Playable | In game, playable. Menus 60 FPS; the 3D course 45-49 FPS on plain `main`, about 60 FPS with the open performance drafts | ? | `main`; open PRs for correctness and for 60 FPS | [#3839](https://github.com/boykopovar/AnyPS5/discussions/3839) |
| Road of Death | PPSA07286 | Playable | Main loop, room changes, music streaming, save data read and written | ? | `main` + open PRs + local patches | [#3163](https://github.com/boykopovar/AnyPS5/issues/3163) |
| Hollow Knight: Silksong | PPSA12544 | Playable | Intro, menus and gameplay with room transitions, 60 FPS, on plain `main` (AMD). On NVIDIA it needs open PRs and `APS5_STORE_AT_FLIP=1` | Interactive main menu at 60 FPS; gameplay not reported | `main` (Windows, AMD); `main` + open PRs (Windows, NVIDIA) | [#726](https://github.com/boykopovar/AnyPS5/discussions/726), [#1996](https://github.com/boykopovar/AnyPS5/discussions/1996), [#3731](https://github.com/boykopovar/AnyPS5/issues/3731) |
| High on Life | PPSA15388 | In game | ? | First level with player control, gun, HUD, pause menu and checkpoint reload; every lit 3D surface draws black | `main` + local patches | [#4139](https://github.com/boykopovar/AnyPS5/issues/4139), [#4185](https://github.com/boykopovar/AnyPS5/issues/4185) |
| Disney Epic Mickey: Rebrushed | PPSA15657 | In game, not playable | Crashes at the start of the level | Dark Beauty Castle loads and the game runs (dialogue, quest, Mickey takes damage), but Unreal never draws the 3D view | `main` + open PRs + local patches, `APS5_NO_HOST_IMPORT=1` | [#2255](https://github.com/boykopovar/AnyPS5/discussions/2255), [#2365](https://github.com/boykopovar/AnyPS5/issues/2365) |
| Ready or Not | PPSA18605 | In game, not playable | Sign-in, language, main menu, then the station level with the briefing and the tutorial panel; 5-6 FPS in the menu and under 2 FPS in the level, most lighting missing | ? | `main` + open PRs | [#4184](https://github.com/boykopovar/AnyPS5/discussions/4184) |
| Minecraft Preview | PPSA19639 | Playable | Title screen 60 FPS; create, load and walk a world, Save & Quit, keyboard and audio. In-world at the default render distance 7 FPS on `main`, 31 FPS with the patch; 60 FPS at lower render distances | ? | `main` + local patches | [#1277](https://github.com/boykopovar/AnyPS5/discussions/1277) |
| STAR WARS: Dark Forces Remaster | PPSA19728 | Playable | First mission completed with keyboard and mouse | ? | `main` + local patches | [#4310](https://github.com/boykopovar/AnyPS5/discussions/4310) |
| Big Helmet Heroes | PPSA19943 | In game, not playable | ? | New game, difficulty and player select, tutorial HUD and character; 4-5 FPS in the menu and 0.7-1.2 FPS in the tutorial, much of the scene black | fork branch + open PRs | [#3873](https://github.com/boykopovar/AnyPS5/discussions/3873) |
| Balatro | PPSA21402 | Playable | Full gameplay at about 60 FPS with audio, no aborts or garbled draws, over about 10 minutes | ? | `main` (`4a747c48`) | [#1142](https://github.com/boykopovar/AnyPS5/discussions/1142), [#1034](https://github.com/boykopovar/AnyPS5/issues/1034) |
| Astro Bot | PPSA21564, PPSA21567 | In game, not playable | Title screen about 45 FPS, intro about 15 FPS, gameplay 3 FPS; up to 9-11 FPS in `hub_crashsite` with local performance work | Tutorial reported playable on the `astrobot` branch | fork branch (`astrobot`); on `main` + open PRs it reaches the menu, the first level and Space Station | [#357](https://github.com/boykopovar/AnyPS5/discussions/357) |
| Vampire Survivors | PPSA21703 | Playable | Boot, menus, save and load, full runs with level-ups and chests, the results screen, 60 FPS (AMD). On NVIDIA it stops when the PSN plugin starts without its start argument | ? | `main` + local patches | [#3745](https://github.com/boykopovar/AnyPS5/discussions/3745) |
| Animal Well | PPSA22520 | Playable | In game, playable at 60 FPS, audio matching the PC release | ? | `main` (`2ba993a8`) | [#966](https://github.com/boykopovar/AnyPS5/discussions/966) |
| God of War: Sons of Sparta | PPSA28997 | Playable | Title screen, cutscenes, gameplay, the first Cyclops boss, the River area and the first save altar; save data persists. Frame-rate drops, and one reporter saw 5 crashes in 2 hours | ? | `main` (`b42ef24e`) | [#1742](https://github.com/boykopovar/AnyPS5/discussions/1742) |
| Wobbly Life | PPSA29413 | In game | ? | Main menu, tutorial intro at 60 FPS, in-tutorial 3D reached; the open-world tutorial renders at about 1 FPS and then aborts on vertex routing | `main` (`2ccbd376f`) | [#2211](https://github.com/boykopovar/AnyPS5/discussions/2211), [#4329](https://github.com/boykopovar/AnyPS5/issues/4329), [#4070](https://github.com/boykopovar/AnyPS5/issues/4070) |
| Hades II | PPSA36082 | Playable | ? | Boots, loads a game and plays with no skipped draws; the hub hits the 60 FPS cap. Only boots with warm shader caches | `main` + open PRs + local patches, `APS5_HOST_IMPORT_MIB=8192` | [#1965](https://github.com/boykopovar/AnyPS5/discussions/1965) |
| Yamagi Quake II (homebrew) | PPSA99007 | Playable | Menu, New game, `demo1` (Outer Base), 1140+ frames at about 30 FPS with no error | ? | `main` + local patches | [#1413](https://github.com/boykopovar/AnyPS5/issues/1413) |
| DOOM Shareware (homebrew) | PPSA99666 | Playable | In game (E1M1 and later maps) | In game (E1M1) | `main` + open PRs (see the bullets below) | [#1578](https://github.com/boykopovar/AnyPS5/issues/1578), [#2475](https://github.com/boykopovar/AnyPS5/discussions/2475) |

Measured frame rates on the two hosts this page started with:

| Game           | ID        | GTX 1050 Ti / i5-7500 3.4GHz | Intel(R) HD Graphic 620 / i5-7200 2.5GHz |
|----------------|-----------|------------------------------|------------------------------------------|
| Dreaming Sarah | PPSA02929 | 60 FPS                       | 36 FPS                                   |
| DOOM Shareware (homebrew) | PPSA99666 | ? | ? |

## Duplicate and near-duplicate entries

- **PPSA21564 and PPSA21567 are the same game**, Astro Bot, in two regions (`v01.007.000` and `v01.018.000` in the reports). They are one row. Both reach gameplay only on the `astrobot` fork branch, and both are a few frames per second there.
- **PPSA01341 and PPSA01342 are both Demon's Souls**, and they are not at the same point. PPSA01341 v1.07 reaches the tutorial level on Linux at 2.0-2.2 FPS. PPSA01342 v01.005.000 on Windows reaches the logos (about 38 FPS), the title screen and the main menu (about 37 FPS) on a fork branch, and then hangs on "New Game" to "Continue Offline"; character creation and levels are not reached. Only PPSA01341 has a gameplay row.

## Notes per title

DOOM Shareware is a public homebrew, [lowbit/ps5-doom](https://github.com/lowbit/ps5-doom), release v1.1.1 (content version 01.001.001).

- Windows 11, RTX 4070 (version 01.001.000, an integration build before [#553](https://github.com/boykopovar/AnyPS5/pull/553), [#683](https://github.com/boykopovar/AnyPS5/pull/683), [#684](https://github.com/boykopovar/AnyPS5/pull/684) and [#685](https://github.com/boykopovar/AnyPS5/pull/685) were merged): movement and shooting in E1M1. [In-game screenshots](https://github.com/boykopovar/AnyPS5/pull/685#issuecomment-5997547697).
- Windows 11, RTX 4070 SUPER (v1.1.1, `main` `2edaf7f4` plus open PRs, reported in [#690](https://github.com/boykopovar/AnyPS5/pull/690)): further maps, weapon switching, new games, level select, save/load round trip and audible audio.
- Linux, Ubuntu 24.04 container, Mesa llvmpipe, no hardware GPU (v1.1.1, `main` `0e173a46` plus [#1278](https://github.com/boykopovar/AnyPS5/pull/1278), [#1279](https://github.com/boykopovar/AnyPS5/pull/1279) and [#1307](https://github.com/boykopovar/AnyPS5/pull/1307), relinked with `--exclude-sce-module libc.prx`, see [#1497](https://github.com/boykopovar/AnyPS5/issues/1497)): E1M1, 35 FPS in game; audio and extended gameplay not tested. The `/download0` directory must exist beside the executable.
- Linux, CachyOS, NVIDIA RTX 3080 Ti, driver 610.57.04, KDE Plasma Wayland (v1.1.1, `main` `a11c8f7f7` without extra PRs; `eboot.bin` and `sce_module/libc.prx` unwrapped from fSELF before relinking, see [#2599](https://github.com/boykopovar/AnyPS5/issues/2599); the title's `libc.prx` converted as a guest module, no `--exclude-sce-module`): launcher at 60 FPS, E1M1 at 35 FPS in game; new game from the menu, movement, turning, firing, an item pickup and the automap checked with controller input from `APS5_PAD_INPUT_FILE`. Audio output opened at 48000 Hz but was not listened to; later maps and save/load not tested. The `/download0` directory did not need to exist before the first run.

Dreaming Sarah is playable on a GTX 1050 Ti and on Intel HD 620, and does not start at all on an AMD Radeon RX 6700 XT: it crashes at its first real graphics submission inside `amdvlk64.dll`, at the same address as nine `agc_driver` execution tests on that machine. The cause is the background link-time-optimized pipeline link; `APS5_NO_PIPELINE_LTO=1` is a workaround, and [#3149](https://github.com/boykopovar/AnyPS5/pull/3149) fixes it there. See [#2944](https://github.com/boykopovar/AnyPS5/issues/2944) and [#3018](https://github.com/boykopovar/AnyPS5/discussions/3018).

Hollow Knight: Silksong is the only retail title reported playable on plain `main` with nothing else at all, and that is on Windows with an AMD GPU. The same title on Windows with NVIDIA needs open PRs and `APS5_STORE_AT_FLIP=1`, or the backgrounds are garbled or it crashes while loading `tut_01`. On Linux with NVIDIA only the interactive main menu is reported, and that run patched the title's own modules, which the project does not carry.

God of War: Sons of Sparta is the only retail title with a gameplay result on an unmodified upstream `main` commit, on Windows with an AMD RX 6800 XT / RX 6700 XT. Reports disagree about how well it runs: the gameplay reporter calls it playable apart from noticeable frame-rate drops, a second reporter sees stutter and no consistent 60 FPS, a third saw 5 crashes in 2 hours, and a fourth reports 10-12 FPS in the menu alone on an RTX 4070 SUPER. Frame times were not measured in any of them.

Wobbly Life reaches the tutorial on pure `main`, with the menu and the tutorial intro at 60 FPS, but the open-world part of the tutorial runs at about 1 FPS and then aborts deterministically on `VGT_SHADER_STAGES_EN` vertex routing in 4 of 4 runs, so the tutorial cannot be finished. It needs `--to-intel` on Intel hosts: the title ships AMD-only `insertq`.

Disney Epic Mickey: Rebrushed had an earlier "reaches gameplay" report that the reporter corrected: the images were the pre-rendered Bink intro movie with the game's HUD on top. With that movie skipped, the level loads and the game's logic runs, but Unreal submits only about 19 draws per frame (HUD and a few small passes) and never draws the 3D view.

Astro Bot reaches gameplay only on the `oneandonlydean/astrobot` fork branch, which is frozen as a performance reference and is not being fixed further. On upstream `main` with [#425](https://github.com/boykopovar/AnyPS5/pull/425), [#454](https://github.com/boykopovar/AnyPS5/pull/454), [#2490](https://github.com/boykopovar/AnyPS5/pull/2490) and [#2491](https://github.com/boykopovar/AnyPS5/pull/2491) it reaches the menu, the first level and Space Station. No report has it above about 11 FPS anywhere.

Hades II boots only with warm shader caches. The first cold start has to compile the title's shaders before its 512 MiB upload pool overflows, and that is the main thing still open for it.

Minecraft Preview, STAR WARS: Dark Forces Remaster, Yamagi Quake II, Silt, PAC-MAN WORLD Re-PAC and Vampire Survivors are each playable or in game with local patches that have no pull request. Their reporters say so explicitly; none of them is a claim about stock `main`.

- On the first run without a shader cache, the presentation kernel is compiled at its first dispatch, frame 1 does not complete in time and the title switches to the CPU scaler. Later runs with the cached kernel stay on AGC compute presentation (measured on Windows 11, RTX 3080, `main` `d70b8998`).

Not verified: Windows on current `main` without extra PRs for most rows, Linux with AMD or Intel GPUs for most rows, and any long-session or save/load stability beyond what each report states.
