# AnyPS5 on Xbox One (Developer Mode)

Experimental port. A relinked PS5 game runs as a UWP app on an Xbox One in Developer Mode. The AnyPS5 libraries are built for Windows as usual; Vulkan is provided by `xvk`, a small Vulkan-on-Direct3D 12 layer written for this port, because the Xbox only offers D3D12 at feature level 11 with shader model 5.1.

Status on an Xbox One S (6 CPU cores for apps, about 5 GB of memory, AMD Jaguar without AVX):

| Game | Result |
|---|---|
| Celeste | plays, about 33 FPS with `APS5_MIRROR_PUBLISH=1` (about 5 FPS without) |
| Animal Well | reaches its intro, about 2 FPS |

## Layout

| Folder | What it is |
|---|---|
| `xvk/` | Vulkan ICD on D3D12. `xcross.inc` and `xlower.inc` convert SPIR-V to HLSL 5.1 on the console with SPIRV-Cross and compile it to DXBC |
| `console/` | the AnyPS5 Console app that runs on the Xbox: game library, launcher, settings pages |
| `payload/` | loader that maps a relinked game and its libraries inside the UWP process |
| `hlsl51/` | offline SPIR-V to HLSL 5.1 to DXBC chain and the patch scripts used while bringing shaders up |
| `shaders/` | the app's own present shaders |
| `capprobe/`, `probe.cpp` | capability probe used to survey what the console's D3D12 driver supports |
| `*.py` at the top | PC-side tools: deploy, push files, convert and add a game, patch libraries, read logs |

SPIRV-Cross is the submodule at `3rdparty/SPIRV-Cross`.

## Driver changes this port needs

On this branch (`xbox/xvk-runtime`):

- `feat(agc): publish storage textures into the heap mirror on the GPU`: the Xbox cannot import host memory into D3D12 (`OpenExistingHeapFromAddress` fails), so every render target used to come back through the CPU. `APS5_MIRROR_PUBLISH=1` copies and retiles it on the GPU instead, and skips images whose version has not changed.
- 8-bit storage and `shaderInt64` fallbacks (`run BDA byte access without storageBuffer8BitAccess`, `run without shaderInt64 using the descriptor buffer path`).
- `HostCanonical`: `std::filesystem::canonical` fails inside the UWP sandbox; paths are resolved through `GetFinalPathNameByHandleW`.
- `HostCpu`: the game is relinked with AVX lowered to SSE (`--to-sse`, branch `feat/relinker-lower-avx-to-sse-main`); the CPU check accepts the features the relinker lowered.

## Building and running

You need an Xbox One in Developer Mode (activated by its owner), the Visual Studio 2022 build tools with the Windows 10 SDK, and the MinGW toolchain from [BUILD](../../docs/dev/BUILD.md).

1. Build AnyPS5 for Windows from this branch.
2. Build `xvk` with `xvk/build.cmd` and SPIRV-Cross with `build-cross.cmd`.
3. Relink a game you dumped from your own PS5 with `--to-sse`, then `python xbox_convert.py <game folder>` and `python xbox_add_game.py <game folder> --convert`.
4. Pass every library you copy to the console through `python xboxpatch.py <out> <prx...>`.
5. Build the console app with `console/build.ps1` and install it with `python deploy.py <console address>`. The Device Portal login is read from a file outside the repository; nothing in this tree stores it.

The scripts still use the folder layout of the machine they were written on (`C:\AnyPS5-work\xbox-probe`); change the paths at the top of each script.

## Limits

- No host memory import into D3D12, so CPU-written guest memory is copied into GPU heaps.
- Shader model 5.1: no typed UAV loads for most formats, no 64-bit integers, no wave ops; the converters lower or emulate these.
- About 17 fence waits per frame in Celeste are the next bottleneck; the tile-7 retile costs about 6 ms per 4K frame.
- Only games you dumped yourself and copied to the console's developer storage. Retail games installed on the console are not reachable from Developer Mode.
