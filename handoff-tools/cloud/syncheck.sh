#!/usr/bin/env bash
# usage: syncheck.sh <tree-root> <file>  -> "OK file", "NEW file" (absent in tree) or "FAIL file :: first error"
root=$1; f=$2; lib=$(echo "$f" | cut -d/ -f4)
if [ ! -f "$root/$f" ]; then echo "NEW  $f"; exit 0; fi
out=$(cd "$root" && g++ -std=c++20 -fsyntax-only -w -Icore/libs -Icore/libs/prx -Icore/libs/prx/$lib -Icore/libs/prx/$lib/include -Icore -I3rdparty "$f" 2>&1)
if [ $? -eq 0 ]; then echo "OK   $f"; else echo "FAIL $f :: $(echo "$out" | grep -m1 -E 'error' | cut -c1-220)"; fi
