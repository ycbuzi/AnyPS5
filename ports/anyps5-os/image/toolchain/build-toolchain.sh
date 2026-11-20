#!/bin/bash
# Builds the on-device AnyPS5 toolchain for the AnyPS5 OS (Linux x86-64, x86-64-v2 baseline: no AVX) in WSL Arch, as root.
# Source: the tar files export-source.ps1 wrote to C:\AnyPS5-work\ostoolchain-src (by default the committed HEAD of
# C:\AnyPS5-work\wt-cbint, branch chromebook/integration-1116 = the --to-sse branch merged with quest/integration-1116,
# with every submodule). They are unpacked to /root/aos-toolchain/src on ext4: a build reading the source over /mnt/c
# (9p) is several times slower than one on the WSL disk, and the copy takes under a minute.
# Result: /root/aos-toolchain/opt/anyps5 (bin/relinker, bin/anyps5-build-game, bin/anyps5-compat, libs/, share/, VERSION).
# usage: build-toolchain.sh [jobs]     log: /mnt/c/AnyPS5-work/ostoolchain-build.log (run-locked.ps1 starts it)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TARS=${TARS:-/mnt/c/AnyPS5-work/ostoolchain-src}
W=${W:-/root/aos-toolchain}
SRC=$W/src
BUILD=$W/build
OUT=$W/opt/anyps5
JOBS=${1:-12}
BASELINE=x86-64-v2
CFLAGS_ARCH="-march=$BASELINE -mtune=generic"
step() { echo "== $* $(date '+%F %T')"; }

[ -f "$TARS/manifest.tsv" ] && [ -f "$TARS/source.env" ] || { echo "no exported source in $TARS (run export-source.ps1 first)"; exit 1; }
. <(tr -d '\r' < "$TARS/source.env")
step "AnyPS5 OS toolchain $COMMIT (source: $REF, lowering: $LOWERING), $JOBS jobs"

pacman -S --needed --noconfirm gcc cmake ninja python git nasm pkgconf ccache binutils libx11 libxext libxrandr libxkbcommon \
  wayland wayland-protocols libdecor mesa libpipewire alsa-lib libpulse vulkan-icd-loader > /dev/null 2>&1

EXPORT_STAMP="$COMMIT $(ls -l --time-style=+%s "$TARS" | md5sum | cut -c1-12)"
if [ "$(cat "$SRC/.commit" 2>/dev/null)" != "$EXPORT_STAMP" ]; then
  step "unpack source"
  rm -rf "$SRC" "$BUILD"
  mkdir -p "$SRC"
  while IFS=$'\t' read -r tar path; do
    path=${path%$'\r'}
    mkdir -p "$SRC/$path"
    tar -C "$SRC/$path" -xf "$TARS/$tar"
  done < "$TARS/manifest.tsv"
  echo "$EXPORT_STAMP" > "$SRC/.commit"
fi

TO_SSE=no
grep -q '"--to-sse"' "$SRC/core/relinker/cli/src/CliArgs.cpp" && TO_SSE=yes
SSE_LOWERS=""
if [ "$TO_SSE" = yes ]; then
  SSE_LOWERS=$(python3 - "$SRC/core/libs/prx/libkernel/System/src/HostCpu.cpp" <<'PY2'
import re, sys
m = re.search(r'kLoweredFeatureNames\[\]\s*=\s*\{([^}]*)\}', open(sys.argv[1], encoding="utf-8").read())
if not m:
    sys.exit("no kLoweredFeatureNames in " + sys.argv[1])
print(" ".join(n for n in re.findall(r'"([^"]+)"', m.group(1)) if " " not in n))
PY2
)
fi
step "relinker --to-sse: $TO_SSE${SSE_LOWERS:+ (lowers $SSE_LOWERS)}"

FFMPEG_DIR=${FFMPEG_PREBUILT_DIR:-}
FFMPEG_NOTE="prebuilt $FFMPEG_DIR (static)"
if [ -z "$FFMPEG_DIR" ]; then
  pacman -S --needed --noconfirm ffmpeg > /dev/null 2>&1
  FFMPEG_DIR=$W/ffmpeg-system
  rm -rf "$FFMPEG_DIR"
  mkdir -p "$FFMPEG_DIR/include" "$FFMPEG_DIR/lib" "$FFMPEG_DIR/share/ffmpeg"
  for l in avformat avcodec swscale avutil avfilter swresample; do
    ln -s "/usr/include/lib$l" "$FFMPEG_DIR/include/lib$l"
    echo "INPUT(/usr/lib/lib$l.so)" > "$FFMPEG_DIR/lib/lib$l.a"
  done
  FFMPEG_NOTE="system ffmpeg $(pacman -Q ffmpeg | cut -d' ' -f2) (shared, from the OS's ffmpeg package)"
fi

step "configure"
export CCACHE_DIR=$W/ccache
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_C_FLAGS="$CFLAGS_ARCH" -DCMAKE_CXX_FLAGS="$CFLAGS_ARCH" -DCMAKE_ASM_FLAGS="$CFLAGS_ARCH" \
  -DMI_OPT_ARCH=OFF -DFFMPEG_PREBUILT_DIR="$FFMPEG_DIR" > "$W/configure.log" 2>&1 || { tail -30 "$W/configure.log"; exit 1; }
tail -2 "$W/configure.log"

step "build relinker"
nice -n 15 cmake --build "$BUILD" --target relinker --parallel "$JOBS"
step "build libs"
nice -n 15 cmake --build "$BUILD" --target libs --parallel "$JOBS"

step "install $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/libs" "$OUT/share"
install -m 755 "$BUILD/core/relinker/relinker" "$OUT/bin/relinker"
expected=$(find "$SRC/core/libs/prx" -mindepth 1 -maxdepth 1 -type d ! -name '.*' -printf '%f.prx\n' | sort)
missing=""
for lib in $expected; do
  if [ -f "$BUILD/core/libs/libs/$lib" ]; then install -m 644 "$BUILD/core/libs/libs/$lib" "$OUT/libs/$lib"; else missing="$missing $lib"; fi
done
[ -z "$missing" ] || { echo "missing libraries:$missing"; exit 1; }
for extra in "$BUILD"/core/libs/libs/*.so*; do [ -f "$extra" ] && install -m 644 "$extra" "$OUT/libs/"; done
install -m 755 "$HERE/bin/anyps5-build-game" "$OUT/bin/anyps5-build-game"
install -m 755 "$HERE/bin/anyps5-compat" "$OUT/bin/anyps5-compat"
install -m 755 "$HERE/isa-scan.py" "$OUT/share/isa-scan.py"
install -m 644 "$SRC/tools/unwrap_self.py" "$OUT/share/unwrap_self.py"
install -m 644 "$HERE/README.txt" "$OUT/README.txt"
mkdir -p "$OUT/share/licenses"
install -m 644 "$SRC/LICENSE" "$OUT/share/licenses/AnyPS5.txt" 2>/dev/null || true
install -m 644 "$SRC/3rdparty/mimalloc/LICENSE" "$OUT/share/licenses/mimalloc.txt" 2>/dev/null || true

python3 - "$SRC/core/libs/prx/libkernel/System/src/HostCpu.cpp" "$OUT/share/cpu-requirements.txt" <<'PY'
import re, sys
flags = {"SSE": "sse", "SSE2": "sse2", "SSE3": "pni", "SSSE3": "ssse3", "SSE4.1": "sse4_1", "SSE4.2": "sse4_2",
         "POPCNT": "popcnt", "LZCNT": "abm", "BMI1": "bmi1", "BMI2": "bmi2", "MOVBE": "movbe", "CMPXCHG16B": "cx16",
         "LAHF/SAHF": "lahf_lm", "AVX": "avx", "AVX2": "avx2", "FMA": "fma", "F16C": "f16c", "AES": "aes",
         "PCLMULQDQ": "pclmulqdq", "RDRAND": "rdrand", "RDSEED": "rdseed", "ADX": "adx", "SHA": "sha_ni",
         "CLFLUSHOPT": "clflushopt", "CLWB": "clwb", "RDPID": "rdpid", "XSAVEOPT": "xsaveopt", "XSAVEC": "xsavec",
         "SSE4A": "sse4a", "PREFETCHW": "3dnowprefetch", "CLZERO": "clzero"}
rows = re.findall(r'\{"([^"]+)",\s*Register::\w+,\s*\d+,\s*(true|false)\}', open(sys.argv[1], encoding="utf-8").read())
if not rows:
    sys.exit("no CPU feature table in " + sys.argv[1])
with open(sys.argv[2], "w") as out:
    out.write("# feature required|optional /proc/cpuinfo-flag (from libkernel HostCpu.cpp of this build)\n")
    for name, required in rows:
        out.write(f"{name} {'required' if required == 'true' else 'optional'} {flags.get(name, '?')}\n")
PY

lowers=""
[ "$LOWERING" = no ] || lowers=$(cat "$SRC/core/relinker/LOWERS.txt" 2>/dev/null || echo "")
avx_relinker=$(objdump -d --no-show-raw-insn "$OUT/bin/relinker" | grep -c -E '%[yz]mm' || true)
avx_libs=0
for f in "$OUT"/libs/*; do n=$(objdump -d --no-show-raw-insn "$f" 2>/dev/null | grep -c -E '%[yz]mm' || true); avx_libs=$((avx_libs + n)); done
glibc=$(for f in "$OUT/bin/relinker" "$OUT"/libs/*; do objdump -T "$f" 2>/dev/null; done | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
cat > "$OUT/VERSION" <<EOF
name=AnyPS5 OS toolchain
commit=$COMMIT
commit_date=$COMMIT_DATE
source_ref=$REF
built=$(date -u '+%FT%TZ')
compiler=$(gcc -dumpfullversion)
target=linux-x86_64
cpu_baseline=$BASELINE
avx_lowering=$LOWERING
lowers=$lowers
to_sse=$TO_SSE
to_sse_branch=$([ "$TO_SSE" = yes ] && echo "$REF")
to_sse_commit=$([ "$TO_SSE" = yes ] && echo "$COMMIT")
sse_lowers=$SSE_LOWERS
min_glibc=${glibc#GLIBC_}
ffmpeg=$FFMPEG_NOTE
ymm_instructions_relinker=$avx_relinker
ymm_instructions_libs=$avx_libs
skipped_submodules=$SKIPPED
EOF
for f in "$OUT/bin/relinker" "$OUT"/libs/*.prx "$OUT"/libs/*.so*; do
  [ -f "$f" ] && ldd "$f" 2>/dev/null | awk '/=>/ {print $1}'
done | sort -u | grep -v -E '\.prx$' > "$OUT/share/system-libraries.txt" || true
python3 "$HERE/isa-scan.py" "$OUT/bin/relinker" "$OUT"/libs/* > "$OUT/share/isa-scan.txt" || true
grep -E '^(TOTAL|FILE)' "$OUT/share/isa-scan.txt" | grep -v ' total=0 ' | head -60
cat "$OUT/VERSION"
du -sh "$OUT"
step "done"
