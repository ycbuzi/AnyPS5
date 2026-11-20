#!/bin/bash
set -euo pipefail
HOSTIP=${1:?usage: deploy-toolchain.sh <device ip> [wsl bundle dir]}
BUNDLE=${2:-/root/aos-toolchain/opt/anyps5}
KEY="$HOME/.ssh/id_ed25519_chromebook"
SSH=(ssh -o ConnectTimeout=15 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$HOME/.ssh/known_hosts_anyps5os" -i "$KEY" "root@$HOSTIP")

MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- test -x "$BUNDLE/bin/relinker" || { echo "no toolchain at $BUNDLE (run toolchain/build-toolchain.sh)"; exit 1; }
MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- sed -n 's/^commit=/toolchain /p' "$BUNDLE/VERSION"

MSYS_NO_PATHCONV=1 wsl.exe -d archlinux -u root -- tar -C "$BUNDLE" -czf - . \
  | "${SSH[@]}" "set -e; rm -rf /opt/anyps5.new; mkdir -p /opt/anyps5.new; tar -C /opt/anyps5.new -xzf -; test -x /opt/anyps5.new/bin/relinker"

"${SSH[@]}" "bash -s" <<'EOF'
set -e
rm -rf /opt/anyps5.old
[ -d /opt/anyps5 ] && mv /opt/anyps5 /opt/anyps5.old
mv /opt/anyps5.new /opt/anyps5
rm -rf /opt/anyps5.old
mkdir -p /usr/local/bin
ln -sf /opt/anyps5/bin/anyps5-build-game /opt/anyps5/bin/anyps5-compat /usr/local/bin/
missing=$(for f in /opt/anyps5/bin/relinker /opt/anyps5/libs/*; do LD_LIBRARY_PATH=/opt/anyps5/libs ldd "$f" 2>/dev/null | grep 'not found' | grep -v '\.prx '; done | sort -u || true)
[ -z "$missing" ] || { echo "missing system libraries: $missing"; exit 1; }
/opt/anyps5/bin/relinker --version 2>/dev/null | head -1 || true
/opt/anyps5/bin/anyps5-compat || true
echo "toolchain installed at /opt/anyps5 ($(du -sh /opt/anyps5 | cut -f1))"
EOF
