#!/bin/bash
replay=/root/cbint/build-full/core/libs/prx/libSceAgcDriver/agc_shader_replay
src=${1:-"/mnt/d/AnyPS5 Games/Marvel's Spider-Man Remastered (PPSA01467)"}
limit=${2:-120}
out=/root/xbox-single-lane
rm -rf "$out"; mkdir -p "$out/64" "$out/1"
n=0
for req in "$src"/*.req; do
    [ $n -ge "$limit" ] && break
    n=$((n + 1))
    for size in ${SIZES:-64 1}; do
        (cd "$out/$size" && timeout 60 "$replay" --spv --subgroup-size $size "$req" > /dev/null 2> "$(basename "$req").err" || echo "fail $size $(basename "$req")" >> ../failed.txt)
    done
done
echo "replayed $n requests"
for size in 64 1; do echo "size $size: $(ls "$out/$size"/*.spv 2>/dev/null | wc -l) spv"; done
