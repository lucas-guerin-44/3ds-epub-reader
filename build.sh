#!/bin/bash
# Build script for 3DS EPUB Reader
# Uses devkitPro's own MSYS2 shell for correct path resolution.
#
# Usage: ./build.sh [clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DKPRO_MSYS2="/c/devkitPro/msys2/usr/bin/bash.exe"

if [ -f "$DKPRO_MSYS2" ]; then
  "$DKPRO_MSYS2" --login -c "cd '$SCRIPT_DIR' && make $*"
else
  cd "$SCRIPT_DIR"
  make "$@"
fi
