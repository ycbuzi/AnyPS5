#!/bin/bash
set -e
src=${1:-"/mnt/d/AnyPS5 Games/Marvel's Spider-Man Remastered (PPSA01467)"}
limit=${2:-150}
name=${3:-spiderman}
here=/mnt/c/AnyPS5-work/xbox-probe/hlsl51
work=$here/runs/$name
SIZES=1 bash $here/single-lane.sh "$src" "$limit"
rm -rf "$work"
mkdir -p "$work"
cp /root/xbox-single-lane/1/*.spv "$work/" 2>/dev/null || true
cp /root/xbox-single-lane/failed.txt "$work/replay-failed.txt" 2>/dev/null || true
cd "$here"
python.exe pushfix.py "runs/$name" "runs/$name/push"
python.exe dxbc.py "runs/$name/push" "runs/$name/dxbc" | tee "runs/$name/dxbc.txt"
for adapter in 0 1 2; do ./bin/psocheck.exe "runs\\$name\\dxbc" $adapter | tail -2; done | tee "runs/$name/pso.txt"
