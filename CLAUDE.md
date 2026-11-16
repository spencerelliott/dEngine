# dEngine (Dreamcast)

## Building
Run `./dc-build.sh [Release|Debug] [--cdi]`. It builds inside the official KallistiOS
Docker image (`kallistios/dc-kos-toolchain`: KOS 2.1, GCC 14.2). It starts `dockerd`
if needed and pulls the image if it is missing. Output goes to
`cmake-build-<type>-dreamcast/dEngine.elf`; `--cdi` also packs `dEngine.cdi`
with `mkdcdisc` from `disc/`.

- GL is GLdc from kos-ports (`-lGL`, headers in `$KOS_PORTS/include/GL`).
- `toolchain/Dreamcast.cmake` uses `$KOS_SH4_PRECISION` for the FPU ABI, so the
  engine matches how KOS was built.
- `romdisk/` and `assets/` are local and gitignored. The script creates them empty,
  because `main.c` always links a romdisk.
- `build_assets.sh` (zsh + grealpath), `build_disc.sh` and `run_dctool_ip.sh` use
  macOS paths from the original dev machine.
