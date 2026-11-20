#!/bin/bash
# Screenshots of the console screens fed by a live host: live-shot.sh <host url> <out dir> [screens...]
HOST="$1"; OUT="$2"; shift 2
cd /root/acn/glyphs/build || exit 1
for s in "${@:-home library}"; do
    SDL_VIDEODRIVER=offscreen timeout 60 ./anyps5-console --offscreen 1920x1080 --host "$HOST" --start "$s" --keys wait:6000 --shot "$OUT/live-$s.png" > "/tmp/live-$s.log" 2>&1
    echo "$s rc=$?"; tail -2 "/tmp/live-$s.log"
done
