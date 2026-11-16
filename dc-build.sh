#!/bin/bash
# Build dEngine for Dreamcast inside the official KallistiOS toolchain image.
# Usage: ./dc-build.sh [Release|Debug] [--cdi]
# Output: cmake-build-<type>-dreamcast/dEngine.elf (and dEngine.cdi with --cdi)
set -euo pipefail
cd "$(dirname "$0")"
IMAGE="${KOS_IMAGE:-kallistios/dc-kos-toolchain:latest}"
TYPE="${1:-Release}"
LOWER=$(echo "$TYPE" | tr '[:upper:]' '[:lower:]')
BUILD_DIR="cmake-build-${LOWER}-dreamcast"
CDI=0; [[ " $* " == *" --cdi "* ]] && CDI=1

if ! docker info >/dev/null 2>&1; then
  command -v dockerd >/dev/null || { echo "docker is not installed" >&2; exit 1; }
  (dockerd >/tmp/dockerd.log 2>&1 &)
  for _ in $(seq 30); do docker info >/dev/null 2>&1 && break; sleep 1; done
fi
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker pull -q "$IMAGE"

# CMake only links a romdisk when these exist; main.c always references one.
mkdir -p romdisk assets disc

docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src "$IMAGE" sh -c "
  cmake -S . -B $BUILD_DIR -DCMAKE_TOOLCHAIN_FILE=toolchain/Dreamcast.cmake -DCMAKE_BUILD_TYPE=$TYPE &&
  cmake --build $BUILD_DIR -j\$(nproc) &&
  sh-elf-size $BUILD_DIR/dEngine.elf &&
  if [ $CDI = 1 ]; then mkdcdisc -N -d disc/ -e $BUILD_DIR/dEngine.elf -n dEngine -o dEngine.cdi && ls -l dEngine.cdi; fi
"
