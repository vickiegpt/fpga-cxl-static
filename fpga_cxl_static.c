// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/memory_hotplug.h>
#include <linux/moduleparam.h>
#include <linux/overflow.h>
#include <linux/sizes.h>

static unsigned long long hpa_start;
module_param(hpa_start, ullong, 0400);
MODULE_PARM_DESC(hpa_start, "CXL HPA start address");

static unsigned long long hpa_size;
module_param(hpa_size, ullong, 0400);
MODULE_PARM_DESC(hpa_size, "CXL HPA size");

static int nid = NUMA_NO_NODE;
module_param(nid, int, 0400);
MODULE_PARM_DESC(nid, "Target NUMA node");

static bool commit;
module_param(commit, bool, 0400);
MODULE_PARM_DESC(commit, "Actually hot-add memory; dangerous");

static struct resource *cxl_res;
static unsigned long long hpa_end;
static bool memory_added;

static int __init fpga_cxl_static_init(void)
{
	unsigned long block_size;
	int rc;

	if (!hpa_start || !hpa_size) {
		pr_err("hpa_start and hpa_size are required\n");
		return -EINVAL;
	}

	if (check_add_overflow(hpa_start, hpa_size - 1, &hpa_end)) {
		pr_err("HPA range overflows\n");
		return -EOVERFLOW;
	}

	block_size = memory_block_size_bytes();
	if (!IS_ALIGNED(hpa_start, block_size) ||
	    !IS_ALIGNED(hpa_size, block_size)) {
		pr_err("range must be aligned to memory block size %#lx\n",
		       block_size);
		return -EINVAL;
	}

	/*
	 * This must be a firmware/HDM-decoder configured CXL.mem HPA range,
	 * not a PCI BAR. request_mem_region() intentionally rejects overlaps.
	 */
	cxl_res = request_mem_region(hpa_start, hpa_size,
				     "fpga-cxl-static");
	if (!cxl_res) {
		pr_err("HPA range [%#llx-%#llx] is busy or overlaps another resource\n",
		       hpa_start, hpa_end);
		return -EBUSY;
	}

	pr_info("reserved HPA [%#llx-%#llx], size=%llu MiB, nid=%d\n",
		hpa_start, hpa_end, hpa_size >> 20, nid);

	if (!commit) {
		pr_warn("validation-only mode; memory was NOT hot-added\n");
		return 0;
	}

	rc = add_memory_driver_managed(nid, hpa_start, hpa_size,
				       "fpga-cxl-static", MHP_NONE);
	if (rc) {
		pr_err("add_memory_driver_managed failed: %d\n", rc);
		release_mem_region(hpa_start, hpa_size);
		cxl_res = NULL;
		return rc;
	}

	memory_added = true;
	pr_warn("memory hot-added; keep it offline until the range is fully validated\n");
	pr_warn("do not unload this module while any added memory block is online\n");
	return 0;
}

static void __exit fpga_cxl_static_exit(void)
{
	if (memory_added) {
		pr_err("refusing to remove hot-added memory automatically\n");
		pr_err("offline and remove all blocks before unloading in a production driver\n");
		return;
	}

	if (cxl_res)
		release_mem_region(hpa_start, hpa_size);
}

module_init(fpga_cxl_static_init);
module_exit(fpga_cxl_static_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vickiegpt");
MODULE_DESCRIPTION("Experimental static CXL HPA memory registrar");
