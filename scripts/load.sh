#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: sudo $0 <hpa_start> <hpa_size> <nid> [commit=0|1]"
  echo "Example: sudo $0 0x1080000000 0x40000000 2 0"
}

[[ $# -ge 3 && $# -le 4 ]] || { usage; exit 2; }

HPA_START=$1
HPA_SIZE=$2
NID=$3
COMMIT=${4:-0}

if [[ $EUID -ne 0 ]]; then
  echo "Run as root." >&2
  exit 1
fi

if [[ "$COMMIT" == "1" ]]; then
  DEFAULT_STATE=$(cat /sys/devices/system/memory/auto_online_blocks 2>/dev/null || true)
  if [[ "$DEFAULT_STATE" != "offline" ]]; then
    echo "Refusing commit=1: new memory is not configured to remain offline." >&2
    echo "Boot with memhp_default_state=offline or run:" >&2
    echo "  echo offline > /sys/devices/system/memory/auto_online_blocks" >&2
    exit 1
  fi
fi

insmod ./fpga_cxl_static.ko \
  hpa_start="$HPA_START" \
  hpa_size="$HPA_SIZE" \
  nid="$NID" \
  commit="$COMMIT"

dmesg | tail -50
