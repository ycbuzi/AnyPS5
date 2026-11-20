#!/bin/bash
set -euo pipefail
HOSTIP=${1:?usage: deploy-native.sh <chromebook ip> [wsl build dir]}
BUILD=${2:-/root/acn/integ/build}
KEY="$HOME/.ssh/id_ed25519_chromebook"
SSH=(ssh -o ConnectTimeout=15 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$HOME/.ssh/known_hosts_anyps5os" -i "$KEY" "root@$HOSTIP")
HERE="$(cd "$(dirname "$0")" && pwd)"

bash "$HERE/update-os.sh" "$HOSTIP"

MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- tar -C "$BUILD" -cf - anyps5-console assets \
  | "${SSH[@]}" "set -e; mkdir -p /opt/anyps5-console; tar -C /opt/anyps5-console -xf -; chmod 755 /opt/anyps5-console/anyps5-console"

"${SSH[@]}" "bash -s" <<'EOF'
set -e
missing=$(ldd /opt/anyps5-console/anyps5-console | grep 'not found' || true)
[ -z "$missing" ] || { echo "missing libraries: $missing"; exit 1; }
install -m 755 /dev/stdin /usr/local/bin/anyps5-session <<'S'
#!/bin/sh
export ANV_SYS_MEM_LIMIT=100
cd "$HOME/console/os-host" || exit 1
mkdir -p "$HOME/games"
python3 host.py --port 8790 --games "$HOME/games" > "$HOME/.anyps5-host.log" 2>&1 &
HOST=$!
for i in $(seq 100); do curl -s -o /dev/null http://127.0.0.1:8790/ && break; sleep 0.1; done
SDL_JOYSTICK_HIDAPI_PS5=0 SDL_VIDEODRIVER=kmsdrm /opt/anyps5-console/anyps5-console --host http://127.0.0.1:8790 > "$HOME/.anyps5-session.log" 2>&1
kill $HOST 2>/dev/null
sleep 1
S
install -m 644 /dev/stdin /etc/udev/rules.d/70-anyps5-controllers.rules <<'R'
KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0ce6|0df2|05c4|09cc|0ba0", MODE="0660", TAG+="uaccess"
KERNEL=="hidraw*", KERNELS=="*054C:0CE6*|*054C:0DF2*|*054C:05C4*|*054C:09CC*", MODE="0660", TAG+="uaccess"
R
udevadm control --reload
echo "native console installed; session switched"
systemd-run --on-active=2 --unit=anyps5-session-restart-$(date +%s) systemctl restart getty@tty1 >/dev/null
echo "session restart scheduled"
EOF
