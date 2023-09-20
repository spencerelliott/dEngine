#!/bin/bash

/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/font/font1.png" -o "data/font/font1.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/textures/monsters/hellknight.png" -o "data/texturesPVR/monsters/hellknight.png.argb4444.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/textures/monsters/hellknight_local.png" -o "data/texturesPVR/monsters/hellknightBump.png.argb4444.pvr"
#/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/textures/monsters/hellBaron.png" -o "data/texturesPVR/monsters/hellBaron.png.argb4444.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/textures/monsters/hellBaronDiff.png" -o "data/texturesPVR/monsters/hellBaronDiff.png.argb4444.pvr"
/opt/toolchains/dc-kos-gccrs/kos/utils/texconv/texconv -c -f ARGB4444 -i "data/textures/monsters/hellBaronSpec.png" -o "data/texturesPVR/monsters/hellBaronSpec.png.argb4444.pvr"
