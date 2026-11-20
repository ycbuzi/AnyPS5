#!/bin/bash
# Proves the bundle in WSL: relinks one real game with anyps5-build-game, checks its instructions and libraries,
# and checks anyps5-compat against a no-AVX CPU (Pentium Silver N6000 flag set). Does not run the game.
# usage: test-relink.sh [app0 dir on /mnt]   (default Celeste Classic)
set -uo pipefail
GAME=${1:-"/mnt/d/AnyPS5 Games/Celeste Classic (PPSA59322)/app0"}
TC=${TC:-/root/aos-toolchain/opt/anyps5}
T=/root/aos-toolchain/test
rm -rf "$T"; mkdir -p "$T/src"
cp -a "$GAME" "$T/src/app0"
rm -rf "$T/src/app0/radv-shader-cache" "$T/src/app0/shaders" 2>/dev/null
echo "== source $(du -sh "$T/src/app0" | cut -f1)"

echo "== anyps5-build-game"
"$TC/bin/anyps5-build-game" "$T/src" "$T/out" -- ${RELINK_EXTRA:-}
echo "exit $?"
G=$T/out/game
[ -x "$G" ] || { echo "no game binary"; exit 1; }
file "$G" 2>/dev/null || true
echo "== instructions in the relinked game"
python3 "$(dirname "$0")/isa-scan.py" --functions 15 "$G" | tee "$T/game-isa.txt" | grep -E '^(FILE|TOTAL)'
readelf -lW "$G" | awk '$1 == "0x61a50001" || $1 ~ /LOOS\+0x1a50001/ {print "lowered-CPU header: " $0}'
python3 - "$G" "$TC/VERSION" <<'PY3'
import sys
sys.path.insert(0, sys.argv[2].rsplit("/", 1)[0] + "/bin")
import importlib.machinery, importlib.util
loader = importlib.machinery.SourceFileLoader("c", sys.argv[2].rsplit("/", 1)[0] + "/bin/anyps5-compat")
spec = importlib.util.spec_from_loader("c", loader); c = importlib.util.module_from_spec(spec); loader.exec_module(c)
names, mask = c.lowered_in_binary(sys.argv[1], c.read_kv(sys.argv[2]))
print(f"lowered in the binary: 0x{mask:x} {' '.join(sorted(names)) or 'none'}")
PY3
grep -E "SSE conversion|VEX instructions not lowered|not lowered at" "$T/out/relinker-output.txt" | head -20
echo "== guest modules"
find "$T/out/app0" -name '*.guest.prx' | sed "s|$T/out/||"
echo "== libraries (ldd from the game folder)"
cd "$T/out"
ldd "$G" | sed 's/ (0x[0-9a-f]*)//' | awk '{print "  " $0}'
notfound=$(ldd "$G" | grep -c 'not found')
outside=$(ldd "$G" | grep '=>' | grep -v 'not found' | grep -v "$T/out/" | awk '{print $1}' | tr '\n' ' ')
echo "not found: $notfound; resolved outside the game folder: $outside"
readelf -d "$G" | grep -E 'RUNPATH|RPATH' || echo "  (no RPATH/RUNPATH)"
echo "== anyps5-compat on this PC"
"$TC/bin/anyps5-compat"; echo "exit $?"
echo "== anyps5-compat with a no-AVX CPU (N6000 flags, read from the Chromebook 2026-11-16)"
cat > "$T/n6000.cpuinfo" <<'EOF'
vendor_id	: GenuineIntel
model name	: Intel(R) Pentium(R) Silver N6000 @ 1.10GHz
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc art arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf tsc_known_freq pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg cx16 xtpr pdcm sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave rdrand lahf_lm 3dnowprefetch cpuid_fault epb cat_l2 cdp_l2 ssbd ibrs ibpb stibp ibrs_enhanced fsgsbase tsc_adjust smep erms rdt_a rdseed smap clflushopt clwb intel_pt sha_ni xsaveopt xsavec xgetbv1 xsaves split_lock_detect dtherm arat pln pts umip waitpkg gfni rdpid movdiri movdir64b md_clear flush_l1d arch_capabilities
EOF
"$TC/bin/anyps5-compat" --cpuinfo "$T/n6000.cpuinfo" --game "$T/src" --dry-run; echo "exit $?"
"$TC/bin/anyps5-compat" --json --cpuinfo "$T/n6000.cpuinfo"; echo "exit $?"
echo "== reinstall keeps saves"
mkdir -p "$T/out/_sd"; echo save > "$T/out/_sd/save.dat"
"$TC/bin/anyps5-build-game" "$T/src" "$T/out" > "$T/reinstall.log" 2>&1; echo "exit $? ($(tail -1 "$T/reinstall.log"))"
[ -f "$T/out/_sd/save.dat" ] && echo "save kept" || echo "SAVE LOST"
ls -A "$T/out" | tr '\n' ' '; echo
echo "== --unwrap-modules"
"$TC/bin/anyps5-build-game" --unwrap-modules "$T/src" "$T/out-unwrap" 2>&1 | grep -E 'unwrapped|OK|ERROR'
grep -E 'Converted modules|guest' "$T/out-unwrap/relinker-output.txt" | head -5
find "$T/out-unwrap" -name '*.guest.prx' | sed "s|$T/||"
cmp -s "$T/src/app0/sce_module/libc.prx" "$GAME/sce_module/libc.prx" && echo "source module untouched"
echo "== bad input"
mkdir -p "$T/empty"; "$TC/bin/anyps5-build-game" "$T/empty" "$T/out2"; echo "exit $?"
echo "== the --to-sse build (as anyps5-build-game picks it on a CPU without AVX2)"
if grep -q '^to_sse=yes' "$TC/VERSION"; then
  "$TC/bin/anyps5-build-game" "$T/src" "$T/out-sse" -- --to-sse > "$T/sse-build.log" 2>&1; echo "exit $? ($(tail -1 "$T/sse-build.log"))"
  if [ -x "$T/out-sse/game" ]; then
    python3 "$(dirname "$0")/isa-scan.py" --functions 15 "$T/out-sse/game" | tee "$T/game-sse-isa.txt" | grep -E '^(FILE|TOTAL)'
    grep -E "SSE conversion|not lowered at|VEX instructions not lowered" "$T/out-sse/relinker-output.txt" | tail -5
    if command -v qemu-x86_64 >/dev/null; then
      echo "== run under qemu-x86_64 -cpu Snowridge (Tremont, the N6000's core: no AVX, AVX2, FMA, F16C, BMI1, BMI2, LZCNT)"
      (cd "$T/out-sse" && SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout -s KILL ${QEMU_SECONDS:-120} qemu-x86_64 -cpu Snowridge ./game > "$T/qemu.log" 2>&1); echo "exit $?"
      grep -v "TCG doesn't support" "$T/qemu.log" | grep -m1 -E "host CPU" || true
      grep -E -i "illegal instruction|signal 4|lacks|SIGILL" "$T/qemu.log" | head -5
      echo "last lines:"; grep -v "TCG doesn't support" "$T/qemu.log" | tail -15
    fi
  fi
fi
