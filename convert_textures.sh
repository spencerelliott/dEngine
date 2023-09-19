#!/bin/bash

/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f RGB565 -i "data/textures/monsters/hellknight.png" -o "data/texturesPVR/monsters/hellknight.png.rgb565.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f RGB565 -i "data/textures/monsters/hellknight_local.png" -o "data/texturesPVR/monsters/hellknightBump.png.rgb565.pvr"
#/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f RGB565 -i "data/textures/monsters/hellBaron.png" -o "data/texturesPVR/monsters/hellBaron.png.rgb565.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f RGB565 -i "data/textures/monsters/hellBaronDiff.png" -o "data/texturesPVR/monsters/hellBaronDiff.png.rgb565.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f RGB565 -i "data/textures/monsters/hellBaronSpec.png" -o "data/texturesPVR/monsters/hellBaronSpec.png.rgb565.pvr"