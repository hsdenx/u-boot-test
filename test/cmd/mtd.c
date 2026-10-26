// SPDX-License-Identifier: GPL-2.0+
/*
 * Test for the mtd command on a device without an erase size
 *
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <mapmem.h>
#include <mtd.h>
#include <dm/test.h>
#include <linux/mtd/mtd.h>
#include <test/cmd.h>
#include <test/test.h>
#include <test/ut.h>

/* The SPI SRAM of the test device tree, an MTD device of type MTD_RAM */
#define MTD_RAM_PATH	"/spi@1/sram@0"

#define MTD_RAM_SRC	0x0
#define MTD_RAM_DST	0x100

/* Neither offset nor length is a multiple of any usual erase block */
#define MTD_RAM_OFFSET	0x1f
#define MTD_RAM_LEN	0x64

static int cmd_test_mtd_ram(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct mtd_info *mtd;
	u8 *out, *in;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(MTD_RAM_PATH),
						&dev));
	mtd = dev_get_uclass_priv(dev);
	ut_asserteq(0, mtd->erasesize);

	/*
	 * The first MTD command probes all devices, so output from other
	 * drivers during probing is irrelevant to this test.
	 */
	ut_assertok(run_command("mtd list", 0));
	console_record_reset();

	out = map_sysmem(MTD_RAM_SRC, MTD_RAM_LEN);
	in = map_sysmem(MTD_RAM_DST, MTD_RAM_LEN);
	for (i = 0; i < MTD_RAM_LEN; i++)
		out[i] = 0x5a + i;
	memset(in, 0, MTD_RAM_LEN);

	ut_assertok(run_commandf("mtd write %s 0x%x 0x%x 0x%x", mtd->name,
				 MTD_RAM_SRC, MTD_RAM_OFFSET, MTD_RAM_LEN));
	ut_assert_nextline("Writing %d byte(s) at offset 0x%08x", MTD_RAM_LEN,
			   MTD_RAM_OFFSET);
	ut_assert_console_end();

	ut_assertok(run_commandf("mtd read %s 0x%x 0x%x 0x%x", mtd->name,
				 MTD_RAM_DST, MTD_RAM_OFFSET, MTD_RAM_LEN));
	ut_assert_nextline("Reading %d byte(s) at offset 0x%08x", MTD_RAM_LEN,
			   MTD_RAM_OFFSET);
	ut_assert_console_end();
	ut_asserteq_mem(out, in, MTD_RAM_LEN);

	/* The erase has to be refused, not divided by the missing block size */
	ut_asserteq(1, run_commandf("mtd erase %s 0x%x 0x%x", mtd->name,
				    MTD_RAM_OFFSET, MTD_RAM_LEN));
	ut_assert_nextline("%s cannot erase", mtd->name);
	ut_assert_console_end();

	unmap_sysmem(in);
	unmap_sysmem(out);

	return 0;
}
CMD_TEST(cmd_test_mtd_ram, UTF_CONSOLE);
