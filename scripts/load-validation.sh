#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: sudo $0 <hpa_start> <hpa_size> <nid>" >&2
  echo "Example: sudo $0 0x1080000000 0x80000000 2" >&2
  exit 2
fi

HPA_START="$1"
HPA_SIZE="$2"
NID="$3"

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root." >&2
  exit 1
fi

insmod ./fpga_cxl_static.ko \
  hpa_start="$HPA_START" \
  hpa_size="$HPA_SIZE" \
  nid="$NID" \
  commit=0

dmesg | tail -50
