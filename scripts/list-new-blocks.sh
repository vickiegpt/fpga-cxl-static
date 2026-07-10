#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <hpa_start> <hpa_size>" >&2
  exit 2
fi

START=$(( $1 ))
SIZE=$(( $2 ))
END=$(( START + SIZE - 1 ))
BLOCK_SIZE=$(( $(cat /sys/devices/system/memory/block_size_bytes) ))

printf 'Target HPA: 0x%x-0x%x\n' "$START" "$END"
printf 'Memory block size: 0x%x\n' "$BLOCK_SIZE"

for path in /sys/devices/system/memory/memory[0-9]*; do
  [[ -e "$path/phys_index" ]] || continue
  idx=$(( 16#$(cat "$path/phys_index") ))
  block_start=$(( idx * BLOCK_SIZE ))
  block_end=$(( block_start + BLOCK_SIZE - 1 ))

  if (( block_start >= START && block_end <= END )); then
    state=$(cat "$path/state")
    printf '%s 0x%x-0x%x %s\n' "$(basename "$path")" "$block_start" "$block_end" "$state"
  fi
done
