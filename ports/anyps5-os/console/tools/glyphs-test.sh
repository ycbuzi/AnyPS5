#!/bin/sh
set -e
cd "$(dirname "$0")/.."
NAME="${1:?usage: tools/glyphs-test.sh <agent-name> <out-dir>}"
OUT="${2:?missing out dir}"
BIN="/root/acn/$NAME/build/anyps5-console"
mkdir -p "$OUT"
"$BIN" --selftest > "$OUT/glyphs-selftest.txt" 2>/dev/null || { cat "$OUT/glyphs-selftest.txt"; exit 1; }
tail -1 "$OUT/glyphs-selftest.txt"
shot() {
    SDL_VIDEODRIVER=offscreen nice -n 10 "$BIN" --offscreen 1920x1080 --host-file "${3:-tests/fixtures/basic.json}" --keys "$2" --shot "$OUT/$1.png" $4 > /dev/null 2>&1
    [ -s "$OUT/$1.png" ] || { echo "no shot $1"; exit 1; }
}
for s in playstation xbox nintendo generic generic-chord keyboard chromebook mouse touch; do
    shot "glyphs-$s" "ok,ok,src:$s"
done
for s in playstation xbox generic-chord chromebook mouse; do
    shot "glyphs-help-$s" "ok,ok,help,src:$s"
done
for s in xbox generic-chord keyboard; do
    shot "glyphs-launch-$s" "ok,ok,src:$s,ok,wait:600" tests/fixtures/basic.json "--settle-ms 300"
done
shot glyphs-sheet "src:xbox" tests/fixtures/basic.json "--start glyphs"
shot glyphs-boot-xbox "src:xbox" tests/fixtures/basic.json "--settle-ms 2500"
echo "shots in $OUT"
