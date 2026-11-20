#!/bin/sh
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:?usage: tools/build.sh <agent-name>}"
B="/root/acn/$NAME/build"
[ -f "$B/build.ninja" ] || cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
nice -n 10 ninja -C "$B" -j8
