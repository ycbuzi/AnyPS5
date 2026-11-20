# AnyPS5 OS (Chromebook)

AnyPS5 OS turns a low-end Chromebook into a small console: Arch Linux installed on the internal drive boots in about 15 seconds straight into a native console UI. The UI has a game library with artwork, Play/Install, controllers over Bluetooth, and settings pages for Wi-Fi, language, date and time, family, Bluetooth, light bar and volume. Tested on an ASUS CX1400FKA (Intel N6000, no AVX) with MrChromebox UEFI firmware.

Games are relinked with AVX lowered to SSE (`--to-sse`, branch `feat/relinker-lower-avx-to-sse-main`). The Chromebook game fixes are on this branch (`chromebook/integration-1116-pad`).

Measured on the N6000: Coffee Talk Episode 2 about 50 FPS, Celeste 40, Coffee Talk 19, TERRIFIER 19, Afterdream 10, Animal Well 8.

| Folder | What it is |
|---|---|
| `console/` | the console UI: C++20 with SDL3, SDL3_ttf, SDL3_image and nlohmann_json (Arch packages), plus its tests |
| `image/` | building the disk image, installing it to a USB stick or the internal drive, A/B updates, recovery, boot units |
| `image/selftest/` | on-device checks: self test, a scripted tour that presses keys and takes screenshots, game test drives |
| `image/idle-tests/` | runs the installed games by itself while the device is idle and logs their frame rate |
| `image/toolchain/` | building and relinking games on the device |

Start with [image/README.md](image/README.md) for the stick and [image/FIRMWARE.md](image/FIRMWARE.md) for the firmware. Flashing firmware is done by the device's owner and can brick the device; keep the stock backup.

The test fixtures that used game artwork are left out, so the tests that load them need your own art files. Paths and addresses in the scripts are examples (`192.0.2.10` stands for the PC).
