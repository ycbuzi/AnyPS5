#!/bin/bash
replay=/root/cbint/build-full/core/libs/prx/libSceAgcDriver/agc_shader_replay
work=/mnt/c/AnyPS5-work/xbox-probe/hlsl51/runs/$1
shift
tmp=/root/xbox-retest
rm -rf "$tmp"; mkdir -p "$tmp"
: > "$work/retest.txt"
fixed=0; still=0
while read -r name _; do
    for src in "$@"; do
        [ -f "$src/$name" ] || continue
        if (cd "$tmp" && timeout 60 "$replay" --spv --xbox "$src/$name" > "$tmp/$name.out" 2>&1); then
            fixed=$((fixed + 1))
        else
            still=$((still + 1))
            echo "$name $(grep -m1 -iE 'error|fail|not implemented|throw|unsupported|require' "$tmp/$name.out" | cut -c1-200)" >> "$work/retest.txt"
        fi
        break
    done
done < "$work/replay-failed.txt"
echo "retest: now ok $fixed, still failing $still"
cut -d' ' -f2- "$work/retest.txt" | sed -E 's/[0-9]+/N/g' | sort | uniq -c | sort -rn | head -12
