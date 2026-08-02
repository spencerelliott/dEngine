#!/bin/bash

# Background music can be pressed on the disc as CDDA tracks, which the GD-ROM
# drive plays without using any CPU time. Add one -c per track, in the order
# they should be laid out: the first one becomes track 1, and so on.
#
#   -c data/music/theme.wav -c data/music/action.wav

/opt/toolchains/dc-kos-gccrs/kos/utils/mkdcdisc/mkdcdisc -N -d disc/ -e cmake-build-release-dreamcast/dEngine.elf \
  -n dEngine -v 3 -o dEngine.cdi