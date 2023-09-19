#!/bin/bash

/opt/toolchains/dc-kos-gccrs/kos/utils/mkdcdisc/mkdcdisc -d disc/ -e cmake-build-debug-dreamcast/dEngine.elf \
  -n dEngine -v 3 -o dEngine.cdi