# fpga-cxl-static

Experimental Linux 6.16 kernel module for exposing a **preconfigured static CXL.mem HPA range** as hot-pluggable System RAM.

This is intended for FPGA Type-3 / legacy bring-up where firmware and FPGA HDM decoders already establish the HPA-to-DPA mapping, but Linux cannot build a normal CXL port/region topology.

## Important safety warning

This module does **not** discover, configure, or validate CXL decoders. Supplying a PCI BAR, an unbacked host window, an incorrect size, or memory that disappears after FPGA reset can hang the machine or corrupt data.

Use only when all of the following are true:

- The address is a CPU-visible, cache-coherent CXL.mem HPA range, not PCI MMIO.
- Host/root and endpoint HDM decoders are already programmed.
- The entire requested range has stable backing memory.
- FPGA DDR calibration and media-ready state are complete.
- The FPGA and CXL link remain available until reboot.

## Kernel requirements

The current code targets Linux 6.16 and requires at least:

```text
CONFIG_MEMORY_HOTPLUG=y
CONFIG_MEMORY_HOTREMOVE=y        # strongly recommended
CONFIG_NUMA=y
CONFIG_SPARSEMEM=y
```

Check with:

```bash
grep -E 'CONFIG_MEMORY_HOTPLUG|CONFIG_MEMORY_HOTREMOVE|CONFIG_NUMA|CONFIG_SPARSEMEM' \
    /boot/config-$(uname -r)
```

Install the matching kernel build tree or headers, then build:

```bash
make
```

For an explicit source tree:

```bash
make KDIR=/home/labuser/stellia_6.17/linux-6.16.7
```

## 1. Boot safely

Keep the CXL range out of the normal allocator and make newly hot-added blocks default to offline. For the current system, continue using the known-good DRAM limit while validating:

```text
mem=64G memhp_default_state=offline ignore_loglevel loglevel=8
```

`mem=64G` is a temporary containment measure, not the final solution.

## 2. Determine the real HPA and capacity

Do not copy addresses from another machine or use a PCI BAR. Inspect:

```bash
cat /proc/iomem
sudo dmesg | grep -iE 'cxl|memory|hpa|decoder|reserved|dax'
numactl -H
```

The requested start and size must be aligned to the kernel memory-block size:

```bash
cat /sys/devices/system/memory/block_size_bytes
```

## 3. Validation-only load

This reserves the range but does not hot-add it:

```bash
sudo insmod fpga_cxl_static.ko \
    hpa_start=0xYOUR_HPA_START \
    hpa_size=0xYOUR_TEST_SIZE \
    nid=YOUR_EXISTING_NUMA_NODE \
    commit=0

dmesg | tail -50
```

Unload validation mode with:

```bash
sudo rmmod fpga_cxl_static
```

Start with one memory block, then increase gradually. Do not begin with a multi-terabyte firmware window when the FPGA has only a few GiB of backing memory.

## 4. Hot-add as offline System RAM

After the complete requested range has been independently validated:

```bash
sudo insmod fpga_cxl_static.ko \
    hpa_start=0xYOUR_HPA_START \
    hpa_size=0xYOUR_VALIDATED_SIZE \
    nid=YOUR_EXISTING_NUMA_NODE \
    commit=1
```

By default, the module pins itself after a successful add so it cannot be unloaded unsafely. Reboot to reset the experiment.

Inspect the added blocks:

```bash
lsmem
cat /proc/iomem | grep -i -E 'fpga-cxl|System RAM'
grep -H . /sys/devices/system/memory/memory*/state
```

Do not online everything at once. Identify only the blocks belonging to the requested HPA range and bring them online one at a time as movable memory:

```bash
echo online_movable | sudo tee /sys/devices/system/memory/memoryX/state
```

Verify after each block:

```bash
numactl -H
lsmem
```

## Parameters

| Parameter | Meaning | Default |
|---|---|---|
| `hpa_start` | Preconfigured CXL.mem host physical start | required |
| `hpa_size` | Backed and validated range size | required |
| `nid` | Existing target NUMA node | required |
| `commit` | Hot-add when `1`; validation only when `0` | `0` |
| `pin_after_add` | Prevent module unload after hot-add | `1` |

## What this module does not solve

It does not fix:

- `CXL port topology not found`
- an isolated or disabled `/dev/cxl/mem0`
- missing ACPI CEDT/CFMWS/CHBS entries
- missing CXL DVSEC or Register Locator capabilities
- incorrect HDM decoder base, size, interleave, or DPA mapping
- incorrect FPGA memory capacity reporting

It bypasses the normal CXL region-management path and registers an already configured static HPA range through Linux memory hotplug.

## Recovery

If enabling `CXL Type 3 Legacy` causes an early-kernel hang, boot again with the known-good `mem=64G` limit. Do not use `commit=1` until the full target range is known to be accessible.

## Status

Experimental bring-up code. It has not been validated on the user's exact FPGA/server combination and should not be used on production systems.
