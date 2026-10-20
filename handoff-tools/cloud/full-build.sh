#!/usr/bin/env bash
# Full Linux build of a commit in its own worktree (keeps going past errors to collect all of them).
# usage: full-build.sh <commit>
set -u
SP=/tmp/claude-0/-home-user-AnyPS5/273e79d8-d6e6-502a-8da0-bb5a906a0d40/scratchpad
WT=$SP/build-wt
cd /home/user/AnyPS5 || exit 1
if [ ! -d "$WT" ]; then git worktree add -q --detach "$WT" "$1" || exit 1; fi
cd "$WT" && git checkout -q --detach "$1" || exit 1
echo "### commit: $(git log -1 --format='%h %s')  $(date -u +%T)"
echo "### submodules"; git submodule update --init --depth 1 --jobs 4 2>&1 | tail -3; echo "submodules exit: $?  $(date -u +%T)"
echo "### configure"; cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ > build-configure.log 2>&1; echo "configure exit: $?  $(date -u +%T)"; tail -5 build-configure.log
echo "### build"; ninja -C build -k 0 -j4 > build-ninja.log 2>&1; rc=$?; echo "build exit: $rc  $(date -u +%T)"
grep -E '^FAILED: ' build-ninja.log | head -40
echo "failed targets: $(grep -c '^FAILED: ' build-ninja.log)"
