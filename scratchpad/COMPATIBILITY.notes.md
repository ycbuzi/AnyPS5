# Review notes for COMPATIBILITY.draft.md

Not part of the proposed doc. These are the judgement calls, the things I could not
substantiate, and the conflicts a maintainer should resolve before this is merged.

## Row count

**29 rows** in the main table, covering **30 PPSA IDs** (Astro Bot's PPSA21564 and
PPSA21567 share one row). The second table (measured frame rates on the two original
hosts) keeps its 2 rows.

Of the 30 IDs in the task list, **29 are in the table**. One is not: PPSA01342, which is
Demon's Souls in a second region and reaches the main menu only. It is covered in the
"Duplicate and near-duplicate entries" section instead, since the table is a gameplay
table.

## Corrections to the "about seven are on pure main" list

The list in the task was: Silksong, Wobbly Life, Hades II, Animal Well, Hot Shots Golf 2,
Sons of Sparta, Balatro. Against the evidence:

| Title | Claim | What the evidence says |
|---|---|---|
| Hollow Knight: Silksong PPSA12544 | pure `main` | **Confirmed**, but only Windows + AMD. [#726](https://github.com/boykopovar/AnyPS5/discussions/726) update of 2026-10-11: playable at 60 FPS on `main` `a11c8f7f`, no PRs, no patches, no env switches. Windows + NVIDIA ([#1996](https://github.com/boykopovar/AnyPS5/discussions/1996)) needs 6 open PRs **and** `APS5_STORE_AT_FLIP=1`. |
| Animal Well PPSA22520 | pure `main` | **Confirmed**. [#966](https://github.com/boykopovar/AnyPS5/discussions/966): 11 PRs, all merged; "Open blockers: None". |
| Balatro PPSA21402 | pure `main` | **Confirmed**, but by a different report than the bring-up thread. [#1142](https://github.com/boykopovar/AnyPS5/discussions/1142) itself needs #1020, #1038 and a local 2-line flip-depth patch. The pure-`main` result is the later comment on [#1034](https://github.com/boykopovar/AnyPS5/issues/1034): v01.015.000 on `main` `4a747c48`, full gameplay ~60 FPS, ~10 minutes played. |
| God of War: Sons of Sparta PPSA28997 | pure `main` | **Confirmed**. [#1742](https://github.com/boykopovar/AnyPS5/discussions/1742): "unmodified upstream main `b42ef24e`… No additional patches." Note the top post of that thread describes a **fork** result; stock `main` crashed at startup until two separate causes were found. |
| Wobbly Life PPSA29413 | pure `main` | **Partly.** `main` `2ccbd376f`, no local changes — but the result is "playable into the tutorial", and the open-world tutorial runs at ~1 FPS and aborts deterministically (4/4 runs). I downgraded it from Playable to In game. |
| Hot Shots Golf 2 PPSA06699 | pure `main` | **Ambiguous — please check.** [#3839](https://github.com/boykopovar/AnyPS5/discussions/3839) says "In game, playable" and that on plain `main` the course "dips to roughly 45-49 FPS", which implies `main` runs it. But the same post lists #3636 (1_5_5_5 UNORM color targets), #3696 and #3697 as open PRs under "Pull requests / Fixes", with no statement of what breaks without them. I wrote the Build cell as "`main`; open PRs for correctness and for 60 FPS". If you can confirm it renders correctly on plain `main`, this becomes a clean pure-`main` row. |
| Hades II PPSA36082 | pure `main` | **Wrong.** [#1965](https://github.com/boykopovar/AnyPS5/discussions/1965) lists #419, #2121, #2132, #2169 (or a fork branch) for rendering in gameplay, #3301 for menu audio, #1810/#3231/#3237 plus local rebases of #494/#1193/#1201 for speed, and either #290 or `APS5_HOST_IMPORT_MIB=8192` to boot. All open. It also only boots with warm shader caches. |

Two further titles are on stock `main` that the list did not mention:

- **Dreaming Sarah PPSA02929** — release v0.1.1 libraries and relinker ([#3003](https://github.com/boykopovar/AnyPS5/discussions/3003)).
- **One Night Stand PPSA03030** — `main` `ccf9ff90` ([#3356](https://github.com/boykopovar/AnyPS5/issues/3356)); see the caveat below.

## Thin or ambiguous evidence

Kept in the table, but flagged:

- **One Night Stand PPSA03030.** Single comment, posted inside an unrelated issue thread
  ([#3356](https://github.com/boykopovar/AnyPS5/issues/3356), which is the Road of Death
  import issue), AI-assisted, with no gameplay action described: it says the game "starts,
  creates the window, loads its shaders and runs correctly" with "no crash, failed
  presentation or abort", and reports "49 to 60 FPS stable most of the time". No menu,
  level, input or save is named, and the performance line ("Loading visuals and game
  11fps, roughly 39 Drops") is hard to read. Host and commit are specific, so I kept it,
  but a reviewer may prefer to drop it or to ask for a second report. **It is the weakest
  row in the table.**
- **My Friend Peppa Pig PPSA03706.** The only evidence is a *bug* report about an
  intermittent `abort()` on window close ([#3506](https://github.com/boykopovar/AnyPS5/issues/3506)).
  Gameplay is mentioned only in passing: "during gameplay at about 30 fps". There is no
  bring-up thread for this title. The build is a fork test branch
  (`santhoshkumar-dev/AnyPS5` `test/pr419-pr2169-ppsa03706`), and on `main` the title
  "stops earlier on MSAA".
- **3DMiniGolf PPSA03647.** The reporter completed the first hole but writes "This is
  still short of a fully playable compatibility result", and that the local integration
  "includes an earlier sampler revision. Its in-game results do not establish
  compatibility with the current head of #2092 or stock `main`." Row kept at "In game",
  not Playable.
- **Road of Death PPSA07286.** "The game ran perfectly on screen in my test" is one
  sentence inside a comment on an AGC register issue
  ([#3163](https://github.com/boykopovar/AnyPS5/issues/3163)); the fix was a fork branch
  carried as #3330, plus #846 (now merged) and two local libSceHttp stubs. No frame rate,
  no screenshots. The log detail (room changes, music streaming, save data read and
  written) is what makes it credible.
- **Yamagi Quake II PPSA99007.** Also one comment, on a shader-semantics issue
  ([#1413](https://github.com/boykopovar/AnyPS5/issues/1413)), not a bring-up thread. The
  two changes it needed are local, one of which #4030 addresses for the P10 case only. It
  is homebrew (catalog v0.2.0-alpha.1) on an MX450, 1140+ frames at about 30 FPS. Solid
  but single-source.
- **Grand Theft Auto V PPSA04264.** The playable prologue is on
  `MichonGoddijn231849/AnyPS5-fork` `gta-v/testing`, based on `main` from **before** #639,
  i.e. a stale base. On current `main` plus the open PRs the title "presents its first
  frames (the start screens), then stops on a T# read from a material table". Only the
  prologue in the bank was tested. I marked it Playable because the fork report gives
  27-28 FPS, but this is a fork-only result on an old base.
- **Astro's Playroom PPSA01325.** The gameplay claim is one line at the end of the top
  post ("Furthest now: the first level, `cpuplaza_mini_04`, with Astro answering Cross
  (jump) and Square (punch), at 4 to 11 FPS"). The rest of the thread is stop-by-stop
  bring-up. Needs #3669, #425, #3750, #3878 and #3933, all open at the time of writing.

## Conflicting reports

- **God of War: Sons of Sparta PPSA28997.** Four reporters disagree:
  "Fully Playable" in the top post (on a fork); "playable apart from noticeable
  frame-rate drops" on unmodified `main` (RX 6800 XT); "running stably… not getting a
  consistent 60 FPS, occasional stutters and lags" (RX 6700 XT); "10-12 FPS on just the
  menu" (RTX 4070 SUPER, gameplay not reported); and a user who got 5 crashes in 2 hours.
  No frame times were measured in any of them. I wrote the row as Playable with the
  drops and the crash report in the cell, and expanded on it in the prose. If you want
  the table stricter, this one belongs at "In game".
- **Disney Epic Mickey: Rebrushed PPSA15657.** The top post of
  [#2255](https://github.com/boykopovar/AnyPS5/discussions/2255) still says "Reaches the
  first level and gameplay on Linux… gameplay starts with Mickey in the third-person
  camera", and a mid-thread update repeats it. The reporter then **retracted** both: the
  images and clips were the pre-rendered Bink intro movie `dbc_visit1_intro_newgame.bk2`
  with the HUD drawn on top, and the earlier frame-rate numbers were measured over that
  movie. With the movie skipped, the level loads and the game logic runs but Unreal never
  draws the 3D view (~19 draws per frame, no scene or base pass). I used the retraction.
  **The thread's own top post is stale** — worth asking the reporter to update it.
- **Hollow Knight: Silksong PPSA12544.** [#3731](https://github.com/boykopovar/AnyPS5/issues/3731)
  is titled "runs at 60 FPS on Linux/NVIDIA" and claims no engine modifications are
  required, but its own text says "running to the live interactive Main Menu" — not
  gameplay — and it needed two binary patches to the title's own module
  (`Il2cppUserAssemblies.prx.guest.prx`). A maintainer closed it: "this is not a bug
  report. Both changes are patches to the title's own modules, which the project does not
  carry." I recorded the Linux cell as main menu only.
- **Sonic Origins Plus PPSA05324.** The top post of
  [#2021](https://github.com/boykopovar/AnyPS5/discussions/2021) stops at the SEGA logo;
  gameplay is only in "Update 5", which also needed a **root-only host kernel parameter**
  (`ttm.pages_limit=2621440` to raise RADV's GTT heap to 10 GiB) and a 960x540 render
  size. Four of its speed changes are fork branches that were never PRs, because PR intake
  was paused. Playable is correct for Sonic 1 at 51-60 FPS, but it is not reproducible
  without those host settings.
- **Demon's Souls PPSA01341 on current `main`.** One reply reported a 14 s crash as a
  `main` regression, then corrected it: the difference was `ANYPS5_ENABLE_SPIRV_TOOLS=ON`
  vs `OFF`. With ON, `main` + #861 reaches 87 s and stops on the #1921 SPIR-V validation
  failure. I used the correction.
- **Dreaming Sarah PPSA02929 on AMD.** Playable on a GTX 1050 Ti and Intel HD 620, but it
  does not start at all on an RX 6700 XT (crash inside `amdvlk64.dll`). The existing
  per-hardware table said nothing about AMD; I added this as a prose bullet rather than a
  column, since it is a host-driver bug, not a title result.

## Titles in the task list I could **not** substantiate as gameplay

Only one:

- **PPSA01342 (Demon's Souls, second region)** — menu only. Not in the table. The task
  already flagged this, and the evidence agrees: character creation and levels are not
  reached, and "New Game" to "Continue Offline" hangs.

Every other ID in the list has at least one report that names a level, a room, a hole, a
map or player input. The weakest are PPSA03030 and PPSA03706, listed above.

## Structural choices

- The original table's two per-hardware columns (GTX 1050 Ti / i5-7500, Intel HD 620 /
  i5-7200) do not scale to 29 rows — almost every row is a different host. I kept them as
  a separate small "measured frame rates" table under the main one, so the original
  convention survives and the two original data points are not lost. If you would rather
  not have two tables, the alternative is to move those two numbers into the Dreaming
  Sarah prose bullet and drop the second table.
- `Windows` and `Linux` columns are kept, and now hold the furthest state on that OS
  rather than a yes/no. `?` keeps the original doc's meaning of "not tested".
- I added `Status`, `Build` and `Evidence` columns. `Build` is the fourth requirement
  ("stock `main` vs open PRs vs fork vs local patches") and is the column most likely to
  go stale, since PRs get merged — it is worth a line in the doc telling contributors to
  re-check it, which I did not add.
- No image files are referenced. The only image-adjacent link is the existing
  `In-game screenshots` link to a PR comment, which is a link, not a file in the repo, and
  matches `docs/dev/CONVENTIONS.md`.
- PR and issue numbers whose kind I could not verify from the scrape (#3149, #3330, #4076,
  #3854, #2092) were either left unlinked or linked as `/pull/N`; GitHub redirects between
  `/pull/N` and `/issues/N`, so the links resolve either way, but #3149's link is the one
  worth spot-checking.

## Evidence used

- `D:\AnyPS5\scratchpad\discussions.json`, `issues.json`, `i2523_extra.json`,
  `evidence.json` from the earlier scrape. No re-scraping was needed.
- Several bring-up threads have updates **after** the window `classify.py` captured, so I
  re-read the full thread bodies and comments for every row (`raw.py`, `raw2.py`,
  `dump2.py`, `dump3.py` in the same directory). PPSA05324 is the clearest example: the
  classifier's windows stop at the SEGA logo, while update 5 of the same thread has Sonic 1
  at full speed.
