#!/bin/bash
set -euo pipefail
HOSTIP=${1:?usage: update-os.sh <chromebook ip> [root prefix, /mnt when run from the Arch stick]}
ROOT=${2:-}
KEY="$HOME/.ssh/id_ed25519_chromebook"
SSH=(ssh -o ConnectTimeout=15 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$HOME/.ssh/known_hosts_anyps5os" -i "$KEY" "root@$HOSTIP")
HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$(cd "$HERE/.." && pwd)"

tar -C "$TOOLS" --exclude='console/os-host/data' --exclude='*.before-*' --exclude='__pycache__' \
    --exclude='console/art' --exclude='console/cache' --exclude='console/library.js' -cf - console \
  | "${SSH[@]}" "set -e; R='$ROOT'; mkdir -p \$R/home/anyps5; tar -C \$R/home/anyps5 -xf -; chown -R 1000:1000 \$R/home/anyps5/console"

sed -n "/^install -m 755 \/dev\/stdin \/mnt\/usr\/local\/bin\/anyps5-session/,/^EOF$/p;/^install -m 755 \/dev\/stdin \/mnt\/usr\/local\/bin\/anyps5-shell/,/^EOF$/p" "$HERE/install.sh" \
  | sed "s#/mnt/usr/local/bin#$ROOT/usr/local/bin#" | "${SSH[@]}" "bash -s"
echo "updated console and session on $HOSTIP${ROOT:+ (under $ROOT)}"
