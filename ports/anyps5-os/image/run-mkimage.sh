#!/bin/bash
export WIFI_PROFILE=/mnt/c/AnyPS5-work/secrets/wifi.nmconnection
bash "/mnt/c/AnyPS5 Games/_tools/anyps5-os-stick/mkimage.sh"
echo
echo "Build finished (exit $?). This window can be closed."
exec bash
