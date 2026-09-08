// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for the Microchip 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <spi.h>
#include <spi_flash.h>
#include <asm/state.h>
#include <asm/test.h>
#include <dm/test.h>
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

static int dm_test_spi_ram(struct unit_test_state *uts)
{
	struct sandbox_state *state = state_get_current();
	struct udevice *dev, *bus, *emul;
	struct spi_flash *flash;
	u8 src[SPI_RAM_LEN], dst[SPI_RAM_LEN];
	int i;

	ut_assertok(uclass_get_device_by_ofnode(UCLASS_SPI_FLASH,
						ofnode_path(SPI_RAM_PATH),
						&dev));

	flash = dev_get_uclass_priv(dev);
	ut_asserteq(SZ_128K, flash->size);
	ut_asserteq_str("mchp23lcv1024", flash->name);

	/*
	 * The device is useless in the byte operation it powers up in, so
	 * the probe has to have switched it over.
	 */
	bus = dev_get_parent(dev);
	emul = state->spi[dev_seq(bus)][SPI_RAM_CS].emul;
	ut_assertnonnull(emul);
	ut_assert(sandbox_spi_ram_is_sequential(emul));

	for (i = 0; i < SPI_RAM_LEN; i++)
		src[i] = i + 1;

	ut_assertok(spi_flash_write_dm(dev, SPI_RAM_OFFSET, SPI_RAM_LEN, src));

	memset(dst, 0, SPI_RAM_LEN);
	ut_assertok(spi_flash_read_dm(dev, SPI_RAM_OFFSET, SPI_RAM_LEN, dst));
	ut_asserteq_mem(src, dst, SPI_RAM_LEN);

	/* Neighbouring cells must not have been touched */
	ut_assertok(spi_flash_read_dm(dev, 0, SPI_RAM_OFFSET, dst));
	for (i = 0; i < SPI_RAM_OFFSET; i++)
		ut_asserteq(0, dst[i]);

	/*
	 * A RAM needs no erase before a write, but an erase that was asked
	 * for has to leave behind what an erased flash reads, and it has to
	 * stop at the end of the range it was given.
	 */
	ut_assertok(spi_flash_erase_dm(dev, 0, SPI_RAM_OFFSET));

	ut_assertok(spi_flash_read_dm(dev, 0, SPI_RAM_OFFSET, dst));
	for (i = 0; i < SPI_RAM_OFFSET; i++)
		ut_asserteq(0xff, dst[i]);

	ut_assertok(spi_flash_read_dm(dev, SPI_RAM_OFFSET, SPI_RAM_LEN, dst));
	ut_asserteq_mem(src, dst, SPI_RAM_LEN);

	/* Now erase the data itself, over more than one staging step */
	ut_assertok(spi_flash_erase_dm(dev, SPI_RAM_OFFSET, SPI_RAM_LEN));
	ut_assertok(spi_flash_read_dm(dev, SPI_RAM_OFFSET, SPI_RAM_LEN, dst));
	for (i = 0; i < SPI_RAM_LEN; i++)
		ut_asserteq(0xff, dst[i]);

	/*
	 * Since we are about to destroy all devices, we must tell sandbox
	 * to forget the emulation device
	 */
	sandbox_sf_unbind_emul(state, dev_seq(bus), SPI_RAM_CS);

	return 0;
}
DM_TEST(dm_test_spi_ram, UTF_SCAN_PDATA | UTF_SCAN_FDT);
