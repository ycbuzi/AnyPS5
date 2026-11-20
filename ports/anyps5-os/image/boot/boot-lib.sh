AOS_EFI=${AOS_EFI:-/efi}
AOS_TAR=(--numeric-owner --xattrs --xattrs-include='*' --acls)
AOS_VARS=/sys/firmware/efi/efivars
AOS_LOADER_GUID=4a67b082-0a4c-41cf-b6c7-440b29bb8c4f

aos_mount_efi() {
  mountpoint -q "$AOS_EFI" || mount "$AOS_EFI" 2>/dev/null || true
}

aos_layout() {
  [ -f "$AOS_EFI/anyps5/layout.conf" ] || { echo "no $AOS_EFI/anyps5/layout.conf" >&2; return 1; }
  . "$AOS_EFI/anyps5/layout.conf"
}

aos_partuuid() {
  case $1 in
    a) echo "$PART_A" ;;
    b) echo "$PART_B" ;;
    esp) echo "$PART_ESP" ;;
    recovery) echo "$PART_RECOVERY" ;;
    data) echo "$PART_DATA" ;;
  esac
}

aos_dev() {
  echo "/dev/disk/by-partuuid/$(aos_partuuid "$1")"
}

aos_other() {
  if [ "$1" = a ]; then echo b; else echo a; fi
}

aos_current_slot() {
  sed -n 's/.*anyps5\.slot=\([ab]\).*/\1/p' /proc/cmdline
}

aos_entry_file() {
  local f
  for f in "$AOS_EFI/loader/entries/anyps5-$1.conf" "$AOS_EFI/loader/entries/anyps5-$1+"*.conf; do
    [ -f "$f" ] && { echo "$f"; return 0; }
  done
  return 0
}

aos_tries_left() {
  basename "$1" .conf | sed -n 's/.*+\([0-9]\+\)\(-[0-9]\+\)\?$/\1/p'
}

aos_entry_is_bad() {
  [ "$(aos_tries_left "$1")" = 0 ]
}

aos_entry_version() {
  local f
  f=$(aos_entry_file "$1")
  if [ -n "$f" ]; then sed -n 's/^version[[:space:]]\+//p' "$f" | head -1; else echo 0; fi
}

aos_max_version() {
  local a b
  a=$(aos_entry_version a); b=$(aos_entry_version b)
  a=${a:-0}; b=${b:-0}
  if [ "$a" -gt "$b" ]; then echo "$a"; else echo "$b"; fi
}

aos_set_title() {
  grep -qx "title $2" "$1" || sed -i "s/^title .*/title $2/" "$1"
}

aos_retitle() {
  local f o
  f=$(aos_entry_file "$1")
  [ -n "$f" ] && aos_set_title "$f" "AnyPS5 OS"
  o=$(aos_entry_file "$(aos_other "$1")")
  if [ -n "$o" ]; then
    if aos_entry_is_bad "$o"; then aos_set_title "$o" "AnyPS5 OS (failed update)"; else aos_set_title "$o" "AnyPS5 OS (previous)"; fi
  fi
  return 0
}

aos_write_entry() {
  local slot=$1 version=$2 tries=$3 title=$4 f
  rm -f "$AOS_EFI/loader/entries/anyps5-$slot.conf" "$AOS_EFI/loader/entries/anyps5-$slot+"*.conf
  f="$AOS_EFI/loader/entries/anyps5-$slot${tries:++$tries}.conf"
  {
    echo "title $title"
    echo "sort-key anyps5"
    echo "version $version"
    echo "linux /anyps5/$slot/vmlinuz-linux"
    echo "initrd /anyps5/$slot/initramfs-linux.img"
    echo "options root=PARTUUID=$(aos_partuuid "$slot") rw anyps5.slot=$slot $CMDLINE"
  } > "$f.new"
  mv -f "$f.new" "$f"
}

aos_write_loader_conf() {
  install -d "$AOS_EFI/loader/entries"
  cat > "$AOS_EFI/loader/loader.conf" <<EOF
default anyps5-*
timeout 0
editor no
auto-entries no
auto-firmware no
console-mode keep
EOF
}

aos_write_recovery_entries() {
  install -d "$AOS_EFI/loader/entries"
  cat > "$AOS_EFI/loader/entries/recovery.conf" <<EOF
title Recovery
sort-key anyps5-recovery
linux /anyps5/recovery/vmlinuz-linux
initrd /anyps5/recovery/initramfs-linux.img
options root=PARTUUID=$PART_RECOVERY rw anyps5.recovery=menu $RECOVERY_CMDLINE
EOF
  cat > "$AOS_EFI/loader/entries/reset.conf" <<EOF
title Factory reset (keeps nothing)
sort-key anyps5-reset
linux /anyps5/recovery/vmlinuz-linux
initrd /anyps5/recovery/initramfs-linux.img
options root=PARTUUID=$PART_RECOVERY rw anyps5.recovery=reset $RECOVERY_CMDLINE
EOF
}

aos_write_fstab() {
  cat > "$1/etc/fstab" <<EOF
PARTUUID=$PART_ESP /efi vfat rw,noatime,fmask=0137,dmask=0027,nofail 0 2
PARTUUID=$PART_DATA /data ext4 rw,noatime,commit=60 0 2
/data/home /home none bind,x-systemd.requires=anyps5-data.service 0 0
EOF
}

aos_carry_state() {
  local from=$1 to=$2 f
  for f in etc/machine-id etc/hostname etc/localtime etc/adjtime root/.ssh/authorized_keys; do
    if [ -e "$from/$f" ] || [ -L "$from/$f" ]; then install -d "$to/$(dirname "$f")"; rm -f "$to/$f"; cp -a "$from/$f" "$to/$f"; fi
  done
  for f in "$from"/etc/ssh/ssh_host_*; do [ -f "$f" ] && cp -a "$f" "$to/etc/ssh/"; done
  for f in etc/NetworkManager/system-connections var/lib/bluetooth; do
    if [ -d "$from/$f" ]; then rm -rf "${to:?}/$f"; install -d "$to/$(dirname "$f")"; cp -a "$from/$f" "$to/$f"; fi
  done
  return 0
}

aos_decompress() {
  case "$1" in
    *.zst) zstd -dc ;;
    *.gz|*.tgz) gzip -dc ;;
    *.xz) xz -dc ;;
    *) cat ;;
  esac
}

aos_untar() {
  aos_decompress "$1" | tar "${AOS_TAR[@]}" -xpf - -C "$2"
}

aos_find_part() {
  local p
  for p in $(lsblk -lnpo NAME "$1" | tail -n +2); do
    [ "$(blkid -o value -s PARTLABEL "$p" 2>/dev/null)" = "$2" ] && { echo "$p"; return 0; }
  done
  return 0
}

aos_newest_slot() {
  local s best="" bestv=-1 v f
  for s in a b; do
    f=$(aos_entry_file "$s")
    [ -n "$f" ] || continue
    aos_entry_is_bad "$f" && continue
    v=$(aos_entry_version "$s"); v=${v:-0}
    if [ "$v" -gt "$bestv" ]; then best=$s; bestv=$v; fi
  done
  echo "${best:-a}"
}

aos_copy_from_disk() {
  local src=$1 new=$2 w=$3 dev loop="" esp slot part rc=0
  dev=$src
  if [ -f "$src" ]; then loop=$(losetup -rPf --show "$src"); dev=$loop; fi
  udevadm settle 2>/dev/null || true
  install -d "$w/srcesp" "$w/src"
  esp=$(aos_find_part "$dev" anyps5-esp)
  slot=a
  if [ -n "$esp" ] && mount -o ro "$esp" "$w/srcesp"; then
    slot=$(AOS_EFI="$w/srcesp" aos_newest_slot)
    umount "$w/srcesp"
  fi
  part=$(aos_find_part "$dev" "anyps5-system-$slot")
  if [ -z "$part" ]; then
    echo "no anyps5-system-$slot partition on $src (not an AnyPS5 OS v2 disk)" >&2
    rc=1
  else
    echo "== source $part (slot $slot of $src)"
    mount -o ro "$part" "$w/src" && { tar "${AOS_TAR[@]}" -C "$w/src" -cf - . | tar "${AOS_TAR[@]}" -xpf - -C "$new" || rc=1; umount "$w/src"; } || rc=1
  fi
  [ -n "$loop" ] && losetup -d "$loop"
  return $rc
}

aos_pick_slot() {
  local fa fb va vb
  fa=$(aos_entry_file a); fb=$(aos_entry_file b)
  [ -z "$fa" ] && { echo a; return; }
  [ -z "$fb" ] && { echo b; return; }
  aos_entry_is_bad "$fa" && { echo a; return; }
  aos_entry_is_bad "$fb" && { echo b; return; }
  va=$(aos_entry_version a); vb=$(aos_entry_version b)
  if [ "${va:-0}" -le "${vb:-0}" ]; then echo a; else echo b; fi
}

aos_boot_count_path() {
  local f="$AOS_VARS/LoaderBootCountPath-$AOS_LOADER_GUID"
  [ -r "$f" ] || return 0
  tail -c +5 "$f" | iconv -f UTF-16LE -t UTF-8 2>/dev/null | tr -d '\000' | tr '\\' '/'
}

aos_should_reboot() {
  local left cur o f
  [ -n "$1" ] || return 1
  left=$(aos_tries_left "$1")
  [ -n "$left" ] || return 1
  [ "$left" -gt 0 ] && return 0
  cur=$(aos_current_slot)
  [ -n "$cur" ] || return 1
  o=$(aos_other "$cur")
  f=$(aos_entry_file "$o")
  [ -n "$f" ] && ! aos_entry_is_bad "$f"
}
