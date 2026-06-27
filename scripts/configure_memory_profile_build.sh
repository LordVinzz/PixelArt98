#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build-memory-profile}"

cmake -S "$repo_root" -B "$repo_root/$build_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPIXELART_MEMORY_PROFILE_BUILD=ON \
  -DPIXELART_ENABLE_SANITIZERS=OFF \
  -DPIXELART_BUILD_TESTS=ON

cmake --build "$repo_root/$build_dir" --target pixelart_sdl2

cat <<EOF
Memory profile build ready:
  $repo_root/$build_dir/pixelart_sdl2

Trace with:
  PIXELART_APP="$repo_root/$build_dir/pixelart_sdl2" scripts/profile_blue_marble_memory.sh
EOF
