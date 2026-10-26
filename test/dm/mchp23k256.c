// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for the Microchip 23K256 and 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <mtd.h>
#include <spi.h>
#include <asm/test.h>
#include <dm/device-internal.h>
#include <dm/test.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>
#include <test/test.h>
#include <test/ut.h>

/* The 23LCV1024 and the 23K256 of the test device tree */
#define MCHP23_PATH		"/spi@1/sram@0"
#define MCHP23_SMALL_PATH	"/spi@1/sram@1"

/*
 * Long enough to run past the 32 byte page of the device, and starting
 * just before a page boundary, so that the transfer only comes back
 * unharmed if the driver put the device into sequential operation.
 */
#define MCHP23_OFFSET		0x1f
#define MCHP23_LEN		100

/*
 * A write command with its address and data in one transfer, opcode 0x02,
 * the 24 bit address of the 23LCV1024 and four data bytes.
 */
#define MCHP23_ONE_ADDR		0x100
#define MCHP23_ONE_DATA_LEN	4

/*
 * The second partition in the test device tree. Its offset is not aligned
 * to any erase block size, which does not matter for a device without
 * erase requirements, where writes are aligned to single bytes.
 */
#define MCHP23_PART		"spi-ram-part1"
#define MCHP23_PART_OFFSET	0xa001
#define MCHP23_PART_SIZE	0x1000

static int dm_test_mchp23k256(struct unit_test_state *uts)
{
	struct udevice *dev, *emul;
	struct erase_info instr = { };
	struct mtd_info *mtd, *found;
	u8 src[MCHP23_LEN], dst[MCHP23_LEN];
	u8 one[] = { 0x02, (MCHP23_ONE_ADDR >> 16) & 0xff,
		     (MCHP23_ONE_ADDR >> 8) & 0xff, MCHP23_ONE_ADDR & 0xff,
		     0xde, 0xad, 0xbe, 0xef };
	size_t retlen;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(MCHP23_PATH),
						&dev));

	mtd = dev_get_uclass_priv(dev);
	ut_asserteq(SZ_128K, mtd->size);
	ut_asserteq(MTD_RAM, mtd->type);

	/*
	 * Registering with the MTD layer is what makes the mtd command able
	 * to reach the device, so check the name it was given resolves back
	 * to it rather than checking the string itself.
	 */
	found = get_mtd_device_nm(mtd->name);
	ut_asserteq_ptr(mtd, found);
	put_mtd_device(found);

	/* The emulation the slave points at, probed by the lookup */
	ut_assertok(uclass_get_device_by_phandle(UCLASS_SPI_EMUL, dev,
						 "sandbox,emul", &emul));

	/*
	 * The device powers up in sequential mode, but its current mode
	 * may have been changed by a previous user. Switch to byte mode,
	 * where each command transfers one byte, and probe again to ensure
	 * the driver selects sequential mode itself.
	 */
	sandbox_mchp23k256_force_byte_mode(emul);
	ut_assertok(device_remove(dev, DM_REMOVE_NORMAL));
	ut_assertok(device_probe(dev));
	ut_assert(sandbox_mchp23k256_is_sequential(emul));

	mtd = dev_get_uclass_priv(dev);

	for (i = 0; i < MCHP23_LEN; i++)
		src[i] = i + 1;

	ut_assertok(mtd_write(mtd, MCHP23_OFFSET, MCHP23_LEN, &retlen, src));
	ut_asserteq(MCHP23_LEN, retlen);

	memset(dst, 0, MCHP23_LEN);
	ut_assertok(mtd_read(mtd, MCHP23_OFFSET, MCHP23_LEN, &retlen, dst));
	ut_asserteq(MCHP23_LEN, retlen);
	ut_asserteq_mem(src, dst, MCHP23_LEN);

	/* Neighbouring cells must not have been touched */
	ut_assertok(mtd_read(mtd, 0, MCHP23_OFFSET, &retlen, dst));
	for (i = 0; i < MCHP23_OFFSET; i++)
		ut_asserteq(0, dst[i]);

	/*
	 * The device does not support erase and sets MTD_NO_ERASE. Reject
	 * erase requests rather than pretending the data was cleared.
	 * Request the erase over the data just written so the following
	 * check would detect an erase that incorrectly succeeded.
	 */
	ut_assert(mtd->flags & MTD_NO_ERASE);
	instr.addr = MCHP23_OFFSET;
	instr.len = MCHP23_LEN;
	ut_asserteq(-EOPNOTSUPP, mtd_erase(mtd, &instr));

	ut_assertok(mtd_read(mtd, MCHP23_OFFSET, MCHP23_LEN, &retlen, dst));
	ut_asserteq_mem(src, dst, MCHP23_LEN);

	/*
	 * The device sees a byte stream while the chip select is asserted,
	 * so a master that sends the write command, the address and the data
	 * in one transfer has to get the same result as the driver, which
	 * sends the data in a second one.
	 */
	ut_assertok(spi_claim_bus(dev_get_parent_priv(dev)));
	ut_assertok(spi_xfer(dev_get_parent_priv(dev), sizeof(one) * 8, one,
			     NULL, SPI_XFER_BEGIN | SPI_XFER_END));
	spi_release_bus(dev_get_parent_priv(dev));

	ut_assertok(mtd_read(mtd, MCHP23_ONE_ADDR, MCHP23_ONE_DATA_LEN,
			     &retlen, dst));
	ut_asserteq_mem(&one[sizeof(one) - MCHP23_ONE_DATA_LEN], dst,
			MCHP23_ONE_DATA_LEN);

	return 0;
}
DM_TEST(dm_test_mchp23k256, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* The 23K256 is a quarter of the size and takes a two byte address */
static int dm_test_mchp23k256_small(struct unit_test_state *uts)
{
	u8 src[MCHP23_LEN], dst[MCHP23_LEN];
	struct mtd_info *mtd;
	struct udevice *dev;
	size_t retlen;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(MCHP23_SMALL_PATH),
						&dev));

	mtd = dev_get_uclass_priv(dev);
	ut_asserteq(SZ_32K, mtd->size);

	for (i = 0; i < MCHP23_LEN; i++)
		src[i] = 0x80 - i;

	/* An offset the two byte address reaches, but not in its low byte */
	ut_assertok(mtd_write(mtd, SZ_16K + MCHP23_OFFSET, MCHP23_LEN,
			      &retlen, src));
	ut_asserteq(MCHP23_LEN, retlen);

	memset(dst, 0, MCHP23_LEN);
	ut_assertok(mtd_read(mtd, SZ_16K + MCHP23_OFFSET, MCHP23_LEN,
			     &retlen, dst));
	ut_asserteq_mem(src, dst, MCHP23_LEN);

	/* Nothing may have landed at the offset the low byte alone gives */
	ut_assertok(mtd_read(mtd, MCHP23_OFFSET, MCHP23_LEN, &retlen, dst));
	for (i = 0; i < MCHP23_LEN; i++)
		ut_asserteq(0, dst[i]);

	return 0;
}
DM_TEST(dm_test_mchp23k256_small, UTF_SCAN_PDATA | UTF_SCAN_FDT);

static int dm_test_mchp23k256_part(struct unit_test_state *uts)
{
	struct mtd_info *mtd, *part;
	struct udevice *dev;
	u8 src[MCHP23_LEN], dst[MCHP23_LEN];
	size_t retlen;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(MCHP23_PATH),
						&dev));
	mtd = dev_get_uclass_priv(dev);

	/*
	 * This is what the mtd command does through mtd_probe_devices(),
	 * called here for this device alone so that the test does not depend
	 * on what else is registered with the MTD layer.
	 */
	ut_assertok(add_mtd_partitions_of(mtd));

	part = get_mtd_device_nm(MCHP23_PART);
	ut_assert(!IS_ERR_OR_NULL(part));

	/*
	 * The device has no erase size to align a partition to, so the
	 * partition is aligned on the write size of one byte and stays
	 * writeable at an offset no erase block would allow.
	 */
	ut_assert(part->flags & MTD_NO_ERASE);
	ut_assert(part->flags & MTD_WRITEABLE);
	ut_asserteq(MCHP23_PART_SIZE, part->size);

	for (i = 0; i < MCHP23_LEN; i++)
		src[i] = 0xa0 + i;

	ut_assertok(mtd_write(part, 0, MCHP23_LEN, &retlen, src));
	ut_asserteq(MCHP23_LEN, retlen);
	put_mtd_device(part);

	/* what the partition took has to land at its offset in the device */
	memset(dst, 0, MCHP23_LEN);
	ut_assertok(mtd_read(mtd, MCHP23_PART_OFFSET, MCHP23_LEN, &retlen,
			     dst));
	ut_asserteq_mem(src, dst, MCHP23_LEN);

	return 0;
}
DM_TEST(dm_test_mchp23k256_part, UTF_SCAN_PDATA | UTF_SCAN_FDT);
