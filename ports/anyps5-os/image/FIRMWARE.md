# AnyPS5 firmware for the Chromebook (coreboot + edk2 UEFI)

The AnyPS5 OS has its own kernel, splash and session. Everything before the kernel is still Google's: the ChromeOS
firmware, the "OS verification is off" screen, the beep, Ctrl+L into MrChromebox's RW_LEGACY payload, and only then
systemd-boot. This page is the plan for replacing all of that with one firmware, coreboot + edk2 (UEFI), carrying the
AnyPS5 logo. Nothing on this page has been flashed. Every write step below is for Blu to do by hand.

## Today's boot chain and the new one

| | Today | With the AnyPS5 firmware |
|---|---|---|
| Power on | ChromeOS coreboot (RO, write-protected) | coreboot (MrChromebox fork) |
| Screen | "OS verification is off" developer screen, 30 s or Ctrl+L | AnyPS5 logo (edk2 bootsplash), no warning, no beep |
| Payload | RW_LEGACY edk2 (dev_default_boot=altfw) | Full edk2 UEFI, the only payload |
| Boot | systemd-boot from the stick's removable path | systemd-boot from the UEFI boot order: stick or internal disk |
| Settings | GBB flags, crossystem | UEFI setup (Esc at the logo): boot order, boot menu; UEFI variables are kept in SMMSTORE |

What the AnyPS5 firmware adds:

- AnyPS5 logo from the first frame, then the plymouth AnyPS5 splash. One continuous boot, no Google screens.
- No developer-mode screen, no beep, no key presses, no 30 s timeout, and nobody can wipe the machine by pressing
  Space at the dev screen ("return to verified mode").
- Direct boot: the first UEFI boot entry boots right away. `bootctl install` and `efibootmgr` work for real, because
  UEFI variables persist (today only the removable path `BOOTX64.EFI` works).
- A standard PC firmware: ACPI, UEFI GOP, any x86_64 Linux or Windows can boot.

What is lost:

- ChromeOS stops booting. The ChromeOS install on the eMMC becomes unusable; the AnyPS5 OS then lives on the stick or,
  later, on the eMMC (`install-internal.sh`).
- Google verified boot, ChromeOS firmware updates and the Esc+Refresh+Power recovery screen are gone. After this, the
  way back is flashing (the script, or an external programmer).

## Is this board supported?

Yes. docs.mrchromebox.tech/docs/supported-devices.html, Intel Jasper Lake section:

| Device | Board | RW_LEGACY | UEFI Full ROM | WP method |
|---|---|---|---|---|
| Asus Chromebook CX1400FKA | GALTIC360 | yes | yes | CR50 (SuzyQ, battery) |

Read from the machine on 2026-11-16 over ssh (read-only):

- Firmware `Google_Galtic.13606.646.0`, DMI product and board `Galtic`. The security chip is a CR50 (H1), not a Ti50,
  so the battery method works.
- SPI flash: 16 MiB on the Jasper Lake SPI controller (8086:4da4). Layout from the backup: descriptor 0x0-0xFFF, Intel
  ME (CSE) 0x1000-0x380FFF, BIOS 0x381000-0xFFFFFF. Inside the BIOS region: RW_LEGACY (MrChromebox's payload, 1 MiB),
  RW_MRC_CACHE, RW_VPD, and the write-protected WP_RO (0xC00000-0xFFFFFF, 4 MiB) with RO_VPD, GBB and the RO firmware.
- Write protect is ON: `flashrom --wp-status` prints `Protection range: start=0x00c00000 length=0x00400000 (upper 1/4)`
  and `Protection mode: hardware`; the WP pin (ACPI `GPIO.1`, type 3 = write protect, controller INT34C8 pin 267)
  reads `active`.
- The AnyPS5 OS kernel binds `spi_intel_pci` to the SPI controller, so flashrom uses the Linux MTD path (`/dev/mtd0`)
  and says "Chipset specific features ... are not available on this path". Reading works on that path. For writing,
  unload the driver first (step 6).

## Risks

- A failed or interrupted write leaves a machine that does not power on to anything. The only fixes then are an
  external SPI programmer (CH341A with a SOIC-8 clip) or, if CCD was opened beforehand (step 3), a SuzyQable. Without
  one of those, a bad flash means a dead Chromebook until one is bought.
- The battery is unplugged while flashing, so the charger is the only power. A pulled cable during the 90 s write is a
  bad flash. Use a solid 45 W or stronger USB-C PD charger and do not touch the cable.
- Opening the case: unplug the charger first, touch something grounded, pull the battery connector straight out by its
  plastic housing, never by the wires. Do not tug ribbon cables.
- The AnyPS5-branded ROM is a local build of MrChromebox's sources plus a logo. MrChromebox's own release ROM for this
  board is tested by many users; the AnyPS5 build is not. That is why the plan flashes MrChromebox's release first and
  the AnyPS5 build second, with a working backup and (ideally) a programmer at hand.
- ChromeOS on the eMMC stops booting (see above). Data on the eMMC is not touched by flashing, but it is not reachable
  from ChromeOS any more.
- Windows only: GALTIC needs the touchpad type written to SSFC (script option `T`) or the touchpad will not work.
  Linux does not care.

## Step by step

### 0. Have ready

- The PC, the AnyPS5 OS stick, a second USB stick (FAT32) for copies of the backup.
- A 45 W or stronger USB-C PD charger (MrChromebox's minimum for the battery method).
- A small Phillips screwdriver and a plastic pry tool.
- Insurance, required for 6b (the AnyPS5 ROM) and strongly recommended for 6a: a SuzyQable (USB-C debug cable for CR50, used in step 3) or a CH341A programmer with
  a SOIC-8 clip and a 1.8 V adapter (check the flash chip's marking and datasheet for its voltage before clipping on).

### 1. Back up the stock firmware (mandatory, works with WP on)

A backup was taken on 2026-11-16 and is on the PC:

```
C:\AnyPS5 Games\_tools\anyps5-os-stick\firmware\backup\BACKUP-galtic-Google_Galtic.13606.646.0-20261116.rom
sha256 a6668c70c91a290817849d7a7fdb821828dd9c99acf3448ba770432da02aaad7
```

It is the whole 16 MiB chip (descriptor, ME and BIOS regions all readable), read twice with identical hashes. Take a
fresh one right before flashing anyway, because the RW areas (MRC cache, VPD) change. On the Chromebook in the
AnyPS5 OS:

```
pacman -S --needed flashrom
mkdir -p /root/fw-backup && cd /root/fw-backup
flashrom -p internal -r a.rom
flashrom -p internal -r b.rom
sha256sum a.rom b.rom
```

Both hashes must match. Then on the PC:

```
scp -o UserKnownHostsFile=~/.ssh/known_hosts_anyps5os -i ~/.ssh/id_ed25519_chromebook root@192.0.2.10:/root/fw-backup/a.rom "C:\AnyPS5 Games\_tools\anyps5-os-stick\firmware\backup\BACKUP-galtic-<date>.rom"
```

Keep one more copy off the PC (cloud drive or the FAT32 stick). The firmware script in step 5 also offers its own
backup; say yes and save it to the USB stick too.

### 2. Optional second fallback: a ChromeOS recovery stick

On any computer with Chrome, the Chromebook Recovery Utility makes a recovery stick for "ASUS Chromebook Flip CX1400"
(board galtic). The firmware script's "Restore Stock Firmware" can pull stock firmware out of that stick if the backup
is ever lost.

### 3. Optional insurance: open CCD with a SuzyQable (only possible before step 5)

This makes unbricking over USB possible without opening the case again. It needs ChromeOS, which still boots today:
at the developer screen press Ctrl+D, then Ctrl+Alt+F2 (VT2), log in as `chronos`.

Before starting: this step leaves and re-enters developer mode, and each switch wipes the ChromeOS user data on the
eMMC. Copy anything you still want out of ChromeOS (Downloads, Files) to a USB stick first.

```
sudo gsctool -a -o
```

Press the power button whenever the screen says "Press PP button now!", until "PP Done!"; the machine reboots and
leaves developer mode, so turn developer mode back on (Esc+Refresh+Power, Ctrl+D) and return to VT2. With the SuzyQ in
the debug USB-C port:

```
ls /dev/ttyUSB*
echo "wp disable" > /dev/ttyUSB0
echo "wp disable atboot" > /dev/ttyUSB0
echo "ccd reset factory" > /dev/ttyUSB0
sudo gsctool -a -w
```

The last command must say `Flash WP: forced disabled` (and `at boot: forced disabled`). If this step is done, step 4
(opening the case) is not needed.

Re-entering developer mode resets the boot settings, so the AnyPS5 stick would no longer start from the developer
screen. Still in VT2, turn them back on before leaving ChromeOS:

```
sudo crossystem dev_boot_altfw=1 dev_boot_usb=1 dev_default_boot=altfw
crossystem dev_boot_altfw dev_default_boot
```

The second line must print `1 altfw`.

Prove the cable works before relying on it (step 6 needs this): from a Linux machine with flashrom (the MacBook, or
WSL after attaching the cable with `usbipd attach --wsl`), read the chip twice and compare:

```
flashrom -p raiden_debug_spi:target=AP -r suzyq-a.rom
flashrom -p raiden_debug_spi:target=AP -r suzyq-b.rom
sha256sum suzyq-a.rom suzyq-b.rom
```

Both hashes must match each other. They will not match the step 1 backup exactly (the RW areas change), that is fine. Opening CCD resets developer mode and wipes ChromeOS user data, which is fine here.
After step 5 the ChromeOS `gsctool` is gone, so this step can only happen before.

### 4. Turn hardware write protect off (battery method)

1. Shut down. Unplug the charger.
2. Turn the Chromebook over, remove the bottom-cover screws (look under the rubber feet too), pry the cover off from
   the hinge side.
3. Unplug the battery cable from the mainboard. Leave it unplugged.
4. Put the cover back loosely (two screws are enough).
5. Plug in the 45 W charger and press power. With the battery gone, CR50 drops hardware write protect.

### 5. Check that write protect is really off

In the AnyPS5 OS (boot the stick as usual):

```
gpioget --as-is -c gpiochip0 267
```

`"267"=inactive` means the write-protect pin is off. Today, with the battery in, it reads `"267"=active`. (`--as-is`
only reads; pacman package `libgpiod`.)

```
flashrom -p internal --wp-status
```

This shows the flash chip's status-register setting, not the pin. It keeps saying `Protection mode: hardware` with
range `upper 1/4` until the firmware script clears software write protect; that clear only succeeds when the pin is
off. After step 6 it should say the protection is disabled with an empty range. If the script refuses with a
write-protect error, the pin is still on: go back to step 4.

In ChromeOS VT2 (only before step 6): `crossystem wpsw_cur` must print `0`.

### 6. Flash MrChromebox's UEFI Full ROM, then the AnyPS5 ROM

Do everything in one sitting, battery still unplugged, charger in.

6a. MrChromebox's release (tested ROM, keeps serial, HWID and VPD). Run it from ChromeOS VT2 (Ctrl+D at the developer
screen, Ctrl+Alt+F2, user `chronos`), the environment the script is made for:

```
cd; curl -LOf https://mrchromebox.tech/firmware-util.sh && sudo bash firmware-util.sh
```

- The header must show write protect disabled.
- Choose `2) Install/Update UEFI (Full ROM) Firmware`. Accept the backup offer and save it to the FAT32 stick.
- Let it finish ("may take up to 90s"). If it prints an error: do not reboot, do not power off; re-run the option
  or restore the backup from the same menu.
- Reboot from the script (`R`). The first boot can take 30 s or more (memory training). Then the edk2 screen appears.
  A black screen is normal for up to a few minutes on this first boot: wait 5 minutes before holding power, and do not
  power-cycle over and over. If nothing ever appears, the machine needs the step 7 "does not boot" path.

Stop here unless a recovery path is proven. 6a is MrChromebox's tested release and already removes the developer
screen, the beep and the 30 s wait; it lacks only the AnyPS5 logo. 6b writes a ROM that has never booted on any
machine, so only go on when one of these is true:

- step 3 was done and the SuzyQ double read matched, or
- a CH341A with a SOIC-8 clip (and 1.8 V adapter if the chip needs it) read this chip twice with matching hashes.

Otherwise stay on 6a; that is a finished, working setup.

6b. The AnyPS5 build. First back up the working 6a firmware, so a bad 6b can be undone without losing anything: boot
the AnyPS5 OS stick (Esc at the logo opens the boot menu), then

```
modprobe -r spi_intel_pci spi_intel
mkdir -p /root/fw-backup && cd /root/fw-backup
flashrom -p internal -r uefi-a.rom
flashrom -p internal -r uefi-b.rom
sha256sum uefi-a.rom uefi-b.rom
```

The two hashes must match; copy `uefi-a.rom` to the PC and to the FAT32 stick as
`BACKUP-galtic-mrchromebox-uefi-<date>.rom`. Copy the ROM from `firmware\out\` (see "Build" below) to the FAT32 stick
and check its sha256 against `BUILD-INFO.txt`. Then, with the driver still unloaded:

```
cd; curl -LOf https://mrchromebox.tech/firmware-util.sh && sudo bash firmware-util.sh
```

The `modprobe -r` hands the SPI controller back to flashrom's own chipset driver. In the script pick its custom
firmware option (named "Flash Custom Firmware" or similar; numbers change between script versions, so read the menu),
flash from the USB device, and pick the AnyPS5 ROM. The script is meant to copy serial number, HWID, VPD, RW_MRC_CACHE
and SMMSTORE from the running firmware into the custom ROM before writing; read its output and stop if it does not
say so. If this script version has no custom firmware option, stop and stay on 6a: never write the ROM by hand with
`flashrom -w`, which skips those copies and the safety checks. If the write reports an error: do not reboot or power
off, write `uefi-a.rom` back with the script's restore option (or the same custom option), and only then reboot.
Reboot. The AnyPS5 logo shows at power-on.

6c. Finish: shut down, unplug the charger, plug the battery back in, screw the cover on, plug the charger in again to
wake it. Write protect comes back with the battery, but software protection stays cleared, so later updates (new
AnyPS5 ROM builds) only need step 4 again if the script asks for it.

6d. Make the boot permanent: in UEFI setup (Esc at the logo) put the AnyPS5 stick (or, after `install-internal.sh`,
the eMMC) first.

### 7. Going back to stock

With a working machine (UEFI firmware boots): battery out (step 4), boot the AnyPS5 OS stick, `modprobe -r
spi_intel_pci spi_intel`, run the script, choose `Restore Stock Firmware`, point it at the backup on the USB stick
(the most exact restore, serial number included). Battery back in. ChromeOS on the eMMC may boot again as is;
otherwise boot the recovery stick from step 2 and reinstall ChromeOS.

With a machine that does not boot (bad flash):

- SuzyQ (only if step 3 was done), from the MacBook or WSL (`usbipd attach --wsl`) with flashrom, cable in the
  Chromebook's debug port. If 6a worked and only 6b failed, write the 6a backup to get the UEFI firmware back; write the
  stock backup to get ChromeOS firmware back:

  ```
  flashrom -p raiden_debug_spi:target=AP -w BACKUP-galtic-mrchromebox-uefi-<date>.rom
  flashrom -p raiden_debug_spi:target=AP -w BACKUP-galtic-<date>.rom
  ```

- CH341A: battery unplugged, charger unplugged, clip on the SPI flash chip (pin 1 to the dot, 1.8 V adapter if the
  chip is 1.8 V):

  ```
  flashrom -p ch341a_spi -r check.rom
  flashrom -p ch341a_spi -w BACKUP-galtic-<date>.rom
  ```

  Read once first and compare against a second read to make sure the clip sits right before writing.

## Build (AnyPS5-branded ROM)

Sources, all official: MrChromebox's coreboot fork (`https://github.com/MrChromebox/coreboot`, branch
`MrChromebox-2609`, the branch MrChromebox's releases are built from), its submodules (MrChromebox/blobs has the
GALTIC descriptor, ME, FSP, VBT and EC binaries extracted from Google's firmware; coreboot's fsp and intel-microcode),
and MrChromebox's edk2 (`https://github.com/mrchromebox/edk2`, `uefipayload_2608`, fetched by the coreboot build). The
only change from the release config `configs/jsl/config.galtic.uefi` is
`CONFIG_EDK2_BOOTSPLASH_FILE` pointing at the AnyPS5 logo (`console-native\assets\logo-mark.png`, white mark on
black, 480x480 BMP3).

Run on the PC (waits for the PC lock, priority 80, refreshes it every minute):

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\AnyPS5 Games\_tools\anyps5-os-stick\firmware\build-firmware.ps1"
```

or directly in WSL Arch:

```
bash "/mnt/c/AnyPS5 Games/_tools/anyps5-os-stick/firmware/build-firmware.sh"
```

What it does: installs the pacman build packages, clones the fork into `/root/fw/coreboot`, checks out the blobs, fsp,
intel-microcode and vboot submodules, builds coreboot's cross toolchain once (`make crossgcc-i386`, `make iasl`),
writes `configs/anyps5/config.galtic_anyps5.uefi`, runs MrChromebox's `build-uefi.sh galtic_anyps5`, checks the image
is 16 MiB and prints its FMAP, then copies the ROM, config, bootsplash and `BUILD-INFO.txt` (sha256, coreboot, edk2
and blobs commits) to `C:\AnyPS5 Games\_tools\anyps5-os-stick\firmware\out\`. Logs: `out\build.log`,
`out\build.log.err`, `out\status.txt`. Other settings: `BOARD`, `BRANCH`, `LOGO`, `OUT`, `JOBS` environment variables.

### Build of 2026-11-16

```
C:\AnyPS5 Games\_tools\anyps5-os-stick\firmware\out\coreboot_edk2-galtic_anyps5-mrchromebox_20261116.rom
sha256 1ba651c25478e1a249e15dcfb3099bfe42cefc5c2b6a7bc2574376cab17477bf
16777216 bytes
coreboot MrChromebox-2609.3-1-g771cde3579 (771cde3579cc1bec8bad54c23267894c6a1b2d29)
edk2     6c14abf865e093452edc775651df61775bf99c9f (uefipayload_2608)
blobs    feaa3e9082c397beab4037bd756a2a4513d75bb3
```

Checks done: "Built google/dedede (Galtic)"; edk2 built with `BOOTSPLASH_IMAGE=TRUE` from the AnyPS5 BMP; the image is
16 MiB; its flash descriptor has the same regions as the stock backup (descriptor 0x0-0xFFF, ME 0x1000-0x380FFF, BIOS
0x381000-0xFFFFFF), so the script's BIOS-region write fits; FMAP has SI_DESC, SI_ME, RW_MRC_CACHE, SMMSTORE, RO_VPD,
FMAP and COREBOOT (ramstage, FSP-M/S, VBT, EC RW, microcode, edk2 payload).

Not checked: booting it. A Jasper Lake board ROM cannot run in QEMU (FSP needs the real chip), so the first real test
is step 6b on the Chromebook. MrChromebox's own release ROM for GALTIC is built from the same branch, config and blobs;
the AnyPS5 build differs only in the bootsplash image (and the edk2 payload it is compiled into).
