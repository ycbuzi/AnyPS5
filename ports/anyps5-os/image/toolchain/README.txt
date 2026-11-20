AnyPS5 OS toolchain (/opt/anyps5)

The AnyPS5 relinker and the Linux system libraries a relinked PS5 game loads, built for this OS (Linux x86-64,
x86-64-v2 baseline, no AVX). Installing a game relinks it on the device, so the result always matches this OS, this
CPU and this AnyPS5 version. VERSION names the AnyPS5 commit, the CPU baseline and whether AVX lowering is in.

On a CPU without AVX2 (the Chromebook's Pentium Silver N6000 has SSE4.2 only) a toolchain with to_sse=yes relinks
with --to-sse: AVX, AVX2, FMA, F16C, BMI1, BMI2, LZCNT and MOVBE in the game are lowered to SSE4.2, and the game
carries a header that tells libkernel's start-up check which features were lowered. The check only accepts the
missing AVX features when every VEX instruction of the game was lowered; anyps5-build-game reads that header after
relinking and refuses to install a game that still needs AVX on this CPU (exit 3; --force installs it anyway).

  bin/anyps5-compat [--json] [--game DIR [--dry-run]]
      Can this device run relinked games, and can this game be installed?
      exit 0 installable, 2 needs a newer AnyPS5 (the conversion fails), 3 this CPU lacks what the games need,
      4 the game folder is not usable, 6 the toolchain is broken, 7 undecidable
  bin/anyps5-build-game [--force] [--name NAME] [--copy] <dump or app0 dir> <out dir> [-- relinker options]
      Install / reinstall / update: relinks the game into <out> (<out>/game, libs/, app0/, anyps5-build.json).
      Saves and anything else in <out> stay. exit 0 built, 1 usage, 2 relinker failed, 3 CPU lacks what games
      need (nothing built; --force builds anyway), 4 input not a usable game, 5 output not writable or too small,
      6 toolchain broken
  bin/relinker      the AnyPS5 relinker itself
  libs/             AnyPS5 system libraries (*.prx); each game gets hard links (or copies) in its own libs/
  share/cpu-requirements.txt   CPU features the libraries require (from libkernel's start-up check)
  share/system-libraries.txt   OS libraries the bundle links against
  share/unwrap_self.py         turns an unencrypted SELF eboot.bin into a plain ELF
  share/isa-scan.txt           AVX/BMI/LZCNT instructions found in the relinker and libraries at build time, per
                               function (all are behind CPUID checks; share/isa-scan.py FILE... rescans, needs objdump)

An update is due when anyps5-build.json's toolchain.commit differs from VERSION's commit, or the game's
sce_sys/param.json contentVersion differs from the stamp's contentVersion.

Run a game from its folder: cd <out> && ./game
