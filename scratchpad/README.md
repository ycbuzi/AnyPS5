# scratchpad — local tooling, not upstream's

Local-only helpers for the PC session. Nothing here is part of the AnyPS5 build; it exists so
the working tools survive between sessions instead of living in a temp directory.

| file | what it does |
|---|---|
| `orphans.py` | Finds `.cpp` files on disk that no CMake target claims — the dropped-source sync regression that has cost 13+ build cycles. Follows `include(...)` and matches on relative path, not basename. Run it after every sync, before the 20-minute build. |
| `triage.py` | Ranks all open upstream issues against what this machine can actually do (Windows, RTX 4070 SUPER + GTX 1650 SUPER + Radeon 610M, DualSense, DOOM; no Linux/macOS/Intel/AMDVLK/PSVR2/retail dumps). |
| `titledb.py` | Mines issue bodies for `PPSA` title IDs and the functions named near them, producing a demand ranking: which missing functions the most distinct real games actually need. |
| `fullrun.ps1` | Full build + suite with a **fresh shader cache directory**, which is mandatory — a stale AMD pipeline cache has fabricated mass GPU failures three times. |
| `all-issues.json` | Scrape of all 155 open issues, the input for `triage.py` and `titledb.py`. Re-scrape before trusting it. |

Usage:

    python scratchpad/orphans.py D:/AnyPS5
    python scratchpad/triage.py  scratchpad/all-issues.json
    python scratchpad/titledb.py scratchpad/all-issues.json
    powershell -ExecutionPolicy Bypass -File scratchpad/fullrun.ps1

A validated scanner is one you have run a **positive control** through: revert the bug it is
meant to catch and confirm it still fires. Three scans were discarded this session for
over-reporting. The linker and the full suite are ground truth.
