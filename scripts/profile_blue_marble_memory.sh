#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
unset MallocStackLogging
unset MallocStackLoggingNoCompact

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
default_app="$repo_root/build/PixelArt98"
if [[ "$(uname -s)" == "Darwin" ]]; then
  default_app="$repo_root/build/PixelArt98.app/Contents/MacOS/PixelArt98"
fi
app="${PIXELART_APP:-$default_app}"
image="${1:-${PIXELART_PROFILE_IMAGE:-/Users/vincentdominguez/Desktop/Blue_Marble_2002.png}}"
seconds="${PIXELART_PROFILE_SECONDS:-120}"
max_addresses="${PIXELART_PROFILE_MAX_ADDRESSES:-12}"
capture_threshold_gib="${PIXELART_PROFILE_CAPTURE_GIB:-24}"
capture_threshold_bytes="$(awk -v gib="$capture_threshold_gib" 'BEGIN { printf "%.0f", gib * 1024 * 1024 * 1024 }')"
stamp="$(date +%Y%m%d_%H%M%S)"
out_dir="${PIXELART_PROFILE_OUT:-$repo_root/logs/blue_marble_memory_$stamp}"

if [[ ! -x "$app" ]]; then
  echo "App executable not found or not executable: $app" >&2
  exit 1
fi

if [[ ! -f "$image" ]]; then
  echo "Image not found: $image" >&2
  exit 1
fi

mkdir -p "$out_dir"

trace_csv="$out_dir/huge_image_memory_trace.csv"
rss_csv="$out_dir/rss.csv"
stdout_log="$out_dir/app_stdout_stderr.log"
vmmap_file="$out_dir/vmmap_summary.txt"
heap_file="$out_dir/heap.txt"
addresses_file="$out_dir/heap_addresses.txt"
report_file="$out_dir/report.txt"
high_snapshot_dir=""

capture_allocator_snapshot() {
  local tag="$1"
  local snapshot_dir="$out_dir/$tag"
  mkdir -p "$snapshot_dir"
  vmmap -summary "$pid" >"$snapshot_dir/vmmap_summary.txt" 2>&1 || true
  heap -sortBySize "$pid" >"$snapshot_dir/heap.txt" 2>&1 || heap "$pid" >"$snapshot_dir/heap.txt" 2>&1 || true
  grep -Eo '0x[0-9a-fA-F]+' "$snapshot_dir/heap.txt" | awk '!seen[$0]++' | head -n "$max_addresses" >"$snapshot_dir/heap_addresses.txt" || true
  while IFS= read -r address; do
    [[ -z "$address" ]] && continue
    malloc_history "$pid" "$address" >"$snapshot_dir/malloc_history_${address}.txt" 2>&1 || true
  done <"$snapshot_dir/heap_addresses.txt"
}

env \
  MallocStackLogging=1 \
  PIXELART_TRACE_MEMORY=1 \
  PIXELART_TRACE_MEMORY_FILE="$trace_csv" \
  "$app" --import-image "$image" >"$stdout_log" 2>&1 &
pid="$!"

# Keep MallocStackLogging scoped to the app process. If this script itself was
# launched with malloc logging enabled, unset it before running helper tools.
unset MallocStackLogging
unset MallocStackLoggingNoCompact

cleanup() {
  if kill -0 "$pid" >/dev/null 2>&1; then
    if [[ "${PIXELART_PROFILE_KEEP_APP:-0}" != "1" ]]; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  fi
}
trap cleanup EXIT

echo "second,rss_kb,rss_bytes" >"$rss_csv"
for second in $(seq 0 "$seconds"); do
  if ! kill -0 "$pid" >/dev/null 2>&1; then
    break
  fi
  rss_kb="$(ps -o rss= -p "$pid" | tr -d '[:space:]')"
  if [[ -n "$rss_kb" ]]; then
    rss_bytes=$((rss_kb * 1024))
    echo "$second,$rss_kb,$rss_bytes" >>"$rss_csv"
    if [[ -z "$high_snapshot_dir" && "$rss_bytes" -ge "$capture_threshold_bytes" ]]; then
      high_snapshot_dir="allocator_snapshot_second_${second}"
      capture_allocator_snapshot "$high_snapshot_dir"
    fi
  fi
  sleep 1
done

if kill -0 "$pid" >/dev/null 2>&1; then
  vmmap -summary "$pid" >"$vmmap_file" 2>&1 || true
  heap -sortBySize "$pid" >"$heap_file" 2>&1 || heap "$pid" >"$heap_file" 2>&1 || true
  grep -Eo '0x[0-9a-fA-F]+' "$heap_file" | awk '!seen[$0]++' | head -n "$max_addresses" >"$addresses_file" || true

  while IFS= read -r address; do
    [[ -z "$address" ]] && continue
    malloc_history "$pid" "$address" >"$out_dir/malloc_history_${address}.txt" 2>&1 || true
  done <"$addresses_file"
fi

{
  echo "PixelArt Blue Marble memory profile"
  echo "app=$app"
  echo "image=$image"
  echo "pid=$pid"
  echo "seconds=$seconds"
  echo "high_rss_capture_threshold_gib=$capture_threshold_gib"
  echo
  echo "RSS peak:"
  awk -F, 'NR > 1 && $3 > max { max = $3; sec = $1 } END { if (max) printf("  second=%s rss_bytes=%s rss_gib=%.3f\n", sec, max, max / 1024 / 1024 / 1024); else print "  no samples"; }' "$rss_csv"
  echo
  echo "Largest internal trace vector/buffer rows:"
  if [[ -f "$trace_csv" ]]; then
    trace_top="$out_dir/internal_trace_top.txt"
    awk -F, 'NR > 1 && $12 + 0 > 0 { printf("  bytes=%s event=%s label=%s stack=%s rss=%s\n", $12, $3, $7, $5, $6); }' "$trace_csv" \
      | sort -t= -k2,2nr >"$trace_top" || true
    head -n 30 "$trace_top"
  else
    echo "  trace CSV not found"
  fi
  echo
  echo "High-RSS allocator snapshot:"
  if [[ -n "$high_snapshot_dir" ]]; then
    echo "  $out_dir/$high_snapshot_dir"
  else
    echo "  no sample crossed ${capture_threshold_gib} GiB"
  fi
  echo
  echo "MALLOC_LARGE summary:"
  if [[ -f "$vmmap_file" ]]; then
    grep -i "MALLOC_LARGE" "$vmmap_file" || echo "  no MALLOC_LARGE line found"
  else
    echo "  vmmap output not found"
  fi
  echo
  echo "Resolved malloc_history files:"
  find "$out_dir" -maxdepth 1 -name 'malloc_history_*.txt' -print | sort | sed 's/^/  /'
  echo
  echo "Raw outputs:"
  echo "  $trace_csv"
  echo "  $rss_csv"
  echo "  $vmmap_file"
  echo "  $heap_file"
  echo "  $stdout_log"
} >"$report_file"

cat "$report_file"
