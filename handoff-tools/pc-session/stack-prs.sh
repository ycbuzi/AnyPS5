#!/usr/bin/env bash
# Stack a list of PR heads onto stack/open-prs, skipping any that conflict.
#
#   bash stack-prs.sh 2497 2493 2498 ...
#
# Measures completion before and after so the real delta on top of OUR stack is
# known, which is not the same as each PR's delta against bare origin/main.
set -u
cd /d/AnyPS5 || exit 1
SCRATCH="C:\Users\Blu66\AppData\Local\Temp\claude\C--\4d4fd5e2-923f-4f26-b6f6-ee6d1f584e98\scratchpad"

if [ "$#" -eq 0 ]; then echo "usage: stack-prs.sh <pr> [pr...]"; exit 2; fi

if [ -n "$(git status --porcelain)" ]; then
    echo "ABORT: working tree is dirty"; git status --porcelain; exit 1
fi

START=$(git rev-parse HEAD)
echo "branch : $(git rev-parse --abbrev-ref HEAD)"
echo "HEAD   : $(git log --oneline -1)"
echo "backup : $(git tag -f stack-prestack "$START" >/dev/null 2>&1 && echo stack-prestack)"
echo

applied=(); conflicted=(); empty=(); fetchfail=()

for pr in "$@"; do
    if ! git fetch origin "pull/$pr/head:refs/stackpr/$pr" --force --quiet 2>/dev/null; then
        echo "  #$pr  FETCH-FAIL"; fetchfail+=("$pr"); continue
    fi
    before=$(git rev-parse HEAD)
    if git merge --no-ff --no-commit "refs/stackpr/$pr" >/dev/null 2>&1; then
        if git diff --cached --quiet && git diff --quiet; then
            echo "  #$pr  EMPTY (already present)"; git merge --abort 2>/dev/null; empty+=("$pr"); continue
        fi
        title=$(git log --format=%s -1 "refs/stackpr/$pr")
        git commit -q -m "Merge PR #$pr: $title

Co-Authored-By: Claude <noreply@anthropic.com>"
        if [ "$(git rev-parse HEAD)" != "$before" ]; then
            echo "  #$pr  APPLIED"; applied+=("$pr")
        else
            echo "  #$pr  NO-OP"; empty+=("$pr")
        fi
    else
        files=$(git diff --name-only --diff-filter=U | tr '\n' ' ')
        git merge --abort 2>/dev/null
        echo "  #$pr  CONFLICT : $files"; conflicted+=("$pr")
    fi
done

echo
echo "applied    (${#applied[@]}) : ${applied[*]:-none}"
echo "conflicted (${#conflicted[@]}) : ${conflicted[*]:-none}"
echo "empty      (${#empty[@]}) : ${empty[*]:-none}"
echo "fetch-fail (${#fetchfail[@]}) : ${fetchfail[*]:-none}"
echo
echo "HEAD now : $(git log --oneline -1)"
echo "ahead    : $(git rev-list --count origin/main..HEAD)"
echo "revert with: git reset --hard stack-prestack"
