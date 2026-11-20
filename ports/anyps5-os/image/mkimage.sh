#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${OUT:-/root/aos}
T=$OUT/root
IMG=$OUT/anyps5-os.img
LOG=$OUT/mkimage.log
mkdir -p "$OUT"
exec > >(tee "$LOG") 2>&1
echo "== AnyPS5 OS image build $(date '+%F %T') -> $IMG"
pacman -S --needed --noconfirm arch-install-scripts dosfstools e2fsprogs gptfdisk mtools > /dev/null

if [ "${REUSE_ROOT:-0}" = 1 ]; then
  [ "${LAYOUT:-v1}" = v2 ] || { echo "REUSE_ROOT=1 needs LAYOUT=v2"; exit 1; }
  echo "== reusing the system already built in $T"
else
if mountpoint -q "$T"; then umount -R "$T"; fi
rm -rf "$T" "$OUT/boot" "$OUT/console"
mkdir -p "$T"
mount --bind "$T" "$T"

echo "== console files"
tar -C "/mnt/c/AnyPS5 Games/_tools" --exclude='console/os-host/data' --exclude='*.before-*' --exclude='__pycache__' \
    --exclude='console/art' --exclude='console/cache' --exclude='console/library.js' -cf - console | tar -C "$OUT" -xf -
cp "${KEYS:-$HOME/.ssh/id_ed25519_chromebook.pub}" "$OUT/authorized_keys"

TARGET="$T" CONSOLE_SRC="$OUT/console" AUTH_KEYS="$OUT/authorized_keys" bash "$HERE/install.sh" image

if [ -n "${WIFI_PROFILE:-}" ] && [ -f "$WIFI_PROFILE" ]; then
  install -m 600 "$WIFI_PROFILE" "$T/etc/NetworkManager/system-connections/anyps5-wifi.nmconnection"
  echo "== wifi profile copied"
fi

TOOLCHAIN=${TOOLCHAIN:-/root/aos-toolchain/opt/anyps5}
if [ -x "$TOOLCHAIN/bin/relinker" ] && [ -f "$TOOLCHAIN/VERSION" ]; then
  install -d "$T/opt" "$T/usr/local/bin"
  rm -rf "$T/opt/anyps5"
  cp -a "$TOOLCHAIN" "$T/opt/anyps5"
  ln -sf /opt/anyps5/bin/anyps5-build-game /opt/anyps5/bin/anyps5-compat "$T/usr/local/bin/"
  echo "== AnyPS5 toolchain $(sed -n 's/^commit=//p' "$TOOLCHAIN/VERSION" | cut -c1-12) copied ($(du -sh "$TOOLCHAIN" | cut -f1))"
else
  echo "== no AnyPS5 toolchain in $TOOLCHAIN (toolchain/build-toolchain.sh builds it); image has none"
fi

if [ -x "$HERE/bin/crossystem" ]; then
  install -D -m 755 "$HERE/bin/crossystem" "$T/usr/local/bin/crossystem"
  echo "== crossystem copied ($(cat "$HERE/bin/crossystem.version" 2>/dev/null))"
fi

install -d "$T/etc/systemd/journald.conf.d"
printf '[Journal]\nStorage=volatile\nRuntimeMaxUse=64M\n' > "$T/etc/systemd/journald.conf.d/anyps5-volatile.conf"

install -d "$T/etc/NetworkManager/conf.d"
printf '[connection]\nwifi.powersave = 2\n' > "$T/etc/NetworkManager/conf.d/anyps5-wifi-powersave.conf"

install -m 755 /dev/stdin "$T/usr/local/bin/anyps5-grow" <<'EOF'
#!/bin/sh
set -e
part=$(findmnt -no SOURCE /)
disk=/dev/$(lsblk -no PKNAME "$part")
num=$(cat /sys/class/block/$(basename "$part")/partition)
sfdisk --relocate gpt-bak-std "$disk" || true
echo ", +" | sfdisk -N "$num" --no-reread "$disk" || true
partx -u "$disk" || true
resize2fs "$part"
systemctl disable anyps5-grow.service
EOF
cat > "$T/etc/systemd/system/anyps5-grow.service" <<'EOF'
[Unit]
Description=Grow the AnyPS5 OS root file system to the whole drive
After=local-fs.target
[Service]
Type=oneshot
ExecStart=/usr/local/bin/anyps5-grow
[Install]
WantedBy=multi-user.target
EOF
arch-chroot "$T" systemctl enable anyps5-grow.service
fi

if [ "${LAYOUT:-v1}" = v2 ]; then
  exec bash "$HERE/layout-v2.sh"
fi

echo "== startup image with every driver (built in WSL, booted on other hardware)"
sed -i 's/^\(HOOKS=.*\) autodetect /\1 /' "$T/etc/mkinitcpio.conf"
grep -q '^HOOKS=.* autodetect ' "$T/etc/mkinitcpio.conf" && { echo "autodetect still in HOOKS"; exit 1; }
arch-chroot "$T" mkinitcpio -P
ls -la "$T/boot/initramfs-linux.img"

umount -R "$T" 2>/dev/null || true
for m in proc sys dev run tmp; do umount -R "$T/$m" 2>/dev/null || true; done

echo "== file systems"
mv "$T/boot" "$OUT/boot"
mkdir "$T/boot"
used=$(du -sm "$T" | cut -f1)
root_mb=$(( used * 13 / 10 + 1024 ))
esp_mb=512
rm -f "$OUT/esp.img" "$OUT/root.img" "$IMG"
mkfs.fat -C -F 32 -n APS5EFI "$OUT/esp.img" $(( esp_mb * 1024 )) > /dev/null
mcopy -s -i "$OUT/esp.img" "$OUT/boot"/* ::/
mkfs.ext4 -q -F -L ANYPS5 -d "$T" "$OUT/root.img" "${root_mb}M"

echo "== disk image (${esp_mb} MB boot + ${root_mb} MB system)"
truncate -s $(( (esp_mb + root_mb + 4) * 1024 * 1024 )) "$IMG"
sgdisk -Z "$IMG" > /dev/null
sgdisk -n1:2048:+${esp_mb}M -t1:ef00 -c1:ANYPS5-EFI -n2:0:+${root_mb}M -t2:8304 -c2:ANYPS5 "$IMG" > /dev/null
start2=$(sgdisk -i 2 "$IMG" | awk '/First sector/ {print $3}')
dd if="$OUT/esp.img" of="$IMG" bs=1M seek=1 conv=notrunc,sparse status=none
dd if="$OUT/root.img" of="$IMG" bs=512 seek="$start2" conv=notrunc,sparse status=none
rm -f "$OUT/esp.img" "$OUT/root.img"
sgdisk -v "$IMG" | tail -1
ls -la "$IMG"
sha256sum "$IMG" | tee "$IMG.sha256"
echo "== done $(date '+%F %T')"
