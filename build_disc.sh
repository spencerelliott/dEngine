#!/bin/bash

/opt/toolchains/dc-kos-gccrs/kos/utils/mkdcdisc/mkdcdisc -N -d disc/ -e cmake-build-release-dreamcast/dEngine.elf \
  -n dEngine -v 3 -o dEngine.cdi