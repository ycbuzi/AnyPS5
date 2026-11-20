#!/bin/bash
set -euo pipefail
OUT=${OUT:-/root/aos}
Q=$OUT/qemu
mkdir -p "$Q"
pacman -S --needed --noconfirm qemu-base edk2-ovmf > /dev/null
rm -f "$Q/overlay.qcow2" "$Q/serial.log"
qemu-img create -q -f qcow2 -b "$OUT/anyps5-os.img" -F raw "$Q/overlay.qcow2"
ACC=tcg; [ -e /dev/kvm ] && ACC=kvm
echo "accel=$ACC"
timeout ${SECS:-420} qemu-system-x86_64 -machine q35,accel=$ACC -cpu ${CPU:-max} -m 3072 -smp 4 \
  -drive file="$Q/overlay.qcow2",if=virtio,format=qcow2 \
  -kernel "$OUT/boot/vmlinuz-linux" -initrd "$OUT/boot/initramfs-linux.img" \
  -append "root=LABEL=ANYPS5 rw console=ttyS0 systemd.log_target=console" \
  -netdev user,id=n,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net,netdev=n \
  -display none -serial file:"$Q/serial.log" -daemonize -pidfile "$Q/qemu.pid"
echo "qemu started pid $(cat $Q/qemu.pid)"
