#!/usr/bin/env bash
set -euo pipefail

echo '== kernel =='
uname -a

echo
echo '== memory hotplug configuration =='
CONFIG="/boot/config-$(uname -r)"
if [[ -r "$CONFIG" ]]; then
  grep -E 'CONFIG_MEMORY_HOTPLUG|CONFIG_MEMORY_HOTREMOVE|CONFIG_NUMA|CONFIG_SPARSEMEM' "$CONFIG" || true
else
  echo "Cannot read $CONFIG"
fi

echo
echo '== memory block size =='
cat /sys/devices/system/memory/block_size_bytes 2>/dev/null || true

echo
echo '== NUMA topology =='
numactl -H 2>/dev/null || true

echo
echo '== high physical-memory resources =='
cat /proc/iomem

echo
echo '== CXL / DAX log =='
dmesg | grep -iE 'cxl|dax|hmem|decoder|memory|efi' | tail -300 || true
