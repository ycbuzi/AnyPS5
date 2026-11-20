#!/bin/sh
cd "$(dirname "$0")/.."
NAME="${1:?usage: tools/handoff-test.sh <agent-name> [out dir]}"
OUT="${2:-/tmp/handoff-test-$NAME}"
mkdir -p "$OUT"
F=tests/fixtures/handoff.json
fail=0
check() {
    log="$OUT/$1.log"
    sh tools/shot.sh "$NAME" "$OUT/$1.png" "$2" "$F" > "$log" 2>&1
    shift 2
    for want in "$@"; do
        if grep -qF -- "$want" "$log"; then :; else echo "FAIL $want"; fail=1; fi
    done
}
check crash "ok,ok,event:0,event:1,wait:800,event:2,event:3" 'sent {"display":"released"}' 'display released for a game' 'display taken back'
check hold "ok,ok,event:0,event:1,pshold,wait,event:2" 'sent {"closegame":true}' 'display taken back'
check suspend "ok,ok,event:0,event:1,ps,event:4" 'sent {"suspend":true}' 'display taken back'
check resume "ok,ok,event:0,event:1,ps,event:4,wait:800,ok,event:5,event:6,wait,event:2" 'sent {"resume":true}' 'sent {"suspend":true}'
check nogame "ok,ok,event:0,wait:21000" 'no game started, taking the display back'
check card "ok,ok,event:8,event:9,wait:600" 'launch card: Animal Well shown' 'launch card: preparing 42% (Building 42%)'
check cardhold "ok,ok,event:8,event:10,event:0,wait:1500,event:1,wait:3000,event:2" 'launch card: game process started (pid 4242)' 'handing the display to Animal Well (pid 4242)' 'sent {"display":"released"}' 'game ran for 3 s' 'toast: Animal Well ran for 3 s'
check cardfail "ok,ok,event:8,event:11" 'launch card: Animal Well did not start, card hidden' 'toast: Could not start Animal Well'
check closedafter "ok,ok,event:0,event:1,wait:2000,pshold,wait,event:2" 'sent {"closegame":true}' 'toast: Animal Well closed after 3 s'
ms=$(sed -n 's/.*launch card: shown \([0-9]*\) ms.*/\1/p' "$OUT/cardhold.log")
[ -n "$ms" ] && [ "$ms" -ge 2000 ] || { echo "FAIL launch card shown for '$ms' ms, wanted at least 2000"; fail=1; }
n=$(grep -c 'display released for a game' "$OUT/resume.log")
[ "$n" = 2 ] || { echo "FAIL resume released $n times"; fail=1; }
[ $fail = 0 ] && echo "handoff harness: ok ($OUT)" || echo "handoff harness: FAILED ($OUT)"
exit $fail
