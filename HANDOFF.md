# Handoff: AnyPS5 work between the PC session and the cloud session

Last updated: 2026-10-10 ~10:10 UTC, by the cloud session.

All issue and PR numbers in this file are on **boykopovar/AnyPS5** (upstream) unless marked
otherwise. Bare `#1234` links in this repo point at the fork, where those numbers don't exist.

## If the cloud session died: resume here (written 10:15 UTC, 2026-10-10)

Everything is on `ycbuzi/AnyPS5`:
- `claude/stoic-turing-wc7bje`: the stack (2960/3350 before the agent work) plus the 2631 fix.
- `agent/usbd`: PRs 2224+2228 over a libusb submodule (`3rdparty/libusb`, libusb-cmake
  `c8477c10`). Verified by the agent on Linux: builds, 3/3 usbd tests pass, conventions pass,
  libSceUsbd 5 -> 29 done. Base `6cc335833` (older than the stack).
- `agent/ssl`: PR 1884 over an mbedtls submodule (`3rdparty/mbedtls` 3.6.7 `068ff080`, needs its
  nested `framework` submodule at configure). Verified on Linux: 2/2 ssl tests, conventions pass,
  libSceSsl 5 -> 15 done. Base `6cc335833`.
- `agent/pr2503`, `agent/posix`, `agent/kernel-raise`: were still running at 10:15 UTC in
  worktrees under `/home/user/AnyPS5/.claude/worktrees/` in the cloud container. If the container
  is gone, their work is lost unless it was pushed; check `git ls-remote --heads origin 'agent/*'`.

Next steps, in order:
1. `git merge --no-ff agent/usbd` and `agent/ssl` into the stack (expect changelog conflicts in
   `docs/dev/TechnicalDebt.md`; keep both sides), `git submodule update --init 3rdparty/libusb
   3rdparty/mbedtls` (plus mbedtls's nested `framework`), build, run the suite, measure.
2. On the PC: Windows build + full suite + GPU matrix of the result; run ps5-opengl (PPSA99005)
   for the 2631 fix; test 2640's branch; reproduce 2652 with DOOM.
3. Only then consider posting anything, re-checking each thread first.

## How the PC session picks this up

```
cd D:\AnyPS5
git fetch https://github.com/ycbuzi/AnyPS5 claude/stoic-turing-wc7bje
git show FETCH_HEAD:HANDOFF.md
```

`D:\AnyPS5`'s `origin` is upstream, so fetch the fork by URL. Only one side works on the branch at
a time: the PC pulls this branch before doing new work, and pushes back before handing off again.

## The goals (the user's own words)

1. **"Get as far ahead in this project and do more than what they already have."** Our own build
   ahead of upstream's completion %, by stacking other people's unmerged PRs: *"I'm sure they have
   the stuff that we're missing if they give the code to us to fix and then we can use it too."*
2. **Bigger picture:** build an app on top of AnyPS5 that runs PS5 system calls and games, with
   *"the exact layout the exact information of ps5 would give off when booting."* Not Remote Play,
   not a replica UI.
3. **Later:** FragPunk.
4. **Contributing upstream:** the intake bot closes PRs from anyone with fewer than 20 merged PRs,
   so contributions go through issue comments and Discussions, posted from the PC as `ycbuzi`.

## How we raise the % (the harvesting plan)

Every contributor's finished work is a candidate for our stack, wherever it lives:
- **Open PRs** upstream (184 open at 07:55 UTC).
- **PRs the intake bot closed** (label `intake paused`): closed by policy, not on quality. 126 at
  ~08:07 UTC, growing by about one every 7 minutes.
- **Fix branches posted in issue comments**, because people now skip the PR entirely: elsi06
  (2599), SCD28 (2582), AlxcBTW (2597), SleglJan/AnyPS5 PRs 17-21.
- **Discussions.**
- **Other people's forks**: branches never posted anywhere. As far as the records show, not
  searched yet.

A candidate goes into the stack only if:
1. A maintainer didn't reject it on merit.
2. It merges cleanly, or the conflict is trivial (for example only the `docs/dev/TechnicalDebt.md`
   changelog).
3. It adds more implemented functions than it declares, otherwise the % drops.
4. Its functions actually do something. `progress.py` counts any function without the
   not-implemented marker, so one that only returns an error still counts.
5. The stack still builds and the tests still pass afterwards.

Measure the gain on our stack, not on bare upstream: several candidates overlap work we already
carry. Upstream also absorbs some of this work over time (oneandonlydean's `fix/carry-NNNN` PRs
merge other people's fixes), so our lead shrinks unless we keep harvesting.

## Rules the user set

- **Re-check right before posting**: the thread's comments, the merge state of every PR the post
  relies on, and current `main`. A comment on 2581 had to be deleted because a PR it argued against
  had merged 9 minutes earlier.
- Nothing gets posted without the user's OK, and it must be verified as truthful first.
- Hold the list of 46 missing AGC functions until upstream intake reopens.
- Write references in full (`boykopovar/AnyPS5#2525`).
- Say "I'm guessing" when guessing; never present a guess as fact.
- No redundant or duplicate claims, and no false work or information: if someone already said
  it, don't post it; label how each claim was verified (run, read in code, or not verified).
- Replies are time-sensitive (threads move within minutes): prepare them immediately rather than
  queueing them for later.

## Where things stand

| Branch on ycbuzi/AnyPS5      | What it is |
|------------------------------|------------|
| `stack/open-prs`             | `70333e41`, the PC's stack as pushed (42 commits on top of upstream `4e9d298e`). Untouched. |
| `claude/stoic-turing-wc7bje` | That stack, synced with upstream three times (latest `3afc78464`, merge `9a6e26fe2`), plus 58 harvested PRs (cycles 1-2) and the fix commits listed under cycle 2. |

Completion, measured in the cloud with `python3 tools/progress.py --root <tree> <out-dir>`:

| Tree                                     | Implemented | Total | %      |
|------------------------------------------|-------------|-------|--------|
| Upstream `main`, `3afc78464`             | 2690        | 3138  | 85.72% |
| Ours after the first upstream sync       | 2733        | 3151  | 86.73% |
| Ours after cycle 1 (49 PRs)              | 2886        | 3260  | 88.53% |
| Ours after cycle 2, `9a6e26fe2`          | 2922        | 3298  | 88.60% |
| Ours after cycle 3, `4d9cdc4ae`          | 2953        | 3300  | 89.48% |
| Upstream `main`, `be403084c` (09:50 UTC) | 2697        | 3190  | 84.55% |
| **Ours now (cycle 4), `d43f3e2bc`**      | **2960**    | **3350** | **88.36%** |

Lead over upstream: **+263 implemented functions, +3.81 points.** 390 functions remain. Both
percentages dropped at cycle 4 because upstream merged PR 407, which declares 52 new stubs.

Build and test status:
- Last full Windows suite: **660/660 passed**, on the 29-commit stack (`c44602f8`, base `4e9d298e`).
- After that: **not yet built on Windows.** The 217 `agc_driver_*` GPU tests can only run on the
  PC's three GPUs (RTX 4070 SUPER, GTX 1650 SUPER, Radeon 610M); the cloud machine has no GPU.
- Linux full builds in the cloud (`handoff-tools/cloud/full-build.sh`): build 1 of the cycle 1 tip
  failed on duplicate definitions (fixed in cycle 2); build 3 failed only on the
  `ShaderDiskCache.cpp` size assertion (fixed by the `3afc78464` merge); **build 4 of `9a6e26fe2`
  passed: 1515/1515 targets, 0 failures** (Linux, GCC 13, Release). The CPU-side suite
  (`ctest -E '^agc_|video_out'`) was started on it; result in the log at the end of this file.
- Build 5 (cycle 3 tip `be4d34eeb`) failed at configure: the 2012 merge had created
  `libSceVoice/CMakeLists.txt` on both sides and the resolver kept both copies (two
  `add_library(libSceVoice)`). Fixed in `4d9cdc4ae`, together with a constant the 2503 slice
  re-declared in `libSceShare.native/Export.cpp`. Build 6 of `4d9cdc4ae`: see the log at the end.
- Nothing after `9a6e26fe2` has been built on Windows or tested on a GPU.

### Cycle 1 (cloud, ~08:50-09:12 UTC)

1. Synced with upstream `412fa8b0` (+55 commits). Conflicts:
   - `core/libs/tests/GuestFont.cpp`: upstream merged its own fix for 2582 (as 2614), which our
     `977dcee3` also carried. Took upstream's file and re-applied only PR 2497's additions.
   - `core/libs/tests/GuestSystemExit.cpp`: kept both sides (stack's LoadExec restart helpers and
     upstream's `_Exit` detach helper).
   - No tests or exports became duplicated (`handoff-tools/cloud/dupcheck.py`).
2. Fetched all 2370 upstream PR heads; 1063 are not merged. Trial-merged each into our tree
   (`handoff-tools/cloud/harvest.py`, results in `harvest-2026-10-10.tsv`).
3. 105 candidates raised the count with no hollow functions. Read the bodies of the biggest ones and
   checked close reasons on GitHub for the top ten:
   - Most were closed by a maintainer with *"closing until a title is shown to reach these on
     main"*: need-gated, not rejected on quality. Harvested.
   - Excluded the Usbd group (1743, 1880, 2224, 2228): 1743's design was rejected ("does not take a
     runtime-loaded host libusb fallback") and the group adds a libusb dependency.
4. Stacked one at a time, keeping a PR only if the % rose (`handoff-tools/cloud/stack.py`):
   51 applied, 38 conflicted, 11 no gain, 1 already present. Then dropped two that cannot compile:
   - 2482 (`scePthreadCancel` uses `thread->cancelPending`, which doesn't exist).
   - 2533 (adds `libSceFont/src/Font.cpp` including a `Font.h` that doesn't exist).

Stacked in cycle 1 (49): 867, 967, 989, 1008, 1052, 1169, 1212, 1390, 1527, 1554, 1567, 1574, 1582, 1594, 1637, 1638, 1679, 1681, 1687, 1721, 1724, 1725, 1765, 1767, 1768, 1770, 1773, 1841, 1852, 1875, 1879, 1922, 1982, 1983, 1984, 1998, 2017, 2037, 2038, 2166, 2187, 2213, 2217, 2234, 2271, 2277, 2306, 2308, 2358. (1879 was reverted again in cycle 2.)

### Cycle 2 (cloud, ~09:12-09:35 UTC)

1. Re-ran the 38 cycle 1 candidates that conflicted with `handoff-tools/cloud/stack2.py`, which
   resolves purely additive conflicts and rejects duplicate definitions, duplicate tests/exports and
   syntax breaks. 10 applied: 1806, 1814, 2058, 2167, 2173, 2216, 2235, 2280, 2309, 2604.
2. Build 1 of the cycle 1 tip failed: `sceFontDefineAttribute`/`sceFontGetAttribute` defined twice
   in `libSceFont/src/Style.cpp` (PR 1879 against the base's font work) and eight libc functions
   defined in two files by overlapping PRs. Reverted 1879's merge (`b1383ca1f`) and removed one copy
   of each libc duplicate (`38869e765`: kept 2280's fmin/fmax/nextafter, which handle signed zeros
   and range errors; Math.cpp's ceilf/fabsf; Strings.cpp's wcscspn).
3. `stack2.py`'s first version de-duplicated code lines inside additive conflicts and dropped eight
   closing lines from `libc/CMakeLists.txt` (CMake parse error). Fixed the resolver, audited every
   additive merge (`dropcheck.py`: only that file was damaged), rebuilt it by replaying the merges
   (`replay_file.py`, `07c08db84`).
4. Build 3 failed only on `ShaderDiskCache.cpp`'s `sizeof(RecompileResult)` assertion: the stack
   carried PR 1776's LDS work (`e201003ca`) with upstream's older assertion. Synced with upstream
   `3afc78464` (+28 commits, `9a6e26fe2`), which merged 1776 and 1492 properly and also upstream's
   own version of our 2525 fix (`0e03ad0f9`); took upstream's side in the four relinker files.
5. Re-scored the 12 PR heads that changed upstream since cycle 1: nothing worth stacking (2633 adds
   one hollow function; 2634 replaces every stub in the tree at once).

### Cycle 3 (cloud, ~09:40-09:55 UTC)

Candidates: the PRs holding real implementations of what we lack (see "How far can this go").
- `stack2.py` run on 33 of them: only 1886 applied (+3); the rest conflict for real, 6 of them only
  in the changelog where both sides edited the same bullets.
- Taught the resolver to merge any changelog conflict (ours + the other side's new bullets):
  1583, 2012, 1689, 1985 applied (+10). 1141 and 1108 rejected: duplicate definitions.
- 1952 hand-merged (+10): kept our `libc/Export.cpp`, deleted the eight `_Cnd_*`/`_Mtx_*` stubs
  that its new `src/Threads.cpp` implements.
- 1981 and 1993 hand-merged the same way as 1952 (+3: `vsnprintf_s`, `vsscanf_s`, `_Iswctype`).
- 2503: only its libSceShare.native and libSceUlt changes applied, as a patch (+5); the rest
  still conflicts in libkernel, libc and the dialogs.
- Left out after inspection: 1884 (rewrites libSceSsl on mbedtls, a dependency the project does
  not have), 2393/1802/1431 (written against the pre-1988 thread model; need re-implementation),
  2503 (34 files, 10 real conflict hunks), 82 (needs `Ngs2WaveformFormat` from 1021, which
  conflicts), 850/852 (need FreeType headers to check here), 1616 (duplicates Stdio functions).

### Cycle 4 (cloud, 09:46-09:50 UTC)

Synced with upstream `be403084c` (+28 commits, merge `4e05bbaf6`). Upstream merged 1141
(pthread_cancel) and 407 (Deathloop loading, +52 stubs). Conflicts: `libc/Export.cpp` (took
upstream's side minus the `_Cnd_*`/`_Mtx_*` stubs our 1952 merge implements), `libSceJson2/Export.cpp`
(kept our `SpecialFloatFormatType` enum; then `d43f3e2bc` removed two placeholder stubs the merge
carried for functions the file already implements), changelog (upstream's bullets plus ours).
Build 7 of `d43f3e2bc`: see the log.

### Agents launched at 09:52 UTC (cloud), each in its own worktree and branch

The user asked for everything in "How far can this go" to be attempted, with subagents, truthfully.
Each branch is merged into `claude/stoic-turing-wc7bje` only after its report is checked and the
tree builds. Branches: `agent/usbd` (libSceUsbd over source-built libusb, PRs 2224+2228 or 1880),
`agent/ssl` (PR 1884 on mbedtls), `agent/pr2503` (the rest of 2503, file by file), `agent/posix`
(implement libkernel `madvise`/`select`/`shm_open`, libc `_WStoul`/`swscanf_s`, evaluate PR 1482),
`agent/kernel-raise` (port 2393's `sceKernelRaiseException` + coredump user data onto the current
thread model). Rules given to all: report only what was verified and how, no hollow
implementations, no pushes, build and test on Linux, keep PR authors' authorship.

### What the stack has that upstream doesn't

- Our own fixes:
  - `629affd4` (2525, repeated DT_NEEDED) and `977dcee3` (2582, guest_font isolation): upstream has
    since merged its own versions (`0e03ad0f9` and 2614), so the stack's copies are retired and
    those files match upstream.
- PRs stacked at the last step: 2394, 2557, 2446, 2518, 2543 (changelog conflict resolved by hand),
  2497 (include clash in `GuestFont.cpp` resolved by hand).
- Deliberately left out:
  - 2383, 2384: declare exports but implement none, which lowers the %.
  - 2493, 2494, 2454, 2399: counted as implemented, but the functions only return errors.
  - 2498, 2495, 2465, 1450: rejected by the maintainer on merit (no do-nothing stubs).
  - 2429, 2438: duplicates of 2394.
  - 2565: declares more functions than it implements.
  - 2435, 2562, 2441: conflict with work already in our stack.

## Issue work

- 2603 (R&C stack overflow since 1988): reply drafted at ~09:35 UTC showing Windows is not exposed
  (guest threads get the requested size as commit inside the 0x210000 reserve the relinker writes;
  `WindowsPeWriter.cpp:34`, `Thread.cpp:459`, Microsoft's Thread Stack Size docs). Handed to the user
  to post. Check the thread before doing more on it.
- 2631 (`v_fma_mix*` with f16 round-toward-zero): **fix written in the cloud at ~10:05 UTC** (see
  the branch): `FLOAT_MODE` f16 rounding toward zero is now translated (truncation of the
  round-to-odd product-sum in the f64 mix path with overflow stopping at 65504, the bit-exact
  toward-zero pack for f32-to-f16 results, a round-to-odd intermediate for f16 add/sub), with a
  CPU test `agc_float16_round_toward_zero`. Rounding toward an infinity still throws. Not measured
  on hardware: the PC should run the GPU suite and, if possible, ps5-opengl (PPSA99005, homebrew)
  on the three GPUs before anything is posted. Nobody had replied on the thread as of 10:00 UTC.
- 2652 (SDK `libc.prx` exports collapse into one return-0 stub during guest module conversion):
  zero comments as of 10:10 UTC. Affects every catalog homebrew including DOOM (PPSA99666), so the
  PC can reproduce it directly. Needs a relinker design decision (bind placeholder exports to our
  libc by NID, or a throwing stub per NID); not started.
- 2648 (Palworld: `sceAudioOut2EnableChat` and eight kqueue-style kernel event exports absent):
  not declared in our tree at all (so not in the 390). Candidates for the "write it" track.
- 2640 (Windows: executable directory outside the ANSI code page): reporter's fix branch
  `justdoston/AnyPS5` `fix/windows-unicode-executable-path`, Wine-tested only. PC job: test on
  real Windows (a `ü日` directory); harvest candidate.
- 2642 (structurizer never terminates on a two-latch loop): design question with a full repro;
  nothing to add.
- 2621 (R&C `DB_SHADER_CONTROL`): skipped, a maintainer design question that needs the title.
- 2601 (unlink on read-only files, Windows): handled upstream in the thread. MarcZX100's rebased
  branch `codex/fix-unlink-readonly-2601` (`505fe780b`) is a harvest candidate.

## How far can this go (measured 2026-10-10 ~09:40 UTC)

`tools/progress.py` counts a function as implemented as soon as it no longer calls
`NotImplemented_nid_no_patch`, so 100% is reachable any time by making every stub return an error.
That is the hollow work this stack refuses, and what upstream rejects on sight (1498 and 2193 were
closed for exactly that: "+100 count is not backed by new behaviour").

For the 376 functions still missing at `9a6e26fe2`, `handoff-tools/cloud/coverage.py` +
`coverage2.py` checked every unmerged upstream PR (mass-stub and merit-rejected PRs excluded, and a
call to an `Unsupported*`/`NotImplemented*` helper counted as hollow):

| Remaining functions | Count |
|---|---|
| Real implementation in some non-rejected unmerged PR | 66 |
| Only hollow implementations exist | 51 |
| Nothing on GitHub | 259 |

Ceiling from harvesting alone: **2988/3298 = 90.6%** (slightly less: 492 and 2499, both rejected on
quality, hold a few of the 66). Beyond that, the code has to be written. The biggest blocks with
nothing available: libSceHmd2 (16, VR headset), libSceAgc (18), libScePsml_debug (10),
libkernel (12), TextToSpeech2, CdlgG2P, VrTracker2, WebAppLauncher (7 each).

PRs holding the 66, with the close reason checked on GitHub:
- 2503 (19: Share, Ult, WebBrowserDialog, libc `_Cnd`/`_Mtx`): closed by the intake bot only, but
  34 files and 10 real conflict hunks against our stack in libkernel/libc. Needs a careful hand
  merge; tried and backed out in cycle 2.
- 1952 (8 libc `_Cnd`/`_Mtx`, need-gated), 1884 (7 Ssl, author-closed, never reviewed), 2393 (3
  libkernel, bot-closed), 1583, 2562, 1141 + 1739 (pthread_cancel; 1739 is open and waits for 1141),
  2012, 1689/953, 82/1021, 1981, 1064, 1672, 1985 and other singles: all conflict with the stack in
  code or in the changelog (cycle 3 run, `handoff-tools/cloud/stack-run3-2026-10-10.txt`).
- 971/1055/1885/2435 (4 Agc marker-span builders): closed because no title calls them and the packet
  layout is guessed; GPU work, left out.
- 492 (10 libc) and 2499 (4): rejected on quality; left out. 1879: reverted (duplicates), left out.

Our lead is other contributors' work assembled, not ours: do not report it upstream as a
completion claim. Upstream's lower number is a policy (merge only what a title needs), not a lag.

**Audit of our own lead (the check the PC session said it owed, done 2026-10-10 ~10:00 UTC):** of
the 255 functions our tree implements and upstream doesn't, 15 are constant or error returns by the
same test applied to everyone else's PRs: libSceSystemGesture (5), libSceAgc
`sceAgcDcbEndOcclusionQueryGetSize` and `sceAgcDcbSetIndexIndirectArgsGetSize` (both `return 16`,
which is the correct answer for a size query), libSceNpManager (2), and one each in NpAuth
(`SIGNED_OUT` after argument checks), NpTrophy2, PlayerSelectionDialog, RazorCpu, SigninDialog and
VideoRecordingP. So the honest count of behaviour-adding functions in the lead is about 240.

## Open work

Cloud can do (Linux, no GPU):
1. Cycle 3 continued: hand-merge 1884, 1952, 2393 and the changelog-only conflicts; 2503 when
   there is time.
2. Build the relinker only (`-DANYPS5_RELINKER_ONLY=ON`) and run its tests on Linux.
3. Issue 2599, fSELF-wrapped modules silently skipped: elsi06 has a fix (their fork, branch
   `fix/relinker-self-guest-modules`, commit `bc444a67`; PR 2605 auto-closed), tested on macOS
   only. Check whether it also covers the third silent skip at `GuestModuleBuilder.cpp` line 158
   (upstream `4fbf6ca5`), then test it on Linux.
4. Issue 2598, Palworld stops on `sceSaveDataCancel`: check whether the export exists anywhere.
5. Check the stack's own earlier functions for "counts as implemented but does nothing".
   The PC session said this was owed.
6. Cycle 2 harvesting: the 38 cycle 1 candidates that conflicted (see
   `handoff-tools/cloud/stack-run1-2026-10-10.txt`), PRs opened after 2632, fix branches posted in
   issue comments (MarcZX100 for 2601, elsi06 for 2599), Discussions, other forks.
   Older notes: more harvesting from every source above. Known leftovers: 1611, 1097, 1141, 1431, 1091, 1772
   are not merged upstream as of `4fbf6ca5`.
   Not yet checked whether the stack already carries them. SleglJan/AnyPS5 PRs 17-21 were not
   applied as of last night. New intake-closed PRs since ~08:07 UTC are unmeasured. Skip 1492 (1252 lines). 1909, 2123 and 1686 are now upstream.
7. Maybe a full Linux build: needs extra libraries, and this machine has 4 cores.

PC only (Windows, GPUs, posting):
- Full Windows build and suite of the latest branch.
- DOOM: with a warm shader cache it still falls back to the CPU scaler (3 of 3 runs), which
  contradicts babyhuey on PR 690. The PC was relinking DOOM with a newer build to re-test when the
  limit hit. Our PR 690 comment still says the warm-cache re-test wasn't done; update it only after
  re-testing, and with the user's OK.
- PR 2468, our relinker test-temp fix: closed by the intake bot after Kikoarl's Linux "LGTM!".
  It is in neither upstream `main` nor our stack.
- Anything posted upstream.

## PC layout

- `D:\AnyPS5`: main clone (`origin` = upstream, partial clone). Builds in `build` and
  `build-relinker`; DOOM runs from `build-relinker\doom-run`.
- `D:\AnyPS5-tools`: `env.ps1` (MinGW GCC 15.2.0, CMake 4.3.1, Ninja 1.13.2) and `anyps5-fonts`
  (what `ANYPS5_SYSTEM_FONTS` points at).
- `D:\AnyPS5-games`: DOOM download `PPSA99666\eboot.bin` and unwrapped `doom-src\eboot.elf`.
- `D:\AnyPS5-wt`, `D:\AnyPS5-closed`: extra git worktrees for measuring upstream and the closed
  PRs. `D:\AnyPS5-measure`: purpose not recorded.
- PC notes file: `C:\Users\Blu66\.claude\projects\C--\memory\anyps5-project.md`.

## Log

- 2026-10-10 ~08:30-09:00 UTC (cloud): picked up from the PC session, merged upstream `6cc33583`,
  measured 86.76% vs 85.77%, wrote this file and the harvesting plan.
- 2026-10-10 ~08:50-09:12 UTC (cloud): cycle 1. Synced upstream `412fa8b0`, scored all 1063
  unmerged upstream PRs, stacked 49 (86.73% -> 88.53%), dropped 2482 and 2533 (won't compile),
  recovered the PC session's tools into `handoff-tools/`, drafted the 2603 reply.
- 2026-10-10 ~09:12-09:35 UTC (cloud): cycle 2. Stacked 10 more, fixed the duplicate definitions
  and the CMake damage, reverted 1879, synced upstream `3afc78464` (now 2922/3298 = 88.60% vs
  upstream 85.72%), measured the GitHub ceiling for the remaining functions.
- 2026-10-10 09:38 UTC (cloud): **build 4 of `9a6e26fe2` passed on Linux (1515/1515 targets)**
  and its CPU-side suite passed, **398/398** (`ctest -E '^agc_|video_out'`).
- 2026-10-10 09:33-09:42 UTC (cloud): cycle 3. 1886, 1583, 2012, 1689, 1985 stacked, 1952
  hand-merged (2945/3300 = 89.24%). Build 5 of that tip failed at configure (duplicate
  libSceVoice target).
- 2026-10-10 09:42-09:46 UTC (cloud): 1981 and 1993 hand-merged, the Share/Ult slice of 2503
  applied, the duplicate target and constant fixed (`4d9cdc4ae`, 2953/3300 = 89.48%).
  **Build 6 of `4d9cdc4ae` passed (825 targets rebuilt, 0 failures) and its suite passed 402/402.**
  (Times earlier in this file that read ~10:00 or later were estimates and are 15 minutes ahead of
  these commit times.)
- 2026-10-10 09:46-09:52 UTC (cloud): cycle 4 upstream sync (`4e05bbaf6`, fix `d43f3e2bc`;
  2960/3350 = 88.36% vs upstream 84.55%), build 7 started, five agents launched (see above).
