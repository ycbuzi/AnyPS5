#!/bin/bash
replay=/root/cbint/build-full/core/libs/prx/libSceAgcDriver/agc_shader_replay
here=/mnt/c/AnyPS5-work/xbox-probe/hlsl51
name=${1:-all}
shift
work=$here/runs/$name
tmp=/root/xbox-replay/$name
rm -rf "$tmp" "$work"
mkdir -p "$tmp" "$work"
total=0
for src in "$@"; do
    for req in "$src"/*.req; do
        total=$((total + 1))
        (cd "$tmp" && timeout 60 "$replay" --spv --xbox "$req" > "$(basename "$req").err" 2>&1) || echo "$(basename "$req") $(grep -m1 -iE 'recompile failed|analysis failed|requires|error|throw' "$tmp/$(basename "$req").err" | cut -c1-200)" >> "$work/replay-failed.txt"
    done
done
cp "$tmp"/*.spv "$work/" 2>/dev/null
echo "requests $total, spv $(ls "$work"/*.spv 2>/dev/null | wc -l)" | tee "$work/replay.txt"
cut -d' ' -f2- "$work/replay-failed.txt" 2>/dev/null | sed -E 's/[0-9]+/N/g' | sort | uniq -c | sort -rn | head -12 | tee -a "$work/replay.txt"
cd "$here"
python.exe pushfix.py "runs/$name" "runs/$name/push"
python.exe dxbc.py "runs/$name/push" "runs/$name/dxbc" | tee "runs/$name/dxbc.txt"
for adapter in 0 1 2; do ./bin/psocheck.exe "runs\\$name\\dxbc" $adapter | grep -v "^fail" | tail -2; done | tee "runs/$name/pso.txt"
