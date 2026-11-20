#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BS=$HERE/boot
OUT=${OUT:-/root/aos}
T=$OUT/root
R=$OUT/recovery
E=$OUT/esp
D=$OUT/data
P=$OUT/parts
IMG=$OUT/anyps5-os-v2.img
TARBALL=$OUT/anyps5-os-rootfs.tar.zst
ESP_MB=${ESP_MB:-1024}
SLOT_MB=${SLOT_MB:-8192}
EXTRA_CMDLINE=${EXTRA_CMDLINE:-}
PC_URL=${PC_URL:-}
LOGO=${LOGO:-/mnt/c/AnyPS5 Games/_tools/console-native/assets/logo-mark.png}
U=anyps5
MASK="man-db.timer shadow.timer archlinux-keyring-wkd-sync.timer NetworkManager-wait-online.service systemd-networkd-wait-online.service"

echo "== layout v2 $(date '+%F %T') -> $IMG"
[ -x "$T/usr/bin/bash" ] || { echo "no built system in $T"; exit 1; }
pacman -S --needed --noconfirm arch-install-scripts dosfstools e2fsprogs gptfdisk mtools zstd > /dev/null
mountpoint -q "$T" || mount --bind "$T" "$T"

ID=$(head -c2 /dev/urandom | od -An -tx1 | tr -d ' \n')
PART_ESP=$(cat /proc/sys/kernel/random/uuid)
PART_A=$(cat /proc/sys/kernel/random/uuid)
PART_B=$(cat /proc/sys/kernel/random/uuid)
PART_RECOVERY=$(cat /proc/sys/kernel/random/uuid)
PART_DATA=$(cat /proc/sys/kernel/random/uuid)
CMDLINE="quiet splash loglevel=0 vt.global_cursor_default=0 systemd.show_status=false rd.systemd.show_status=false rd.udev.log_level=0 udev.log_level=0 systemd.gpt_auto=0 panic=10 rd.emergency=reboot rd.shell=0 systemd.crash_reboot=1${EXTRA_CMDLINE:+ $EXTRA_CMDLINE}"
RECOVERY_CMDLINE="quiet loglevel=3 systemd.gpt_auto=0 panic=10${EXTRA_CMDLINE:+ $EXTRA_CMDLINE}"
BUILD="$(date -u +%Y%m%d-%H%M%S)-$ID"
. "$BS/boot-lib.sh"
echo "== install id $ID, build $BUILD"

echo "== slot system"
[ -f "$T/boot/vmlinuz-linux" ] || cp "$OUT/boot/vmlinuz-linux" "$T/boot/vmlinuz-linux"
rm -rf "$T/boot/loader" "$T/boot/EFI" "$T/boot/initramfs-linux-fallback.img"
arch-chroot "$T" pacman -S --needed --noconfirm plymouth > /dev/null
install -d "$T/usr/lib/anyps5" "$T/etc/anyps5" "$T/efi" "$T/data" "$T/usr/share/plymouth/themes/anyps5"
install -m 644 "$BS/boot-lib.sh" "$T/usr/lib/anyps5/boot-lib.sh"
install -m 755 "$BS/anyps5-update" "$BS/anyps5-boot-ok" "$BS/anyps5-data-prep" "$BS/anyps5-data-grow" "$T/usr/local/bin/"
install -m 644 "$BS"/units/*.service "$T/etc/systemd/system/"
echo "$BUILD" > "$T/etc/anyps5/v2"
arch-chroot "$T" systemctl disable anyps5-grow.service 2>/dev/null || true
rm -f "$T/etc/systemd/system/anyps5-grow.service" "$T/usr/local/bin/anyps5-grow"
arch-chroot "$T" systemctl enable anyps5-boot-ok.service anyps5-data-grow.service
for m in $MASK; do ln -sf /dev/null "$T/etc/systemd/system/$m"; done

install -d "$T/etc/systemd/system/getty@tty1.service.d" "$T/etc/systemd/system/plymouth-quit.service.d"
cat > "$T/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\u' --noclear --skip-login --noissue --autologin anyps5 %I $TERM
EOF
cat > "$T/etc/systemd/system/plymouth-quit.service.d/anyps5.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/plymouth quit --retain-splash
EOF

install -m 644 "$BS/plymouth/anyps5.plymouth" "$BS/plymouth/anyps5.script" "$T/usr/share/plymouth/themes/anyps5/"
if [ -f "$LOGO" ]; then install -m 644 "$LOGO" "$T/usr/share/plymouth/themes/anyps5/logo.png"; else echo "== no logo at $LOGO, splash is plain black"; fi
arch-chroot "$T" plymouth-set-default-theme anyps5
printf '[Daemon]\nTheme=anyps5\nShowDelay=0\nDeviceTimeout=8\n' > "$T/etc/plymouth/plymouthd.conf"

echo KEYMAP=us > "$T/etc/vconsole.conf"
install -d "$T/etc/mkinitcpio.conf.d"
cat > "$T/etc/mkinitcpio.conf.d/anyps5.conf" <<'EOF'
MODULES=(i915 bochs virtio_gpu)
HOOKS=(base systemd plymouth microcode modconf keyboard sd-vconsole block filesystems fsck)
COMPRESSION="zstd"
COMPRESSION_OPTIONS=(-6)
EOF
sed -i "s/^PRESETS=.*/PRESETS=('default')/" "$T/etc/mkinitcpio.d/linux.preset"

if [ -d "$T/home/$U" ]; then
  echo "== home becomes the seed for the data partition"
  printf '%s\n' '[ -z "$WAYLAND_DISPLAY$DISPLAY" ] && [ "$(tty)" = /dev/tty1 ] && { [ -n "$XDG_RUNTIME_DIR" ] && : > "$XDG_RUNTIME_DIR/anyps5-session-started"; exec /usr/local/bin/anyps5-session; }' > "$T/home/$U/.bash_profile"
  : > "$T/home/$U/.hushlogin"
  chown "$(stat -c %u:%g "$T/home/$U")" "$T/home/$U/.bash_profile" "$T/home/$U/.hushlogin"
  rm -rf "$T/usr/share/anyps5/seed"
  install -d "$T/usr/share/anyps5/seed"
  cp -a "$T/home" "$T/usr/share/anyps5/seed/home"
  find "$T/home" -mindepth 1 -delete
fi
[ -d "$T/usr/share/anyps5/seed/home/$U" ] || { echo "no seed home"; exit 1; }
echo "$BUILD" > "$T/usr/share/anyps5/seed/console-version"
aos_write_fstab "$T"

echo "== startup image (systemd, plymouth, no autodetect)"
arch-chroot "$T" mkinitcpio -P
ls -la "$T/boot/initramfs-linux.img"
install -d /var/cache/pacman/pkg
if compgen -G "$T/var/cache/pacman/pkg/*" > /dev/null; then mv -f "$T"/var/cache/pacman/pkg/* /var/cache/pacman/pkg/; fi
arch-chroot "$T" bash -c 'systemd-hwdb --usr update; ldconfig; journalctl --update-catalog; /usr/lib/systemd/systemd-update-done'
umount -R "$T" 2>/dev/null || true

echo "== recovery system"
if mountpoint -q "$R"; then umount -R "$R"; fi
rm -rf "$R"
mkdir -p "$R"
mount --bind "$R" "$R"
for i in 1 2 3 4; do
  pacstrap -c -K "$R" base linux linux-firmware-intel intel-ucode openssh networkmanager e2fsprogs dosfstools gptfdisk curl zstd nano > "$OUT/recovery-pacstrap.log" 2>&1 && break
  [ $i = 4 ] && { echo "== recovery packages failed"; tail -20 "$OUT/recovery-pacstrap.log"; exit 1; }
  echo "== recovery packages retry $i"; sleep 15
done
arch-chroot "$R" /bin/bash -e <<'CHROOT'
ln -sf /usr/share/zoneinfo/America/Phoenix /etc/localtime
echo anyps5-recovery > /etc/hostname
echo LANG=C.UTF-8 > /etc/locale.conf
echo KEYMAP=us > /etc/vconsole.conf
passwd -l root
systemctl enable NetworkManager sshd
CHROOT
install -d "$R/usr/lib/anyps5" "$R/etc/anyps5" "$R/efi" "$R/etc/mkinitcpio.conf.d" "$R/etc/systemd/system/getty@tty1.service.d" -m 755
install -d -m 700 "$R/root/.ssh"
install -m 600 "$T/root/.ssh/authorized_keys" "$R/root/.ssh/authorized_keys"
install -m 644 "$BS/boot-lib.sh" "$R/usr/lib/anyps5/boot-lib.sh"
install -m 755 "$BS/anyps5-update" "$BS/anyps5-recovery" "$R/usr/local/bin/"
echo "$BUILD" > "$R/etc/anyps5/recovery"
if compgen -G "$T/etc/NetworkManager/system-connections/*.nmconnection" > /dev/null; then
  install -d -m 700 "$R/etc/NetworkManager/system-connections"
  install -m 600 "$T"/etc/NetworkManager/system-connections/*.nmconnection "$R/etc/NetworkManager/system-connections/"
fi
install -d "$R/etc/NetworkManager/conf.d"
printf '[connection]\nwifi.powersave = 2\n' > "$R/etc/NetworkManager/conf.d/anyps5-wifi-powersave.conf"
cat > "$R/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\u' --noclear --skip-login --noissue --autologin root %I $TERM
EOF
printf '%s\n' '[ "$(tty)" = /dev/tty1 ] && exec /usr/local/bin/anyps5-recovery' > "$R/root/.bash_profile"
: > "$R/root/.hushlogin"
printf 'PARTUUID=%s /efi vfat rw,noatime,fmask=0137,dmask=0027,nofail 0 2\n' "$PART_ESP" > "$R/etc/fstab"
cat > "$R/etc/mkinitcpio.conf.d/anyps5.conf" <<'EOF'
MODULES=()
HOOKS=(base systemd microcode modconf keyboard sd-vconsole block filesystems fsck)
COMPRESSION="zstd"
COMPRESSION_OPTIONS=(-6)
EOF
sed -i "s/^PRESETS=.*/PRESETS=('default')/" "$R/etc/mkinitcpio.d/linux.preset"
for m in $MASK; do ln -sf /dev/null "$R/etc/systemd/system/$m"; done
arch-chroot "$R" mkinitcpio -P
if compgen -G "$R/var/cache/pacman/pkg/*" > /dev/null; then rm -f "$R"/var/cache/pacman/pkg/*; fi
arch-chroot "$R" bash -c 'systemd-hwdb --usr update; ldconfig; journalctl --update-catalog; /usr/lib/systemd/systemd-update-done'
umount -R "$R" 2>/dev/null || true

echo "== boot partition"
rm -rf "$E"
install -d "$E/EFI/BOOT" "$E/EFI/systemd" "$E/loader/entries" "$E/anyps5/a" "$E/anyps5/recovery"
cp "$T/usr/lib/systemd/boot/efi/systemd-bootx64.efi" "$E/EFI/BOOT/BOOTX64.EFI"
cp "$T/usr/lib/systemd/boot/efi/systemd-bootx64.efi" "$E/EFI/systemd/systemd-bootx64.efi"
cp "$T/boot/vmlinuz-linux" "$T/boot/initramfs-linux.img" "$E/anyps5/a/"
cp "$R/boot/vmlinuz-linux" "$R/boot/initramfs-linux.img" "$E/anyps5/recovery/"
cat > "$E/anyps5/layout.conf" <<EOF
ID="$ID"
BUILD="$BUILD"
PART_ESP="$PART_ESP"
PART_A="$PART_A"
PART_B="$PART_B"
PART_RECOVERY="$PART_RECOVERY"
PART_DATA="$PART_DATA"
CMDLINE="$CMDLINE"
RECOVERY_CMDLINE="$RECOVERY_CMDLINE"
PC_URL="$PC_URL"
EOF
AOS_EFI=$E
aos_write_loader_conf
aos_write_entry a 1 3 "AnyPS5 OS"
aos_write_recovery_entries
find "$E" -type f | sed "s#^$E##" | sort
du -sh "$E"

echo "== data partition"
rm -rf "$D"
install -d "$D/anyps5"
cp -a "$T/usr/share/anyps5/seed/home" "$D/home"
cp "$T/usr/share/anyps5/seed/console-version" "$D/home/$U/.anyps5-console-version"
chown "$(stat -c %u:%g "$D/home/$U")" "$D/home/$U/.anyps5-console-version"
echo "$(date -Is) image built $BUILD" > "$D/anyps5/boot-history.log"

echo "== file systems"
used=$(du -sm "$T" | cut -f1)
[ $(( used * 115 / 100 )) -lt "$SLOT_MB" ] || { echo "system is ${used} MB, too big for ${SLOT_MB} MB slots (set SLOT_MB)"; exit 1; }
rec_mb=$(( $(du -sm "$R" | cut -f1) * 13 / 10 + 256 ))
data_mb=$(( $(du -sm "$D" | cut -f1) + 512 ))
rm -rf "$P" "$IMG" "$IMG.sha256"
mkdir -p "$P"
mkfs.fat -C -F 32 -n "AOS${ID^^}E" "$P/1" $(( ESP_MB * 1024 )) > /dev/null
mcopy -s -i "$P/1" "$E"/* ::/
mkfs.ext4 -q -F -L "AOS-$ID-A" -d "$T" "$P/2" "${SLOT_MB}M"
mkfs.ext4 -q -F -L "AOS-$ID-B" "$P/3" "${SLOT_MB}M"
mkfs.ext4 -q -F -L "AOS-$ID-REC" -d "$R" "$P/4" "${rec_mb}M"
mkfs.ext4 -q -F -L "AOS-$ID-DATA" -d "$D" "$P/5" "${data_mb}M"

echo "== disk image: boot ${ESP_MB} MB, slots 2 x ${SLOT_MB} MB (system ${used} MB), recovery ${rec_mb} MB, data ${data_mb} MB (grows on first boot)"
truncate -s $(( (1 + ESP_MB + 2 * SLOT_MB + rec_mb + data_mb + 2) * 1024 * 1024 )) "$IMG"
sgdisk -Z "$IMG" > /dev/null
sgdisk -n1:2048:+${ESP_MB}M -t1:ef00 -c1:anyps5-esp -u1:"$PART_ESP" \
       -n2:0:+${SLOT_MB}M -t2:8304 -c2:anyps5-system-a -u2:"$PART_A" \
       -n3:0:+${SLOT_MB}M -t3:8304 -c3:anyps5-system-b -u3:"$PART_B" \
       -n4:0:+${rec_mb}M -t4:8300 -c4:anyps5-recovery -u4:"$PART_RECOVERY" \
       -n5:0:+${data_mb}M -t5:8300 -c5:anyps5-data -u5:"$PART_DATA" "$IMG" > /dev/null
for i in 1 2 3 4 5; do
  start=$(sgdisk -i $i "$IMG" | awk '/First sector/ {print $3}')
  dd if="$P/$i" of="$IMG" bs=4M seek=$(( start * 512 )) oflag=seek_bytes conv=notrunc,sparse status=none
done
rm -rf "$P"
sgdisk -v "$IMG" | tail -1
sgdisk -p "$IMG" | tail -6

echo "== update payload $TARBALL"
rm -f "$TARBALL"
tar "${AOS_TAR[@]}" -C "$T" -cf - . | zstd -T0 -3 -q -o "$TARBALL"
ls -la "$IMG" "$TARBALL"
sha256sum "$IMG" | tee "$IMG.sha256"
echo "== done $(date '+%F %T')"
