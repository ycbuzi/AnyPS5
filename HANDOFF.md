# Handoff: AnyPS5 work between the PC session and the cloud session

Last updated: 2026-10-10 ~10:10 UTC, by the cloud session.

All issue and PR numbers in this file are on **boykopovar/AnyPS5** (upstream) unless marked
otherwise. Bare `#1234` links in this repo point at the fork, where those numbers don't exist.

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
| **Ours now, `9a6e26fe2` (cycles 1-2)**   | **2922**    | **3298** | **88.60%** |

Lead over upstream: **+232 implemented functions, +2.88 points.** 375 functions remain.

Build and test status:
- Last full Windows suite: **660/660 passed**, on the 29-commit stack (`c44602f8`, base `4e9d298e`).
- After that: **not yet built on Windows.** The 217 `agc_driver_*` GPU tests can only run on the
  PC's three GPUs (RTX 4070 SUPER, GTX 1650 SUPER, Radeon 610M); the cloud machine has no GPU.
- Linux full builds in the cloud (`handoff-tools/cloud/full-build.sh`): build 1 of the cycle 1 tip
  failed on duplicate definitions (fixed in cycle 2); build 3 failed only on the
  `ShaderDiskCache.cpp` size assertion (fixed by the `3afc78464` merge); build 4 of `9a6e26fe2`
  was running when this was written. The log at the end of this file has its result.

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
- 2631 (`v_fma_mix*` with f16 round-toward-zero): no reply (the reporter's analysis is complete).
  A fix is possible with the existing `PackFloat2x16Rtz` IR op, but needs GPU testing: PC job.
- 2621 (R&C `DB_SHADER_CONTROL`): skipped, a maintainer design question that needs the title.
- 2601 (unlink on read-only files, Windows): handled upstream in the thread. MarcZX100's rebased
  branch `codex/fix-unlink-readonly-2601` (`505fe780b`) is a harvest candidate.

## Open work

Cloud can do (Linux, no GPU):
1. Merge the 18 new upstream commits (probable `GuestFont.cpp` conflict, see above).
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
