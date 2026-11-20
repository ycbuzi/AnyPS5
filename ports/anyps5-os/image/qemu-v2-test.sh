#!/bin/bash
set -uo pipefail
OUT=${OUT:-/root/aos}
Q=$OUT/qemu
IMG=$OUT/anyps5-os-v2.img
V1=$OUT/anyps5-os.img
PORT=${PORT:-2223}
HPORT=${HPORT:-8800}
CPU=${CPU:-Snowridge}
KEY=/root/.ssh/aos_test_key
SUM=$Q/v2-summary.txt
CODE=/usr/share/edk2/x64/OVMF_CODE.4m.fd
VARS=/usr/share/edk2/x64/OVMF_VARS.4m.fd
STEPS=${STEPS:-before boot bad update menu recovery reset}
URL=http://10.0.2.2:$HPORT/anyps5-os-rootfs.tar.zst
FAILS=0
mkdir -p "$Q"
pacman -S --needed --noconfirm qemu-base edk2-ovmf openssh python > /dev/null
[ -f "$KEY" ] || install -m 600 -D $HOME/.ssh/id_ed25519_chromebook "$KEY"
SSHO=(-p "$PORT" -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR -o BatchMode=yes)

g() { timeout "${GT:-60}" ssh "${SSHO[@]}" root@127.0.0.1 "$@"; }
note() { echo "$(date +%T) $*" | tee -a "$SUM"; }
pass() { note "PASS $*"; }
fail() { note "FAIL $*"; FAILS=$((FAILS + 1)); }
check() { local what=$1; shift; if "$@"; then pass "$what"; else fail "$what"; fi; }

mon() {
  python3 - "$Q/mon.sock" "$@" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.settimeout(2)
try:
    s.recv(65536)
except Exception:
    pass
for c in sys.argv[2:]:
    s.send((c + "\n").encode())
    time.sleep(0.15)
time.sleep(0.5)
PY
}

spam_keys() {
  python3 - "$Q/mon.sock" "$1" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
end = time.time() + float(sys.argv[2])
while time.time() < end:
    s.send(b"sendkey spc\n")
    time.sleep(0.25)
    try:
        s.setblocking(False); s.recv(65536)
    except Exception:
        pass
    s.setblocking(True)
PY
}

shot() {
  mon "screendump $Q/$1.ppm"
  python3 - "$Q/$1.ppm" "$Q/$1.png" <<'PY'
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
parts, i = [], 0
while len(parts) < 4:
    while d[i:i+1].isspace():
        i += 1
    if d[i:i+1] == b"#":
        i = d.index(b"\n", i) + 1
        continue
    j = i
    while not d[j:j+1].isspace():
        j += 1
    parts.append(d[i:j]); i = j
i += 1
w, h = int(parts[1]), int(parts[2])
px = d[i:i + w * h * 3]
raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
def chunk(t, b):
    return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
PY
  rm -f "$Q/$1.ppm"
}

vm_start() {
  local name=$1 disk=$2; shift 2
  rm -f "$Q/mon.sock" "$Q/qemu.pid"
  qemu-system-x86_64 -machine q35,accel=kvm -cpu "$CPU" -m 3072 -smp 4 \
    -drive if=pflash,format=raw,readonly=on,file="$CODE" -drive if=pflash,format=raw,file="$Q/vars.fd" \
    -drive file="$disk",if=none,id=d0,format=qcow2 -device virtio-blk-pci,drive=d0,bootindex=0 "$@" \
    -netdev user,id=n,hostfwd=tcp:127.0.0.1:$PORT-:22 -device virtio-net-pci,netdev=n,romfile= \
    -vga std -display none -serial file:"$Q/$name-serial.log" -monitor unix:"$Q/mon.sock",server,nowait \
    -daemonize -pidfile "$Q/qemu.pid" 2>> "$Q/qemu-stderr.log"
  sleep 1
  [ -f "$Q/qemu.pid" ] && kill -0 "$(cat "$Q/qemu.pid")" 2>/dev/null
}

vm_stop() {
  [ -f "$Q/qemu.pid" ] || return 0
  local p; p=$(cat "$Q/qemu.pid")
  mon quit 2>/dev/null || kill "$p" 2>/dev/null
  for _ in $(seq 20); do kill -0 "$p" 2>/dev/null || break; sleep 0.5; done
  kill -9 "$p" 2>/dev/null
  rm -f "$Q/qemu.pid"
}

boot_id() { GT=8 g cat /proc/sys/kernel/random/boot_id 2>/dev/null; }

wait_boot() {
  local prev=$1 limit=$2 end id
  end=$((SECONDS + limit))
  while [ "$SECONDS" -lt "$end" ]; do
    id=$(boot_id)
    if [ -n "$id" ] && [ "$id" != "$prev" ]; then echo "$id"; return 0; fi
    sleep 3
  done
  return 1
}

wait_unit() {
  local unit=$1 limit=$2 end s
  end=$((SECONDS + limit))
  while [ "$SECONDS" -lt "$end" ]; do
    s=$(GT=10 g systemctl is-active "$unit" 2>/dev/null)
    case $s in active|failed) echo "$s"; return 0 ;; esac
    sleep 3
  done
  echo timeout
}

slot() { g "sed -n 's/.*anyps5\.slot=\([ab]\).*/\1/p' /proc/cmdline"; }
entries() { g 'ls /efi/loader/entries | tr "\n" " "'; }
countpath() { g '. /usr/lib/anyps5/boot-lib.sh; basename "$(aos_boot_count_path)" 2>/dev/null'; }

analyze() {
  local tag=$1
  {
    echo "### $tag"
    g 'systemd-analyze; systemd-analyze blame | head -15; systemd-analyze critical-chain multi-user.target | head -25'
  } >> "$Q/analyze.txt" 2>&1
  note "$tag: $(g systemd-analyze 2>/dev/null | head -1)"
  note "$tag: console session up $(g 'echo $(( $(stat -c %Y /run/user/1000/anyps5-session-started) - $(awk "/btime/{print \$2}" /proc/stat) ))' 2>/dev/null) s after kernel start"
}

VD=/data/home/anyps5/verify
dsum() { g "cd $VD && sha256sum * | sha256sum | cut -c1-16" 2>/dev/null; }
hist_prefix() { g 'cat /data/anyps5/boot-history.log' > "$Q/hist-now.txt" 2>/dev/null; head -c "$(stat -c %s "$Q/hist-then.txt")" "$Q/hist-now.txt" | cmp -s - "$Q/hist-then.txt"; cp "$Q/hist-now.txt" "$Q/hist-then.txt"; }
data_same() { local s; s=$(dsum); check "DATA unchanged after $1 ($s)" [ -n "$s" -a "$s" = "$D0" ]; check "boot history only appended after $1" hist_prefix; }

trap 'vm_stop; [ -n "${HTTP:-}" ] && kill "$HTTP" 2>/dev/null' EXIT
note "== qemu v2 test start (cpu $CPU, steps: $STEPS)"

has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }

if has before && [ -f "$V1" ]; then
  note "== before: v1 image under OVMF + systemd-boot"
  install -m 644 "$VARS" "$Q/vars.fd"
  rm -f "$Q/v1.qcow2"; qemu-img create -q -f qcow2 -b "$V1" -F raw "$Q/v1.qcow2"
  vm_start v1 "$Q/v1.qcow2" || { CPU=max; note "cpu Snowridge refused, using max"; vm_start v1 "$Q/v1.qcow2"; }
  b=$(wait_boot "" 300) && pass "v1 boots" || fail "v1 boots"
  GT=150 g 'systemctl is-system-running --wait' > /dev/null 2>&1
  GT=20 g 'systemctl reboot' > /dev/null 2>&1
  b2=$(wait_boot "$b" 300) && { GT=150 g 'systemctl is-system-running --wait' > /dev/null 2>&1; analyze "before (v1, second boot)"; }
  vm_stop
fi

install -m 644 "$VARS" "$Q/vars.fd"
size=$(stat -c %s "$IMG")
rm -f "$Q/v2.qcow2"; qemu-img create -q -f qcow2 -b "$IMG" -F raw "$Q/v2.qcow2" $(( size + 8 * 1024 * 1024 * 1024 ))
if has update; then
  note "== making a USB stick copy of the image (new partition GUIDs)"
  rm -f "$Q/stick.img"; cp --sparse=always "$IMG" "$Q/stick.img"; sgdisk -G "$Q/stick.img" > /dev/null
  STICK=(-drive file="$Q/stick.img",if=none,id=d1,format=raw,readonly=on -device virtio-blk-pci,drive=d1)
else
  STICK=()
fi
note "== v2 first boot"
vm_start v2 "$Q/v2.qcow2" "${STICK[@]}" || { CPU=max; note "cpu Snowridge refused, using max"; vm_start v2 "$Q/v2.qcow2" "${STICK[@]}"; }
( for t in 4 7 10 14 20 30; do sleep $(( t - ${prev:-0} )); prev=$t; shot "boot-${t}s"; done ) &
B=$(wait_boot "" 300) || { fail "v2 boots"; exit 1; }
pass "v2 boots (boot id $B)"
check "first boot is slot a" [ "$(slot)" = a ]
r=$(wait_unit anyps5-boot-ok 200); check "anyps5-boot-ok active on first boot ($r)" [ "$r" = active ]
check "systemd-bless-boot marked the entry good" [ "$(GT=10 g systemctl is-active systemd-bless-boot)" = active ]
e=$(entries); check "entry blessed: $e" grep -q "anyps5-a.conf " <<< "$e "
check "data mounted, home bound, efi mounted" g 'findmnt /data >/dev/null && findmnt /home >/dev/null && findmnt /efi >/dev/null'
r=$(wait_unit anyps5-data-grow 60)
note "data size: $(g 'df -h /data | tail -1')"
check "data partition grown" g 'test -e /data/anyps5/grown'
check "home seeded from the slot" g 'test -f /home/anyps5/.bash_profile && test -d /home/anyps5/console'
check "session marker" g 'ls /run/user/1000/anyps5-session-started'
g 'plymouth --ping && echo plymouth-running || echo plymouth-gone; lsinitcpio -a /efi/anyps5/a/initramfs-linux.img 2>/dev/null | head -12; ls -la /efi/anyps5/a/' >> "$Q/boot-details.txt" 2>&1
g 'echo keep > /data/home/anyps5/canary.txt; ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub' > "$Q/hostkey-a.txt" 2>&1
g "mkdir -p $VD && head -c 32M /dev/urandom > $VD/big.bin && for i in \$(seq 50); do head -c 4096 /dev/urandom > $VD/f\$i; done; sync"
D0=$(dsum); g 'cat /data/anyps5/boot-history.log' > "$Q/hist-then.txt" 2>/dev/null
check "DATA verify tree written ($D0)" [ -n "$D0" ]
analyze "after (v2 first boot, includes data grow and ssh key generation)"
wait

if has bad; then
  note "== bad slot: update into b over HTTP, then break b so its console never starts"
  python3 -m http.server "$HPORT" --bind 127.0.0.1 --directory "$OUT" > "$Q/http.log" 2>&1 &
  HTTP=$!
  sleep 1
  GT=1500 g "anyps5-update $URL" > "$Q/update-http.log" 2>&1; rc=$?
  check "anyps5-update over HTTP (rc $rc)" [ $rc = 0 ]
  e=$(entries); check "fresh counted entry for b: $e" grep -q "anyps5-b+3.conf" <<< "$e"
  g 'cat /efi/loader/entries/anyps5-b+3.conf' >> "$Q/update-http.log" 2>&1
  g '. /efi/anyps5/layout.conf; mount /dev/disk/by-partuuid/$PART_B /mnt && ln -sf /dev/null /mnt/etc/systemd/system/getty@tty1.service && echo TIMEOUT=30 > /mnt/etc/anyps5/boot-ok.conf; umount /mnt'
  GT=10 g 'systemctl reboot' > /dev/null 2>&1
  prev=$B; seen=""; end=$((SECONDS + 1200)); final=""
  while [ "$SECONDS" -lt "$end" ]; do
    id=$(wait_boot "$prev" 300) || break
    prev=$id
    s=$(slot); c=$(countpath)
    seen="$seen $s:${c:-uncounted}"
    note "boot: slot $s entry ${c:-uncounted}"
    if [ "$s" = a ]; then final=a; break; fi
  done
  B=$prev
  check "b failed 3 times then a started by itself:$seen" [ "$final" = a ]
  e=$(entries); check "b marked bad: $e" grep -q "anyps5-b+0-3.conf" <<< "$e"
  r=$(wait_unit anyps5-boot-ok 120); check "a reaches the console after fallback ($r)" [ "$r" = active ]
  g 'cat /data/anyps5/boot-history.log' > "$Q/boot-history-after-fallback.txt" 2>&1
  check "3 failures logged on data" [ "$(grep -c 'slot=b.*failed' "$Q/boot-history-after-fallback.txt")" = 3 ]
  g 'grep -h ^title /efi/loader/entries/anyps5-*' >> "$Q/boot-history-after-fallback.txt" 2>&1
  data_same "3-failure fallback"
fi

if has update; then
  note "== interrupted update: truncated rootfs over HTTP must fail and leave no entry for the target slot"
  if [ -n "${HTTP:-}" ]; then
    head -c 200000000 "$OUT/anyps5-os-rootfs.tar.zst" > "$Q/trunc.tar.zst"
    tgt=$( [ "$(slot)" = a ] && echo b || echo a )
    GT=900 g "anyps5-update http://10.0.2.2:$HPORT/qemu/trunc.tar.zst" > "$Q/update-trunc.log" 2>&1; rc=$?
    check "truncated update refused (rc $rc)" [ $rc != 0 ]
    e=$(entries); check "no bootable entry left for slot $tgt: $e" eval "! grep -q 'anyps5-$tgt' <<< '$e'"
    rm -f "$Q/trunc.tar.zst"
  fi
  note "== good update into the inactive slot from the USB stick (/dev/vdb)"
  GT=1500 g 'anyps5-update /dev/vdb' > "$Q/update-stick.log" 2>&1; rc=$?
  check "anyps5-update from the stick (rc $rc)" [ $rc = 0 ]
  inst=$(sed -n 's/.*installed: build .*entry version \([0-9]*\).*/\1/p' "$Q/update-stick.log")
  e=$(entries); note "entries: $e (new version $inst)"
  cur=$(slot)
  GT=10 g 'systemctl reboot' > /dev/null 2>&1
  B=$(wait_boot "$B" 300) || { fail "boot after update"; exit 1; }
  s=$(slot); check "booted the updated slot ($s, was $cur)" [ "$s" != "$cur" ]
  r=$(wait_unit anyps5-boot-ok 200); check "updated slot reaches the console ($r)" [ "$r" = active ]
  e=$(entries); check "updated slot blessed: $e" grep -q "anyps5-$s.conf" <<< "$e"
  g 'grep -h ^title /efi/loader/entries/*.conf' > "$Q/titles.txt" 2>&1
  check "titles: current 'AnyPS5 OS', other '(previous)'" grep -q "(previous)" "$Q/titles.txt"
  check "data kept across slots" g 'grep -q keep /data/home/anyps5/canary.txt'
  g 'ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub' > "$Q/hostkey-b.txt" 2>&1
  check "ssh host key carried over" cmp -s "$Q/hostkey-a.txt" "$Q/hostkey-b.txt"
  data_same "update into the other slot"
fi

if has menu; then
  note "== hidden boot menu (space held during boot)"
  cur=$(slot)
  GT=10 g 'systemctl reboot' > /dev/null 2>&1
  spam_keys 25
  shot boot-menu
  mon "sendkey ret"
  B=$(wait_boot "$B" 300) && pass "boots after the menu" || fail "boots after the menu"
  check "menu Enter boots the default slot" [ "$(slot)" = "$cur" ]
  r=$(wait_unit anyps5-boot-ok 200)
  analyze "after (v2, normal boot)"
  g 'journalctl -b -o short-monotonic --no-pager | grep -E "tty1|agetty|user@1000|user-runtime-dir|session-[0-9]|logind|Startup finished|multi-user|graphical" | head -60; systemd-analyze critical-chain getty@tty1.service' > "$Q/tty1-trace.txt" 2>&1
  data_same "boot menu boot"
fi

if has recovery; then
  note "== recovery entry"
  GT=20 g 'bootctl set-oneshot recovery.conf && systemctl reboot' > /dev/null 2>&1
  B=$(wait_boot "$B" 300) || { fail "recovery boots"; exit 1; }
  check "recovery system booted" g 'test -f /etc/anyps5/recovery'
  sleep 3
  check "recovery menu runs on tty1" g 'pgrep -t tty1 -f anyps5-recovery >/dev/null'
  shot recovery-menu
  g 'anyps5-recovery status; anyps5-recovery repair' > "$Q/recovery-repair.log" 2>&1
  check "repair keeps both slots" grep -q "slot b: entry" "$Q/recovery-repair.log"
  check "recovery leaves data unmounted" g '. /efi/anyps5/layout.conf; ! grep -q "$(readlink -f /dev/disk/by-partuuid/$PART_DATA) " /proc/mounts'
  check "data untouched by recovery" g '. /efi/anyps5/layout.conf; mount -o ro,noload /dev/disk/by-partuuid/$PART_DATA /mnt && grep -q keep /mnt/home/anyps5/canary.txt; r=$?; umount /mnt; exit $r'
  s=$(g ". /efi/anyps5/layout.conf; mount -o ro,noload /dev/disk/by-partuuid/\$PART_DATA /mnt && (cd /mnt/home/anyps5/verify && sha256sum * | sha256sum | cut -c1-16); umount /mnt")
  check "DATA verify tree intact seen from recovery ($s)" [ -n "$s" -a "$s" = "$D0" ]
fi

if has reset; then
  note "== factory reset inside recovery"
  g "printf 'no\n' | anyps5-recovery reset" > "$Q/reset.log" 2>&1
  check "reset refuses without ERASE" g '. /efi/anyps5/layout.conf; mount -o ro,noload /dev/disk/by-partuuid/$PART_DATA /mnt && grep -q keep /mnt/home/anyps5/canary.txt; r=$?; umount /mnt; exit $r'
  g "printf 'ERASE\n' | anyps5-recovery reset" >> "$Q/reset.log" 2>&1
  check "reset erases data after ERASE" g '. /efi/anyps5/layout.conf; mount -o ro,noload /dev/disk/by-partuuid/$PART_DATA /mnt && ! test -e /mnt/home/anyps5/canary.txt; r=$?; umount /mnt; exit $r'
  GT=10 g 'systemctl reboot' > /dev/null 2>&1
  B=$(wait_boot "$B" 300) || { fail "boot after reset"; exit 1; }
  r=$(wait_unit anyps5-boot-ok 200); check "console starts after reset ($r)" [ "$r" = active ]
  check "home seeded again" g 'test -f /home/anyps5/.bash_profile && ! test -e /home/anyps5/canary.txt'
  g 'cat /data/anyps5/boot-history.log' > "$Q/boot-history-after-reset.txt" 2>&1
fi

vm_stop
note "== qemu v2 test done: $FAILS failed"
