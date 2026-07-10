// SPDX-License-Identifier: GPL-2.0
/*
 * Experimental static CXL HPA registrar for FPGA Type-3 bring-up.
 *
 * This module does not discover or program CXL decoders. The host and FPGA
 * must already map the supplied HPA range to stable, cache-coherent CXL.mem.
 */
#include <linux/ioport.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/overflow.h>

#define DRIVER_NAME "fpga-cxl-static"

static unsigned long long hpa_start;
module_param(hpa_start, ullong, 0400);
MODULE_PARM_DESC(hpa_start, "Preconfigured CXL.mem HPA start address");

static unsigned long long hpa_size;
module_param(hpa_size, ullong, 0400);
MODULE_PARM_DESC(hpa_size, "Preconfigured CXL.mem HPA size");

static int nid = NUMA_NO_NODE;
module_param(nid, int, 0400);
MODULE_PARM_DESC(nid, "Existing target NUMA node id");

static bool commit;
module_param(commit, bool, 0400);
MODULE_PARM_DESC(commit, "Hot-add the range when true; false is validation only");

static bool pin_after_add = true;
module_param(pin_after_add, bool, 0400);
MODULE_PARM_DESC(pin_after_add,
                 "Pin module after successful hot-add to prevent unsafe unload");

static struct resource *hpa_res;
static u64 hpa_end;
static unsigned long block_size;
static int mgid = -1;
static bool memory_added;
static bool module_pinned;

static int validate_parameters(void)
{
	if (!hpa_start || !hpa_size) {
		pr_err(DRIVER_NAME ": hpa_start and hpa_size are required\n");
		return -EINVAL;
	}

	if (nid < 0 || nid >= MAX_NUMNODES) {
		pr_err(DRIVER_NAME ": nid=%d is invalid; use an existing NUMA node\n",
		       nid);
		return -EINVAL;
	}

	if (check_add_overflow((u64)hpa_start, (u64)hpa_size - 1, &hpa_end)) {
		pr_err(DRIVER_NAME ": HPA range overflows\n");
		return -EOVERFLOW;
	}

	block_size = memory_block_size_bytes();
	if (!block_size) {
		pr_err(DRIVER_NAME ": memory block size is zero\n");
		return -EINVAL;
	}

	if (!IS_ALIGNED(hpa_start, block_size) ||
	    !IS_ALIGNED(hpa_size, block_size)) {
		pr_err(DRIVER_NAME
		       ": range must be aligned to memory block size %#lx\n",
		       block_size);
		return -EINVAL;
	}

	return 0;
}

static int reserve_hpa(void)
{
	/*
	 * This must be a firmware/HDM-decoder configured CXL.mem HPA range,
	 * never a PCI BAR. Like dax_kmem, reserve it first and leave BUSY clear
	 * so add_memory_driver_managed() can install a child System RAM resource.
	 */
	hpa_res = request_mem_region(hpa_start, hpa_size, DRIVER_NAME);
	if (!hpa_res) {
		pr_err(DRIVER_NAME
		       ": HPA [%#llx-%#llx] is busy or overlaps another resource\n",
		       hpa_start, hpa_end);
		return -EBUSY;
	}

	hpa_res->flags = IORESOURCE_SYSTEM_RAM;
	return 0;
}

static void release_hpa(void)
{
	if (!hpa_res)
		return;

	remove_resource(hpa_res);
	kfree(hpa_res);
	hpa_res = NULL;
}

static int __init fpga_cxl_static_init(void)
{
	int rc;

	rc = validate_parameters();
	if (rc)
		return rc;

	rc = reserve_hpa();
	if (rc)
		return rc;

	pr_info(DRIVER_NAME
		": reserved HPA [%#llx-%#llx], size=%llu MiB, nid=%d, block=%lu MiB\n",
		hpa_start, hpa_end, hpa_size >> 20, nid, block_size >> 20);

	if (!commit) {
		pr_warn(DRIVER_NAME
			": validation-only mode; memory was NOT hot-added\n");
		return 0;
	}

	mgid = memory_group_register_static(nid, PFN_UP(hpa_size));
	if (mgid < 0) {
		rc = mgid;
		mgid = -1;
		pr_err(DRIVER_NAME
		       ": memory_group_register_static failed: %d\n", rc);
		goto err_release;
	}

	rc = add_memory_driver_managed(mgid, hpa_start, hpa_size,
				       "System RAM (fpga-cxl-static)",
				       MHP_NID_IS_MGID);
	if (rc) {
		pr_err(DRIVER_NAME
		       ": add_memory_driver_managed failed: %d\n", rc);
		goto err_group;
	}

	memory_added = true;
	pr_warn(DRIVER_NAME
		": memory hot-added; keep all new memory blocks OFFLINE until tested\n");
	pr_warn(DRIVER_NAME
		": boot with memhp_default_state=offline before using commit=1\n");

	if (pin_after_add && try_module_get(THIS_MODULE)) {
		module_pinned = true;
		pr_info(DRIVER_NAME
			": module pinned to prevent unsafe unload; reboot to reset\n");
	}

	return 0;

err_group:
	memory_group_unregister(mgid);
	mgid = -1;
err_release:
	release_hpa();
	return rc;
}

static void __exit fpga_cxl_static_exit(void)
{
	/*
	 * With pin_after_add=1 this path cannot be reached after successful add.
	 * When explicitly disabled, removal is attempted only if every block is
	 * offline. A failed removal intentionally leaves the resource reserved.
	 */
	if (memory_added) {
#ifdef CONFIG_MEMORY_HOTREMOVE
		int rc = remove_memory(hpa_start, hpa_size);

		if (rc) {
			pr_err(DRIVER_NAME
			       ": remove_memory failed (%d); resource remains reserved\n",
			       rc);
			return;
		}
		memory_added = false;
#else
		pr_err(DRIVER_NAME
		       ": kernel lacks CONFIG_MEMORY_HOTREMOVE; resource remains reserved\n");
		return;
#endif
	}

	if (mgid >= 0) {
		memory_group_unregister(mgid);
		mgid = -1;
	}

	release_hpa();

	/* Normally unreachable because a pinned module cannot enter ->exit. */
	if (module_pinned)
		module_put(THIS_MODULE);
}

module_init(fpga_cxl_static_init);
module_exit(fpga_cxl_static_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vickiegpt");
MODULE_DESCRIPTION("Experimental Linux 6.16 static CXL HPA memory registrar");
MODULE_VERSION("0.2.0");
