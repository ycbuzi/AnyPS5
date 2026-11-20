#!/bin/bash
set -euo pipefail
DISK=${1:-/dev/mmcblk0}
T=/mnt/internal
command -v rsync >/dev/null || pacman -S --needed --noconfirm rsync
command -v sgdisk >/dev/null || pacman -S --needed --noconfirm gptfdisk
[ "$(lsblk -dno TRAN "$DISK" | xargs)" = mmc ] || { echo "refusing: $DISK is not the internal eMMC"; exit 1; }
size=$(lsblk -dbno SIZE "$DISK")
[ "$size" -gt 100000000000 ] && [ "$size" -lt 140000000000 ] || { echo "refusing: $DISK size $size is not the 117 GB eMMC"; exit 1; }
rootsrc=$(findmnt -no SOURCE /)
case "$rootsrc" in "$DISK"*) echo "refusing: the running system is on $DISK"; exit 1;; esac

echo "== wipe $DISK (ChromeOS)"
umount -R "$T" 2>/dev/null || true
for p in "$DISK"p*; do [ -b "$p" ] && wipefs -a "$p" >/dev/null 2>&1 || true; done
wipefs -a "$DISK"
sgdisk -Z "$DISK" >/dev/null
sgdisk -n1:0:+1G -t1:ef00 -c1:AOS-EFI -n2:0:0 -t2:8304 -c2:AOS-ROOT "$DISK"
partprobe "$DISK"; udevadm settle
mkfs.fat -F32 -n AOSEFI "${DISK}p1"
mkfs.ext4 -F -q -L AOSROOT "${DISK}p2"
mkdir -p "$T"
mount "${DISK}p2" "$T"
mount --mkdir "${DISK}p1" "$T/boot"

echo "== copy system"
rsync -aHAX --info=progress2 --one-file-system \
  --exclude=/boot/ --exclude=/proc/ --exclude=/sys/ --exclude=/dev/ --exclude=/run/ --exclude=/tmp/ --exclude=/mnt/ \
  --exclude=/var/cache/pacman/pkg/ --exclude=/var/log/journal/ / "$T/"
mkdir -p "$T"/{proc,sys,dev,run,tmp,mnt,var/log/journal}
chmod 1777 "$T/tmp"
cp -a /boot/. "$T/boot/"

echo "== boot setup"
printf 'LABEL=AOSROOT / ext4 rw,noatime,commit=60 0 1\nLABEL=AOSEFI /boot vfat rw,relatime,fmask=0022,dmask=0022 0 2\n' > "$T/etc/fstab"
for f in "$T"/boot/loader/entries/*.conf; do sed -i 's/root=LABEL=ANYPS5/root=LABEL=AOSROOT/' "$f"; done
rm -f "$T/etc/systemd/system/multi-user.target.wants/anyps5-grow.service"
install -d "$T/boot/EFI/BOOT" "$T/boot/EFI/systemd"
cp "$T/usr/lib/systemd/boot/efi/systemd-bootx64.efi" "$T/boot/EFI/BOOT/BOOTX64.EFI"
cp "$T/usr/lib/systemd/boot/efi/systemd-bootx64.efi" "$T/boot/EFI/systemd/systemd-bootx64.efi"
grep -h '^options' "$T"/boot/loader/entries/*.conf
sync
umount -R "$T"
echo "== done: AnyPS5 OS is on $DISK"
