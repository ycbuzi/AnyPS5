#!/bin/sh
set -e
cd "$(dirname "$0")/.."
NAME="${1:?usage: tools/shot.sh <agent-name> <out.png> [keys] [fixture] [extra args...]}"
OUT="${2:?missing out.png}"
KEYS="${3:-}"
FIX="${4:-tests/fixtures/basic.json}"
shift 2; [ $# -gt 0 ] && shift; [ $# -gt 0 ] && shift
SDL_VIDEODRIVER=offscreen nice -n 10 "/root/acn/$NAME/build/anyps5-console" --offscreen 1920x1080 --host-file "$FIX" --keys "$KEYS" --shot "$OUT" "$@"
