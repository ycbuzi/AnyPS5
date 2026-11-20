#!/bin/bash
# Copies the toolchain's own scripts (bin/anyps5-*, README.txt) into an already built bundle without rebuilding AnyPS5.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${1:-/root/aos-toolchain/opt/anyps5}
[ -x "$OUT/bin/relinker" ] || { echo "no bundle at $OUT (run build-toolchain.sh)"; exit 1; }
install -m 755 "$HERE/bin/anyps5-build-game" "$HERE/bin/anyps5-compat" "$OUT/bin/"
install -m 644 "$HERE/README.txt" "$OUT/README.txt"
echo "scripts updated in $OUT"
