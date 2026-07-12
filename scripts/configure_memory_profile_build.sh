#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build-memory-profile}"

cmake -S "$repo_root" -B "$repo_root/$build_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPIXELART_MEMORY_PROFILE_BUILD=ON \
  -DPIXELART_ENABLE_SANITIZERS=OFF \
  -DPIXELART_BUILD_TESTS=ON

cmake --build "$repo_root/$build_dir" --target PixelArt98

app_path="$repo_root/$build_dir/PixelArt98"
if [[ "$(uname -s)" == "Darwin" ]]; then
  app_path="$repo_root/$build_dir/PixelArt98.app/Contents/MacOS/PixelArt98"
fi

cat <<EOF
Memory profile build ready:
  $app_path

Trace with:
  PIXELART_APP="$app_path" scripts/profile_blue_marble_memory.sh
EOF
