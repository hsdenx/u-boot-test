// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for the Microchip 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <mtd.h>
#include <spi.h>
#include <spi_flash.h>
#include <asm/state.h>
#include <asm/test.h>
#include <dm/test.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>
#include <test/test.h>
#include <test/ut.h>

#define SPI_RAM_PATH	"/spi@1/sram@0"
#define SPI_RAM_CS	0

/*
 * Long enough to run past the 32 byte page of the device, and starting
 * just before a page boundary, so that the transfer only comes back
 * unharmed if the driver put the device into sequential operation.
 */
#define SPI_RAM_OFFSET	0x1f
#define SPI_RAM_LEN	100

/*
 * The second partition of the device in the test device tree. Its offset is
 * on no erase block boundary, which a device that cannot erase does not have
 * to care about.
 */
#define SPI_RAM_PART		"spi-ram-part1"
#define SPI_RAM_PART_OFFSET	0xa000
#define SPI_RAM_PART_SIZE	0x1000

static int dm_test_spi_ram(struct unit_test_state *uts)
{
	struct sandbox_state *state = state_get_current();
	struct udevice *dev, *bus, *emul;
	struct erase_info instr = { };
	struct mtd_info *mtd, *found;
	u8 src[SPI_RAM_LEN], dst[SPI_RAM_LEN];
	struct spi_slave *slave;
	size_t retlen;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(SPI_RAM_PATH),
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

	/*
	 * The device is useless in the byte operation it powers up in, so
	 * the probe has to have switched it over.
	 */
	bus = dev_get_parent(dev);
	ut_assertok(spi_get_bus_and_cs(dev_seq(bus), SPI_RAM_CS, &bus, &slave));
	ut_assertok(sandbox_spi_get_emul(state, bus, slave->dev, &emul));
	ut_assert(sandbox_spi_ram_is_sequential(emul));

	for (i = 0; i < SPI_RAM_LEN; i++)
		src[i] = i + 1;

	ut_assertok(mtd_write(mtd, SPI_RAM_OFFSET, SPI_RAM_LEN, &retlen, src));
	ut_asserteq(SPI_RAM_LEN, retlen);

	memset(dst, 0, SPI_RAM_LEN);
	ut_assertok(mtd_read(mtd, SPI_RAM_OFFSET, SPI_RAM_LEN, &retlen, dst));
	ut_asserteq(SPI_RAM_LEN, retlen);
	ut_asserteq_mem(src, dst, SPI_RAM_LEN);

	/* Neighbouring cells must not have been touched */
	ut_assertok(mtd_read(mtd, 0, SPI_RAM_OFFSET, &retlen, dst));
	for (i = 0; i < SPI_RAM_OFFSET; i++)
		ut_asserteq(0, dst[i]);

	/*
	 * The device cannot erase and says so through MTD_NO_ERASE, so a
	 * caller asking for one has to be told rather than left believing
	 * the area was cleared.
	 */
	ut_assert(mtd->flags & MTD_NO_ERASE);
	instr.addr = 0;
	instr.len = SPI_RAM_OFFSET;
	ut_asserteq(-EOPNOTSUPP, mtd_erase(mtd, &instr));

	ut_assertok(mtd_read(mtd, SPI_RAM_OFFSET, SPI_RAM_LEN, &retlen, dst));
	ut_asserteq_mem(src, dst, SPI_RAM_LEN);

	/*
	 * Since we are about to destroy all devices, we must tell sandbox
	 * to forget the emulation device
	 */
	sandbox_spi_unbind_emul(state, dev_seq(bus), SPI_RAM_CS);

	return 0;
}
DM_TEST(dm_test_spi_ram, UTF_SCAN_PDATA | UTF_SCAN_FDT);

static int dm_test_spi_ram_part(struct unit_test_state *uts)
{
	struct sandbox_state *state = state_get_current();
	struct udevice *dev, *bus;
	struct mtd_info *mtd, *part;
	u8 src[SPI_RAM_LEN], dst[SPI_RAM_LEN];
	size_t retlen;
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_MTD,
						ofnode_path(SPI_RAM_PATH),
						&dev));
	mtd = dev_get_uclass_priv(dev);

	/*
	 * This is what the mtd command does through mtd_probe_devices(),
	 * called here for this device alone so that the test does not depend
	 * on what else is registered with the MTD layer.
	 */
	ut_assertok(add_mtd_partitions_of(mtd));

	part = get_mtd_device_nm(SPI_RAM_PART);
	ut_assert(!IS_ERR_OR_NULL(part));

	/*
	 * The device has no erase size to align a partition to, so the
	 * partition is aligned on the write size and stays writeable, at
	 * whatever offset the device tree puts it.
	 */
	ut_assert(part->flags & MTD_NO_ERASE);
	ut_assert(part->flags & MTD_WRITEABLE);
	ut_asserteq(SPI_RAM_PART_SIZE, part->size);

	for (i = 0; i < SPI_RAM_LEN; i++)
		src[i] = 0xa0 + i;

	ut_assertok(mtd_write(part, 0, SPI_RAM_LEN, &retlen, src));
	ut_asserteq(SPI_RAM_LEN, retlen);
	put_mtd_device(part);

	/* what the partition took has to land at its offset in the device */
	memset(dst, 0, SPI_RAM_LEN);
	ut_assertok(mtd_read(mtd, SPI_RAM_PART_OFFSET, SPI_RAM_LEN, &retlen,
			     dst));
	ut_asserteq_mem(src, dst, SPI_RAM_LEN);

	bus = dev_get_parent(dev);
	sandbox_spi_unbind_emul(state, dev_seq(bus), SPI_RAM_CS);

	return 0;
}
DM_TEST(dm_test_spi_ram_part, UTF_SCAN_PDATA | UTF_SCAN_FDT);
