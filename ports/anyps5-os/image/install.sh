#!/bin/bash
set -euo pipefail
DEV=${1:-/dev/sda}
T=${TARGET:-/mnt}
MODEL=${2:-STORE N GO}
U=anyps5

[ -n "${TARGET:-}" ] || [ "$(lsblk -dno MODEL "$DEV" | xargs)" = "$MODEL" ] || { echo "refusing: $DEV is not '$MODEL'"; exit 1; }
[ -n "${TARGET:-}" ] || [ "$(lsblk -dno TRAN "$DEV" | xargs)" = usb ] || { echo "refusing: $DEV is not USB"; exit 1; }
if [ -z "${TARGET:-}" ] && [ "${RESUME:-0}" != 1 ] && lsblk -nro MOUNTPOINT "$DEV" | grep -q .; then echo "refusing: $DEV is mounted"; exit 1; fi
[ -d "${CONSOLE_SRC:-/root/console}/os-host" ] || { echo "missing /root/console"; exit 1; }

iw dev wlan0 set power_save off 2>/dev/null || true
if [ -n "${TARGET:-}" ]; then
  echo "== building into $TARGET"
  mkdir -p "$TARGET"
elif [ "${RESUME:-0}" = 1 ] && [ "$(lsblk -no LABEL "${DEV}2" 2>/dev/null)" = ANYPS5 ]; then
  echo "== resume on $DEV"
  findmnt -n /mnt >/dev/null || mount "${DEV}2" /mnt
  findmnt -n /mnt/boot >/dev/null || mount --mkdir "${DEV}1" /mnt/boot
else
  echo "== partition $DEV"
  for p in "$DEV"?*; do [ -b "$p" ] && wipefs -a "$p" || true; done
  wipefs -a "$DEV"
  sgdisk -Z "$DEV"
  sgdisk -n1:0:+512M -t1:ef00 -c1:ANYPS5-EFI -n2:0:0 -t2:8304 -c2:ANYPS5 "$DEV"
  partprobe "$DEV"; udevadm settle
  mkfs.fat -F32 -n APS5EFI "${DEV}1"
  mkfs.ext4 -F -q -L ANYPS5 "${DEV}2"
  mount "${DEV}2" /mnt
  mount --mkdir "${DEV}1" /mnt/boot
fi

echo "== packages"
grep -q '^Server' /etc/pacman.d/mirrorlist && [ "$(grep -c '^Server' /etc/pacman.d/mirrorlist)" -ge 4 ] || \
  reflector --country US --protocol https --age 12 --latest 20 --fastest 8 --save /etc/pacman.d/mirrorlist || true
grep -q '^Server' /etc/pacman.d/mirrorlist || echo 'Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch' > /etc/pacman.d/mirrorlist
sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 3/' /etc/pacman.conf
for i in 1 2 3 4 5 6 7 8; do
  pacman -Sy --noconfirm --needed archlinux-keyring && pacstrap -K $T base linux linux-firmware intel-ucode sof-firmware alsa-ucm-conf alsa-utils rtkit \
    networkmanager bluez bluez-utils pipewire pipewire-pulse pipewire-alsa wireplumber \
    mesa vulkan-intel vulkan-tools gamescope chromium python openssh sudo nano ffmpeg polkit \
    noto-fonts noto-fonts-emoji ttf-dejavu sdl3 sdl3_ttf sdl3_image && break
  [ $i = 8 ] && { echo "== packages failed 8 times"; exit 1; }
  echo "== packages retry $i"; sleep 20
done
if [ -n "${TARGET:-}" ]; then
  printf 'LABEL=ANYPS5 / ext4 rw,noatime,commit=60 0 1
LABEL=APS5EFI /boot vfat rw,relatime,fmask=0022,dmask=0022 0 2
' > $T/etc/fstab
else
  grep -q 'LABEL=ANYPS5\|UUID=' $T/etc/fstab 2>/dev/null && grep -q ' / ' $T/etc/fstab || genfstab -U $T >> $T/etc/fstab
fi

echo "== system"
arch-chroot $T /bin/bash -e <<CHROOT
ln -sf /usr/share/zoneinfo/America/Phoenix /etc/localtime
hwclock --systohc || true
sed -i 's/^#en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen
locale-gen
echo LANG=en_US.UTF-8 > /etc/locale.conf
echo anyps5 > /etc/hostname
useradd -m -G wheel,video,input,audio,render $U
passwd -l $U
passwd -l root
systemctl enable NetworkManager bluetooth sshd systemd-timesyncd
if [ -z "${TARGET:-}" ]; then bootctl install --esp-path=/boot --no-variables --graceful; fi
CHROOT

install -d -m 700 $T/root/.ssh $T/home/$U/.ssh
cp "${AUTH_KEYS:-/root/.ssh/authorized_keys}" $T/root/.ssh/authorized_keys
cp "${AUTH_KEYS:-/root/.ssh/authorized_keys}" $T/home/$U/.ssh/authorized_keys

install -d $T/boot/loader/entries $T/boot/EFI/BOOT $T/boot/EFI/systemd
if [ -n "${TARGET:-}" ]; then
  cp $T/usr/lib/systemd/boot/efi/systemd-bootx64.efi $T/boot/EFI/BOOT/BOOTX64.EFI
  cp $T/usr/lib/systemd/boot/efi/systemd-bootx64.efi $T/boot/EFI/systemd/systemd-bootx64.efi
fi
cat > $T/boot/loader/loader.conf <<'EOF'
default anyps5.conf
timeout 0
editor no
EOF
cat > $T/boot/loader/entries/anyps5.conf <<'EOF'
title AnyPS5 OS
linux /vmlinuz-linux
initrd /intel-ucode.img
initrd /initramfs-linux.img
options root=LABEL=ANYPS5 rw quiet loglevel=3
EOF
sed -e 's/^title.*/title AnyPS5 OS (fallback)/' -e 's/initramfs-linux.img/initramfs-linux-fallback.img/' \
  $T/boot/loader/entries/anyps5.conf > $T/boot/loader/entries/anyps5-fallback.conf

install -d $T/etc/systemd/system/getty@tty1.service.d
cat > $T/etc/systemd/system/getty@tty1.service.d/autologin.conf <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\\\u' --noclear --autologin $U %I \$TERM
EOF

install -m 755 /dev/stdin $T/usr/local/bin/anyps5-session <<'EOF'
#!/bin/sh
INTEL_DEBUG=noccs gamescope -f -- /usr/local/bin/anyps5-shell > "$HOME/.anyps5-session.log" 2>&1
sleep 2
EOF
install -m 755 /dev/stdin $T/usr/local/bin/anyps5-shell <<'EOF'
#!/bin/sh
cd "$HOME/console/os-host" || exit 1
mkdir -p "$HOME/games"
python3 host.py --port 8790 --games "$HOME/games" > "$HOME/.anyps5-host.log" 2>&1 &
HOST=$!
for i in $(seq 100); do curl -s -o /dev/null http://127.0.0.1:8790/ && break; sleep 0.1; done
chromium --kiosk --remote-debugging-port=9222 --remote-debugging-address=127.0.0.1 --ozone-platform=x11 --no-first-run --noerrdialogs \
  --disable-session-crashed-bubble --disable-features=Translate --password-store=basic \
  --autoplay-policy=no-user-gesture-required --user-data-dir="$HOME/.config/anyps5-browser" \
  http://127.0.0.1:8790/
kill $HOST 2>/dev/null
EOF
cat > $T/home/$U/.bash_profile <<'EOF'
[ -z "$WAYLAND_DISPLAY$DISPLAY" ] && [ "$(tty)" = /dev/tty1 ] && exec /usr/local/bin/anyps5-session
EOF

install -d -m 750 $T/etc/polkit-1/rules.d
cat > $T/etc/polkit-1/rules.d/50-anyps5.rules <<EOF
polkit.addRule(function(action, subject) {
  if (subject.user != "$U") return;
  var a = action.id;
  if (a.indexOf("org.freedesktop.NetworkManager.") == 0 || a.indexOf("org.freedesktop.timedate1.") == 0 ||
      a.indexOf("org.freedesktop.locale1.") == 0 || a.indexOf("org.bluez.") == 0 ||
      a.indexOf("org.freedesktop.login1.power-off") == 0 || a.indexOf("org.freedesktop.login1.reboot") == 0 ||
      a.indexOf("org.freedesktop.login1.suspend") == 0) return polkit.Result.YES;
});
EOF

install -d $T/etc/udev/rules.d
cat > $T/etc/udev/rules.d/70-anyps5-controllers.rules <<'EOF'
KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0ce6|0df2|05c4|09cc|0ba0", MODE="0660", TAG+="uaccess"
KERNEL=="hidraw*", KERNELS=="*054C:0CE6*|*054C:0DF2*|*054C:05C4*|*054C:09CC*", MODE="0660", TAG+="uaccess"
EOF

install -d $T/usr/share/alsa/ucm2/conf.d/sof-rt5682 $T/etc/wireplumber/wireplumber.conf.d
cat > $T/usr/share/alsa/ucm2/conf.d/sof-rt5682/sof-rt5682.conf <<'EOF'
Syntax 6

If.jsl {
	Condition {
		Type String
		String1 "$${sys:bus/pci/devices/0000:00:1f.3/device}"
		String2 "0x4dc8"
	}
	True {
		SectionUseCase."HiFi" {
			File "/conf.d/sof-rt5682/HiFi.conf"
			Comment "Play HiFi quality Music"
		}
		If.rt5682s {
			Condition {
				Type String
				String1 "$${sys:bus/i2c/devices/i2c-RTL5682:00/modalias}"
				String2 "acpi:RTL5682:"
			}
			True.BootSequence [
				cset "name='Headphone Jack Switch' off"
				cset "name='Stereo1 DAC MIXL DAC L1 Switch' off"
				cset "name='Stereo1 DAC MIXR DAC R1 Switch' off"
				cset "name='RECMIX1L CBJ Switch' on"
				cset "name='CBJ Boost Volume' 28"
				cset "name='Stereo1 ADC L Mux' 'ADC1 L'"
				cset "name='Stereo1 ADC L1 Mux' ADC"
				cset "name='Stereo1 ADC MIXL ADC1 Switch' on"
				cset "name='Stereo1 ADC MIXL ADC2 Switch' off"
				cset "name='IF1 01 ADC Swap Mux' L/L"
				cset "name='STO1 ADC Capture Switch' on"
				cset "name='Headset Mic Switch' off"
			]
		}
	}
}
EOF
cat > $T/usr/share/alsa/ucm2/conf.d/sof-rt5682/HiFi.conf <<'EOF'
If.spk {
	Condition {
		Type ControlExists
		Control "name='Left Spk Switch'"
	}
	True.SectionDevice."Speaker" {
		Comment "Speaker"
		EnableSequence [
			cset "name='Left Spk Switch' on"
			cset "name='Right Spk Switch' on"
		]
		DisableSequence [
			cset "name='Left Spk Switch' off"
			cset "name='Right Spk Switch' off"
		]
		Value {
			PlaybackPriority 100
			PlaybackPCM "hw:${CardId},0"
		}
	}
}

If.hp {
	Condition {
		Type ControlExists
		Control "iface=CARD,name='Headphone Jack'"
	}
	True.SectionDevice."Headphones" {
		Comment "Headphones"
		EnableSequence [
			cset "name='Headphone Jack Switch' on"
			cset "name='Stereo1 DAC MIXL DAC L1 Switch' on"
			cset "name='Stereo1 DAC MIXR DAC R1 Switch' on"
		]
		DisableSequence [
			cset "name='Stereo1 DAC MIXL DAC L1 Switch' off"
			cset "name='Stereo1 DAC MIXR DAC R1 Switch' off"
			cset "name='Headphone Jack Switch' off"
		]
		Value {
			PlaybackPriority 200
			PlaybackPCM "hw:${CardId},1"
			PlaybackMixerElem "DAC1"
			JackControl "Headphone Jack"
		}
	}
}

If.hsmic {
	Condition {
		Type ControlExists
		Control "iface=CARD,name='Headset Mic Jack'"
	}
	True.SectionDevice."Headset" {
		Comment "Headset Microphone"
		EnableSequence [
			cset "name='Headset Mic Switch' on"
		]
		DisableSequence [
			cset "name='Headset Mic Switch' off"
		]
		Value {
			CapturePriority 200
			CapturePCM "hw:${CardId},1"
			CaptureMixerElem "STO1 ADC"
			JackControl "Headset Mic Jack"
		}
	}
}

Include.pcm_split.File "/common/pcm/split.conf"

Macro [
	{
		SplitPCM {
			Name "dmic_stereo_in"
			Direction Capture
			Format S32_LE
			Channels 2
			HWChannels 4
			HWChannelPos0 FL
			HWChannelPos1 FR
			HWChannelPos2 FL
			HWChannelPos3 FR
		}
	}
]

SectionDevice."Mic" {
	Comment "Internal Microphone"
	Value {
		CapturePriority 100
		CapturePCM "hw:${CardId},5"
	}
	Macro.pcm_split.SplitPCMDevice {
		Name "dmic_stereo_in"
		Direction Capture
		Device 5
		HWChannels 4
		Channels 2
		Channel0 0
		Channel1 1
		ChannelPos0 FL
		ChannelPos1 FR
	}
}

Include.hdmi.File "/codecs/hda/hdmi.conf"

If.hdmi1 {
	Condition { Type AlwaysTrue }
	True.Macro.hdmi1.HDMI {
		Number 1
		Device 2
		Priority 500
	}
}

If.hdmi2 {
	Condition { Type AlwaysTrue }
	True.Macro.hdmi2.HDMI {
		Number 2
		Device 3
		Priority 600
	}
}

If.hdmi3 {
	Condition { Type AlwaysTrue }
	True.Macro.hdmi3.HDMI {
		Number 3
		Device 4
		Priority 700
	}
}
EOF
cat > $T/etc/wireplumber/wireplumber.conf.d/51-anyps5-sof-rt5682.conf <<'EOF'
monitor.alsa.rules = [
  {
    matches = [
      {
        api.alsa.card.name = "sof-rt5682"
      }
    ]
    actions = {
      update-props = {
        api.acp.disable-pro-audio = true
      }
    }
  }
]
EOF
sed -i 's|^#\?NoExtract *=.*|NoExtract   = usr/share/alsa/ucm2/conf.d/sof-rt5682/*|' $T/etc/pacman.conf
grep -q '^NoExtract.*sof-rt5682' $T/etc/pacman.conf || { echo "pacman.conf NoExtract not set"; exit 1; }

python3 - $T/etc/NetworkManager/system-connections <<'PY'
import configparser, glob, os, sys, uuid
out = sys.argv[1]
for f in glob.glob("/var/lib/iwd/*.psk"):
    name = os.path.basename(f)[:-4]
    ssid = bytes.fromhex(name[1:]).decode() if name.startswith("=") else name
    c = configparser.ConfigParser(interpolation=None); c.optionxform = str; c.read(f)
    sec = c["Security"] if c.has_section("Security") else {}
    psk = sec.get("Passphrase") or sec.get("PreSharedKey")
    if not psk:
        continue
    p = os.path.join(out, "anyps5-wifi.nmconnection")
    with open(p, "w") as w:
        w.write(f"[connection]\nid={ssid}\nuuid={uuid.uuid4()}\ntype=wifi\nautoconnect=true\n\n[wifi]\nmode=infrastructure\nssid={ssid}\n\n[wifi-security]\nkey-mgmt=wpa-psk\npsk={psk}\n\n[ipv4]\nmethod=auto\n\n[ipv6]\nmethod=auto\n")
    os.chmod(p, 0o600)
    print("wifi profile written")
    break
PY

install -d $T/etc/bluetooth
printf '[General]
ClassicBondedOnly=false
UserspaceHID=true
' > $T/etc/bluetooth/input.conf
install -m 755 /dev/stdin $T/usr/local/bin/anyps5-bt-trust <<'EOF'
#!/bin/bash
for mac in $(bluetoothctl devices Bonded | awk '{print $2}'); do
  info=$(bluetoothctl info "$mac")
  grep -q "Trusted: yes" <<< "$info" && continue
  icon=$(sed -n 's/^[[:space:]]*Icon: //p' <<< "$info")
  cls=$(sed -n 's/^[[:space:]]*Class: \(0x[0-9a-fA-F]*\).*/\1/p' <<< "$info")
  case "$icon" in
    input-*) ;;
    *) [ $(( (${cls:-0} >> 8) & 31 )) -eq 5 ] || continue ;;
  esac
  bluetoothctl trust "$mac"
done
EOF
cat > $T/etc/systemd/system/anyps5-bt-trust.service <<'EOF'
[Unit]
Description=Trust bonded Bluetooth controllers, keyboards and mice
After=bluetooth.service
Requires=bluetooth.service
[Service]
Type=oneshot
ExecStart=/usr/local/bin/anyps5-bt-trust
[Install]
WantedBy=bluetooth.target
EOF
cat > $T/etc/udev/rules.d/71-anyps5-bt-trust.rules <<'EOF'
ACTION=="add", SUBSYSTEM=="hid", KERNEL=="0005:*", TAG+="systemd", ENV{SYSTEMD_WANTS}+="anyps5-bt-trust.service"
EOF
arch-chroot $T systemctl enable anyps5-bt-trust.service
install -m 755 /dev/stdin $T/usr/local/bin/anyps5-pad-watch <<'EOF'
#!/usr/bin/env python3
import glob
import os
import select
import struct
import subprocess
import time

SILENT = 20.0
ACTIVE = "/run/anyps5-input-active"
watched = {}
inputs = {}
touched = 0.0


def scan():
    for name_file in glob.glob("/sys/class/input/event*/device/name"):
        base = os.path.dirname(name_file)
        try:
            name = open(name_file).read().strip()
            mac = open(os.path.join(base, "uniq")).read().strip().upper()
        except OSError:
            continue
        if not name.endswith("Motion Sensors") or len(mac) != 17:
            continue
        dev = "/dev/input/" + os.path.basename(os.path.dirname(base))
        if any(w["dev"] == dev for w in watched.values()):
            continue
        try:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        watched[fd] = {"dev": dev, "mac": mac, "last": time.monotonic()}


def scan_inputs():
    for name_file in glob.glob("/sys/class/input/event*/device/name"):
        base = os.path.dirname(name_file)
        try:
            name = open(name_file).read().strip()
        except OSError:
            continue
        if name.startswith("AnyPS5") or name.endswith("Motion Sensors"):
            continue
        dev = "/dev/input/" + os.path.basename(os.path.dirname(base))
        if any(i["dev"] == dev for i in inputs.values()):
            continue
        try:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        inputs[fd] = {"dev": dev, "pad": bool(glob.glob(base + "/js*")), "opened": time.monotonic()}


def meaningful(data, pad):
    for offset in range(0, len(data) - 23, 24):
        kind, code, value = struct.unpack_from("HHi", data, offset + 16)
        if kind == 1 and value == 1:
            return True
        if kind == 2 and not pad:
            return True
        if kind == 3:
            if not pad:
                return True
            if code in (0, 1, 3, 4) and abs(value - 128) > 40:
                return True
            if code in (2, 5) and value > 40:
                return True
            if code in (16, 17) and value != 0:
                return True
    return False


def mark_active():
    global touched
    now = time.monotonic()
    if now - touched < 5:
        return
    touched = now
    with open(ACTIVE, "w") as f:
        f.write(str(time.time()))


def connected(mac):
    out = subprocess.run(["bluetoothctl", "info", mac], capture_output=True, text=True, timeout=10).stdout
    return "Connected: yes" in out


def drop(fd):
    os.close(fd)
    del watched[fd]


next_scan = 0.0
while True:
    now = time.monotonic()
    if now >= next_scan:
        scan()
        scan_inputs()
        next_scan = now + 5
    ready, _, _ = select.select(list(watched) + list(inputs), [], [], 1.0)
    now = time.monotonic()
    for fd in ready:
        if fd in inputs:
            try:
                data = os.read(fd, 4096)
            except OSError:
                os.close(fd)
                del inputs[fd]
                continue
            if time.monotonic() - inputs[fd]["opened"] > 2 and meaningful(data, inputs[fd]["pad"]):
                mark_active()
            continue
        try:
            if os.read(fd, 4096):
                watched[fd]["last"] = now
        except OSError:
            drop(fd)
    for fd, w in list(watched.items()):
        if now - w["last"] < SILENT:
            continue
        if connected(w["mac"]):
            print("pad %s silent for %.0f s while connected; dropping the link" % (w["mac"], now - w["last"]), flush=True)
            subprocess.run(["bluetoothctl", "disconnect", w["mac"]], capture_output=True, timeout=20)
        drop(fd)
EOF
cat > $T/etc/systemd/system/anyps5-pad-watch.service <<'EOF'
[Unit]
Description=Drop Bluetooth links of controllers that stopped sending input
After=bluetooth.service
[Service]
ExecStart=/usr/local/bin/anyps5-pad-watch
Restart=always
RestartSec=5
[Install]
WantedBy=multi-user.target
EOF
arch-chroot $T systemctl enable anyps5-pad-watch.service
install -m 755 /dev/stdin $T/usr/local/bin/anyps5-keys <<'EOF'
#!/usr/bin/env python3
import glob
import json
import os
import select
import struct
import subprocess
import time
import urllib.request

EV_KEY, EV_MSC, MSC_SCAN = 1, 4, 4
MUTE, VOL_DOWN, VOL_UP = 113, 114, 115
BRIGHT_DOWN, BRIGHT_UP = 224, 225
TOP_ROW = set(range(59, 69)) | {87, 88, 158, 159, 173, 120, 372, 224, 225, 113, 114, 115}
EVENT = struct.Struct("llHHi")
USER_ENV = ["runuser", "-u", "anyps5", "--", "env", "XDG_RUNTIME_DIR=/run/user/1000", "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"]
watched = {}


def keyboards():
    for caps in glob.glob("/sys/class/input/event*/device/capabilities/key"):
        try:
            words = open(caps).read().split()
        except OSError:
            continue
        bits = 0
        for w in words:
            bits = (bits << 64) | int(w, 16)
        if bits >> VOL_UP & 1 or bits >> BRIGHT_UP & 1 or bits >> 30 & 1:
            yield "/dev/input/" + caps.split("/")[4]


def scan():
    for dev in keyboards():
        if dev in (w["dev"] for w in watched.values()):
            continue
        try:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        watched[fd] = {"dev": dev, "scan": None}


def osd(kind, level, muted=False):
    body = json.dumps({"osd": {"kind": kind, "level": level, "muted": muted}}).encode()
    req = urllib.request.Request("http://127.0.0.1:8790/msg", body, {"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=2).read()
    except OSError:
        pass


def volume(arg):
    subprocess.run(USER_ENV + ["wpctl"] + arg, capture_output=True, timeout=5)
    out = subprocess.run(USER_ENV + ["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], capture_output=True, text=True, timeout=5).stdout
    parts = out.split()
    if len(parts) >= 2:
        try:
            osd("volume", round(float(parts[1]) * 100), "MUTED" in out)
        except ValueError:
            pass


def brightness(step):
    for d in glob.glob("/sys/class/backlight/*"):
        try:
            cur = int(open(d + "/brightness").read())
            top = int(open(d + "/max_brightness").read())
        except (OSError, ValueError):
            continue
        new = max(top // 20, min(top, cur + step * top // 12))
        try:
            open(d + "/brightness", "w").write(str(new))
        except OSError:
            continue
        osd("brightness", round(new * 100 / top))


def key(code, value, scan_code):
    if value == 0:
        return
    if code == VOL_UP:
        volume(["set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", "5%+"])
    elif code == VOL_DOWN:
        volume(["set-volume", "@DEFAULT_AUDIO_SINK@", "5%-"])
    elif code == MUTE and value == 1:
        volume(["set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"])
    elif code == BRIGHT_UP:
        brightness(1)
    elif code == BRIGHT_DOWN:
        brightness(-1)
    elif code in TOP_ROW and value == 1:
        print("top-row key code %d scan %s not handled here" % (code, "%x" % scan_code if scan_code is not None else "?"), flush=True)


next_scan = 0.0
while True:
    now = time.monotonic()
    if now >= next_scan:
        scan()
        next_scan = now + 5
    ready, _, _ = select.select(list(watched), [], [], 2.0)
    for fd in ready:
        try:
            data = os.read(fd, EVENT.size * 64)
        except OSError:
            os.close(fd)
            del watched[fd]
            continue
        for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
            _, _, typ, code, value = EVENT.unpack_from(data, i)
            if typ == EV_MSC and code == MSC_SCAN:
                watched[fd]["scan"] = value & 0xFFFFFFFF
            elif typ == EV_KEY:
                key(code, value, watched[fd]["scan"])
                watched[fd]["scan"] = None
EOF
cat > $T/etc/systemd/system/anyps5-keys.service <<'EOF'
[Unit]
Description=Chromebook top-row keys: volume, mute and screen brightness
[Service]
ExecStart=/usr/local/bin/anyps5-keys
Restart=always
RestartSec=3
[Install]
WantedBy=multi-user.target
EOF
arch-chroot $T systemctl enable anyps5-keys.service
install -d $T/etc/bluetooth/main.conf.d 2>/dev/null; grep -q '^AlwaysPairable' $T/etc/bluetooth/main.conf 2>/dev/null || sed -i 's/^#\?AlwaysPairable.*/AlwaysPairable = true/' $T/etc/bluetooth/main.conf

cp -a "${CONSOLE_SRC:-/root/console}" $T/home/$U/console
arch-chroot $T chown -R $U:$U /home/$U
sync
echo "== done"
