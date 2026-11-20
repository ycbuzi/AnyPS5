#!/bin/bash
# Build the native console in WSL (source is the Windows tools folder): build-console.sh
cd /root/acn/glyphs/build || exit 1
nice -n 10 cmake --build . -j6 2>&1 | grep -E "error|Linking" | tail -6
echo "rc=${PIPESTATUS[0]}"
ls -la --time-style=+%H:%M anyps5-console | cut -c24-80
