# AnyPS5 OS stick (Chromebook)

Makes a USB stick that boots the Chromebook (ASUS CX1400FKA, RW_LEGACY firmware, Ctrl+L) straight into AnyPS5 OS:
Arch Linux, autologin user `anyps5`, gamescope + Chromium kiosk showing the console, `os-host/host.py` on 127.0.0.1:8790.

1. Write the Arch ISO to the stick (PC, elevated): `python write_stick.py <iso> <disk#> "Verbatim" 28.9` (scratchpad copy; refuses any other disk).
2. Boot it on the Chromebook (Ctrl+L), `iwctl station wlan0 connect "<wifi>"`, then `curl -s 192.0.2.10:8099/a | sh` (file `a`, served by the Mac from ~/anyps5-stick-serve). It adds the PC key for root and starts sshd.
3. From the PC: copy `_tools\console` to `/root/console` on the live system, then run `install.sh` there. It erases only the USB disk whose model matches (default `STORE N GO`), copies the live Wi-Fi profile into NetworkManager, and installs everything.
4. Reboot without the Arch ISO: the stick boots AnyPS5 OS. SSH: `ssh -i ~/.ssh/id_ed25519_chromebook root@<chromebook ip>` (or `anyps5@`).

Logs on the stick: `~anyps5/.anyps5-host.log`, `~anyps5/.anyps5-session.log`. ChromeOS on the internal disk is never touched.

## Boot stack v2 (A/B slots, recovery, splash) - QEMU-tested, not on the device yet

`LAYOUT=v2 bash mkimage.sh` builds `/root/aos/anyps5-os-v2.img` plus the update payload `anyps5-os-rootfs.tar.zst` (`layout-v2.sh`, scripts in `boot/`). Without `LAYOUT=v2` the old single-slot image is built exactly as before. `REUSE_ROOT=1` skips the package install and reuses `/root/aos/root`.

- Partitions (GPT, PARTUUIDs and labels are new random values per build, kept in `ESP:/anyps5/layout.conf`, so a stick and the eMMC never clash): `anyps5-esp` (FAT, systemd-boot, kernels in `/anyps5/a`, `/anyps5/b`, `/anyps5/recovery`), `anyps5-system-a`, `anyps5-system-b` (8 GB each, `SLOT_MB`), `anyps5-recovery` (small Arch, text menu), `anyps5-data` (`/data`, `/home` is a bind of `/data/home`; grows to the end of the disk on first boot).
- Boot counting: entries are `anyps5-a+3.conf` style. `anyps5-boot-ok.service` waits for the console session (marker written by `~/.bash_profile`), then `systemd-bless-boot` drops the counter. If the session does not start in 90 s (`/etc/anyps5/boot-ok.conf TIMEOUT=`), a counted boot reboots; after 3 failures systemd-boot sorts the slot last and starts the other one. History: `/data/anyps5/boot-history.log`.
- `anyps5-update <rootfs.tar.zst | http URL | v2 .img | /dev/sdX stick>` formats the inactive slot, copies the system, keeps machine id, ssh host keys, Wi-Fi and Bluetooth, writes the slot's entry with a fresh counter and a higher `version`, so it is the default on the next boot (`default anyps5-*`, newest version first).
- Menu: hidden (`timeout 0`); hold Space while starting. Entries: AnyPS5 OS, AnyPS5 OS (previous), Recovery, Factory reset (keeps nothing). The reset entry only opens recovery's reset screen, which needs `ERASE` typed.
- Recovery (`anyps5-recovery`): reinstall from a USB stick/file or from the PC over HTTP, repair boot entries, Wi-Fi (nmtui), factory reset, shell; sshd on. It never mounts DATA except for a confirmed reset. CLI: `anyps5-recovery status|repair|reset`.
- Splash: plymouth theme `anyps5` (console `logo-mark.png`), quiet kernel arguments, `plymouth quit --retain-splash`, silent agetty autologin, `.hushlogin`.
- Test: `bash qemu-v2-test.sh` (OVMF, `CPU=Snowridge`, KVM) runs before/after timing, first boot, bad slot fallback, HTTP and stick updates, menu screenshot, recovery and reset. Logs and screenshots in `/root/aos/qemu` (`v2-summary.txt`). Build the test image with `EXTRA_CMDLINE="console=tty0 console=ttyS0,115200 plymouth.ignore-serial-consoles"` for a serial log; build the device image without it.
- `install-internal.sh` still copies a running single-slot system; it does not know v2 yet.

## Remote reboot

- `crossystem` (vboot_reference, built in WSL with `USE_FLASHROM=0`, static, kept in `bin/`, put in the image by `mkimage.sh` as `/usr/local/bin/crossystem`). Rebuild: clone vboot_reference, `make ARCH=x86_64 WERROR= USE_FLASHROM=0 $PWD/build/libvboot_util.a $PWD/build/utility/crossystem.o`, then `gcc -static -o crossystem build/utility/crossystem.o build/libvboot_util.a`. The stock `crossystem` target needs ChromiumOS flashrom; Arch's flashrom 1.8 lacks `flashrom_flash_getinfo`.
- Set 11-16: `crossystem dev_boot_altfw=1 dev_default_boot=altfw` (dev_boot_altfw was already 1). These go to CMOS (`/dev/nvram`) and read back correctly. The extra RW_NVRAM flash backup copy fails ("flashrom invocation failed", no flashrom). If CMOS is ever cleared, the firmware restores the old flash copy (`dev_default_boot=disk`), so set it again.
- With `altfw` as the default, the developer screen should start RW_LEGACY edk2 after 30 s, the same way Ctrl+L does. edk2 BootOrder is 0001 `USB: Verbatim STORE N GO`, 0000 Enter Setup, 0002 Internal eMMC, so the stick boots first. BootCurrent was 0001 and efivarfs is mounted rw. Nobody has watched a boot with no key press yet.
- Reboot when the kernel is wedged: `echo 1 > /proc/sys/kernel/sysrq; sync; echo s > /proc/sysrq-trigger; echo b > /proc/sysrq-trigger`. Do it while ssh still works: once ext4/jbd2 on the stick stalls, ssh logs in but hangs opening the session, and only a hard power-off helps (hold power about 10 s).
- Upload over ssh from Git Bash with `cat file | ssh ... 'cat > dst'`. `scp` and `ssh ... < file` hang after 0 bytes.

Display notes (11-16, ASUS CX1400 / Jasper Lake): gamescope 3.17 logs "drmModeAddFB2WithModifiers failed" twice and at first showed nothing. With INTEL_DEBUG=noccs and a session restart it drew Chromium. Chromium's first start on a fresh profile showed a white page (no page target); the second start loaded the console. Chromium's DevTools port 9222 (localhost only) lists what it shows: `curl -s 127.0.0.1:9222/json`. Restart the session as root: `kill -9 $(pgrep -x gamescope-wl)` (pkill -f with a pattern that is also in your ssh command kills that ssh too).

- Firmware (boot logo, no developer screen, direct UEFI boot): plan, backup, flashing and restore steps in `FIRMWARE.md`; build with `firmware\build-firmware.ps1` (PC lock) or `firmware/build-firmware.sh` in WSL. Nothing is flashed by any script here.
