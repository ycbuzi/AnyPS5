#!/bin/bash
set -euo pipefail
DISK=${1:?usage: install-internal-v2.sh <emmc disk> <image bytes> <image sha256>  (image on stdin)}
BYTES=${2:?image size in bytes}
SUM=${3:?image sha256}
A=/mnt/aos-a
D=/mnt/aos-data

[ "$(lsblk -dno TRAN "$DISK" | xargs)" = mmc ] || { echo "refusing: $DISK is not an eMMC"; exit 1; }
size=$(lsblk -dbno SIZE "$DISK")
[ "$size" -gt 100000000000 ] && [ "$size" -lt 140000000000 ] || { echo "refusing: $DISK size $size is not the 117 GB eMMC"; exit 1; }
[ "$BYTES" -lt "$size" ] || { echo "refusing: image is larger than $DISK"; exit 1; }
case "$(findmnt -no SOURCE /)" in "$DISK"*) echo "refusing: the running system is on $DISK"; exit 1;; esac
for p in "$DISK"p*; do findmnt -rn -S "$p" >/dev/null && { echo "refusing: $p is mounted"; exit 1; }; done

echo "== write image to $DISK"
for p in "$DISK"p*; do [ -b "$p" ] && wipefs -a "$p" >/dev/null 2>&1 || true; done
zstd -d -c | dd of="$DISK" bs=4M iflag=fullblock conv=fsync status=none
sync

echo "== verify"
got=$(head -c "$BYTES" "$DISK" | sha256sum | cut -d' ' -f1)
[ "$got" = "$SUM" ] || { echo "verify FAILED: $got != $SUM"; exit 1; }
echo "verified $got"

echo "== grow the data partition"
partx -u "$DISK" || true
sfdisk --relocate gpt-bak-std "$DISK"
echo ", +" | sfdisk -N 5 --no-reread "$DISK"
partx -u "$DISK"
udevadm settle
e2fsck -fy "${DISK}p5" >/dev/null || [ $? -le 1 ]
resize2fs "${DISK}p5"

echo "== carry state from this system"
E=/mnt/aos-esp
mkdir -p "$A" "$D" "$E"
mount "${DISK}p2" "$A"
mount "${DISK}p5" "$D"
mount "${DISK}p1" "$E"
trap 'umount "$A" "$D" "$E" 2>/dev/null || true' EXIT
sed -i 's/ console=ttyS0,115200//g; s/ console=ttyS0//g' "$E"/loader/entries/*.conf "$E/anyps5/layout.conf"
H=/home/anyps5
(cd "$H" && tar --exclude=./.bash_profile --exclude=./.hushlogin --exclude=./.anyps5-console-version \
    --exclude='./.anyps5-*.log' -cf - .) | tar -C "$D/home/anyps5" -xf -
chown -R 1000:1000 "$D/home/anyps5"
install -d "$D/anyps5"
touch "$D/anyps5/grown"
echo "$(date -Is) installed from the USB system; data grown to $(lsblk -no SIZE "${DISK}p5" | head -1)" >> "$D/anyps5/boot-history.log"

for f in /etc/NetworkManager/system-connections/* /etc/ssh/ssh_host_* /etc/bluetooth/input.conf \
         /usr/local/bin/anyps5-bt-trust /usr/local/bin/anyps5-pad-watch \
         /etc/systemd/system/anyps5-bt-trust.service /etc/systemd/system/anyps5-pad-watch.service \
         /etc/udev/rules.d/70-anyps5-controllers.rules /etc/udev/rules.d/71-anyps5-bt-trust.rules; do
  [ -e "$f" ] || continue
  install -d "$A$(dirname "$f")"
  cp -a "$f" "$A$f"
done
chmod 600 "$A"/etc/NetworkManager/system-connections/* 2>/dev/null || true
for d in /usr/share/alsa/ucm2/conf.d/sof-rt5682 /etc/wireplumber/wireplumber.conf.d; do
  [ -d "$d" ] || continue
  rm -rf "$A$d"
  install -d "$A$(dirname "$d")"
  cp -a "$d" "$A$d"
done
for pkg in $(comm -23 <(pacman -Qq | sort) <(pacman --root "$A" -Qq | sort)); do
  file=$(ls -t /var/cache/pacman/pkg/"$pkg"-[0-9]*.pkg.tar.zst 2>/dev/null | head -1 || true)
  if [ -n "$file" ]; then pacman --root "$A" --dbpath "$A/var/lib/pacman" -U --noconfirm --needed "$file" >/dev/null || echo "could not carry $pkg"; else echo "not carried (no cached package): $pkg"; fi
done
grep -q '^AlwaysPairable' "$A/etc/bluetooth/main.conf" || sed -i 's/^#\?AlwaysPairable.*/AlwaysPairable = true/' "$A/etc/bluetooth/main.conf"
systemctl --root="$A" enable anyps5-bt-trust.service anyps5-pad-watch.service
rm -rf "$A/var/lib/bluetooth"
cp -a /var/lib/bluetooth "$A/var/lib/bluetooth"
[ -d /opt/anyps5 ] && { rm -rf "$A/opt/anyps5"; cp -a /opt/anyps5 "$A/opt/anyps5"; }
rm -rf "$A/opt/anyps5-console"
cp -a /opt/anyps5-console "$A/opt/anyps5-console"
cp -a /usr/local/bin/anyps5-session "$A/usr/local/bin/anyps5-session"
grep -q '/opt/anyps5-console/anyps5-console' "$A/usr/local/bin/anyps5-session" || { echo "session does not start the native console"; exit 1; }
sync
echo "== done: AnyPS5 OS v2 is on $DISK; remove the USB stick and restart to boot it"
