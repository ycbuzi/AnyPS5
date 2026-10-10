# Handoff tools

Scripts used to harvest upstream work into this branch. Not part of AnyPS5 itself.

## pc-session/ (recovered from the PC session's transcripts, 2026-10-10)

Hard-coded for the PC (`D:\AnyPS5`, the PC session's scratchpad path). Recovered verbatim from the
last version written in the transcript; edits made after that version are not included.

| File | What it does |
|---|---|
| `stack-prs.sh` | Merge a list of upstream PR heads onto the stack, skipping conflicts; tags `stack-prestack` for rollback. |
| `stack-prs.ps1` | Earlier PowerShell version of the same. |
| `unself.py` | Rebuild an ELF from a fake-signed SELF container (used for ps5-doom's `eboot.bin`). |
| `unself.ps1` | Earlier PowerShell attempt; superseded by `unself.py`. |
| `verify-rebase.ps1` | Windows build plus test run after a rebase. |
| `verify-scd28.ps1` | A/B test of a font-test fix with real system fonts installed. |
| `gpu-matrix.ps1` | Run the GPU test set on each of the PC's three GPUs. |

Not recovered: `resolve-debt.py` (merges `docs/dev/TechnicalDebt.md` changelog conflicts) and
`finish-rebase.sh`. They appear only after 07:39 UTC, so they were probably written in the hour that
is in no session record. `cloud/stack.py` does the changelog merge itself.

## cloud/ (written in the cloud session, Linux)

Paths point at the cloud session's scratchpad; adjust before reuse.

| File | What it does |
|---|---|
| `harvest.py` | Trial-merge every unmerged upstream PR head (`refs/pr/N`, fetched with `git fetch upstream '+refs/pull/N/head:refs/pr/N'`) into a scratch worktree and record the change in implemented/total for the libraries it touches, using `tools/progress.py`'s own scanner. |
| `hollow.py` | Flag newly "implemented" functions whose body only returns a constant/error or throws. A heuristic: read the bodies before trusting it. |
| `stack.py` | Stack PR heads one at a time, keeping a PR only if the % rises on our tree; changelog-only conflicts are resolved by keeping both sides. |
| `stack2.py` | `stack.py` plus: resolves purely additive conflicts in any file by keeping both sides verbatim (changelog bullets are de-duplicated), and rejects a PR that creates a duplicate definition, test or export, or breaks `g++ -fsyntax-only`. Its first version de-duplicated code lines too and dropped eight closing lines from `libc/CMakeLists.txt`; `dropcheck.py` finds such drops and `replay_file.py` rebuilds a file by replaying its merges. |
| `libdups.py` | Functions defined more than once across a library's sources (link-time "multiple definition"), HEAD vs a base commit. |
| `coverage.py` | Which of our remaining unimplemented functions any unmerged upstream PR implements, and whether hollow. |
| `syncheck.sh` | `g++ -fsyntax-only` one file; compare HEAD against the pre-stack commit to find breaks a PR introduced. |
| `full-build.sh` | Full Linux build of a commit in its own worktree, collecting every failure. |
| `dupcheck.py` | After a merge, find tests or exports that became duplicated. |
| `extract_tools.py`, `conv.py`, `summaries.py` | Read another session's transcript pages: files it wrote, the conversation, its context summaries. |
| `harvest-2026-10-10.tsv` | Scores of all 1063 unmerged upstream PRs against `81af6c0a`. |
| `stack-run1-2026-10-10.txt` | Result of stacking the 101 candidates. |
