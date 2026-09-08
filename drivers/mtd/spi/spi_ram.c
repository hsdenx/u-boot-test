// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2019 S. Bigler Securiton AG
 * Copyright (C) 2023 Heiko Schocher, DENX Software Engineering, hs@denx.de.
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <log.h>
#include <malloc.h>
#include <spi.h>
#include <spi_flash.h>
#include <vsprintf.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sizes.h>

/* Instruction set of the 23x series serial SRAMs */
#define MCHP23_CMD_WRITE_STATUS	0x01
#define MCHP23_CMD_WRITE	0x02
#define MCHP23_CMD_READ		0x03

/* MODE register, bits 7:6 select byte, page or sequential operation */
#define MCHP23_MODE_SEQ		BIT(6)

/* The 1 Mbit parts take a 24 bit address */
#define MCHP23_ADDR_LEN		3

/* Erasing leaves behind what a flash would read after an erase */
#define SPI_RAM_ERASE_BYTE	0xff

/* An erase can cover the whole device, so stage it in bounded steps */
#define SPI_RAM_ERASE_STEP	SZ_1K

struct spi_ram_data {
	u32 size;
	const char *name;
};

static int spi_ram_xfer(struct spi_slave *spi, const u8 *cmd, size_t cmd_len,
			const void *data_out, void *data_in, size_t data_len)
{
	unsigned long flags = SPI_XFER_BEGIN;
	int ret;

	if (!data_len)
		flags |= SPI_XFER_END;

	ret = spi_xfer(spi, cmd_len * 8, cmd, NULL, flags);
	if (ret) {
		log_debug("failed to send command (%zu bytes): %d\n",
			  cmd_len, ret);
		return ret;
	}

	if (!data_len)
		return 0;

	ret = spi_xfer(spi, data_len * 8, data_out, data_in, SPI_XFER_END);
	if (ret)
		log_debug("failed to transfer %zu bytes of data: %d\n",
			  data_len, ret);

	return ret;
}

static int spi_ram_command(struct udevice *dev, u8 opcode, u32 offset,
			   const void *data_out, void *data_in, size_t len)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	u8 cmd[1 + MCHP23_ADDR_LEN];
	int ret;

	cmd[0] = opcode;
	cmd[1] = offset >> 16;
	cmd[2] = offset >> 8;
	cmd[3] = offset;

	ret = spi_claim_bus(flash->spi);
	if (ret) {
		dev_dbg(dev, "failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_ram_xfer(flash->spi, cmd, sizeof(cmd), data_out, data_in,
			   len);

	spi_release_bus(flash->spi);

	return ret;
}

static int spi_ram_read(struct udevice *dev, u32 offset, size_t len, void *buf)
{
	return spi_ram_command(dev, MCHP23_CMD_READ, offset, NULL, buf, len);
}

static int spi_ram_write(struct udevice *dev, u32 offset, size_t len,
			 const void *buf)
{
	return spi_ram_command(dev, MCHP23_CMD_WRITE, offset, buf, NULL, len);
}

/*
 * A RAM needs no erase before a write, but a caller that asks for one
 * expects the area to read back the way an erased flash would, so fill it.
 */
static int spi_ram_erase(struct udevice *dev, u32 offset, size_t len)
{
	size_t step = min_t(size_t, len, SPI_RAM_ERASE_STEP);
	u8 *buf;
	int ret = 0;

	buf = malloc(step);
	if (!buf)
		return -ENOMEM;

	memset(buf, SPI_RAM_ERASE_BYTE, step);

	while (len) {
		step = min_t(size_t, len, SPI_RAM_ERASE_STEP);

		ret = spi_ram_write(dev, offset, step, buf);
		if (ret)
			break;

		offset += step;
		len -= step;
	}

	free(buf);

	return ret;
}

static int spi_ram_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct spi_flash *flash = mtd->priv;
	int ret;

	ret = spi_ram_erase(flash->dev, instr->addr, instr->len);
	if (ret)
		return ret;

	instr->state = MTD_ERASE_DONE;

	return 0;
}

static int spi_ram_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			    size_t *retlen, u_char *buf)
{
	struct spi_flash *flash = mtd->priv;
	int ret;

	ret = spi_ram_read(flash->dev, from, len, buf);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int spi_ram_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			     size_t *retlen, const u_char *buf)
{
	struct spi_flash *flash = mtd->priv;
	int ret;

	ret = spi_ram_write(flash->dev, to, len, buf);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int spi_ram_probe(struct udevice *dev)
{
	const struct spi_ram_data *data =
		(const struct spi_ram_data *)dev_get_driver_data(dev);
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct spi_slave *spi = dev_get_parent_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	u8 cmd[2];
	int ret;

	flash->dev = dev;
	flash->spi = spi;

	ret = spi_claim_bus(spi);
	if (ret) {
		dev_dbg(dev, "failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	/*
	 * The device powers up in byte operation, where a command transfers
	 * exactly one byte. Select sequential operation instead, so that a
	 * transfer of any length is served by a single command.
	 */
	cmd[0] = MCHP23_CMD_WRITE_STATUS;
	cmd[1] = MCHP23_MODE_SEQ;
	ret = spi_ram_xfer(spi, cmd, sizeof(cmd), NULL, NULL, 0);

	spi_release_bus(spi);

	if (ret) {
		dev_dbg(dev, "failed to select sequential operation: %d\n",
			ret);
		return ret;
	}

	sprintf(flash->mtd_name, "ram%d", dev_seq(dev));

	mtd->name = flash->mtd_name;
	mtd->dev = dev;
	mtd->priv = flash;
	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = data->size;
	mtd->writesize = 1;
	mtd->erasesize = 1;
	mtd->_erase = spi_ram_mtd_erase;
	mtd->_read = spi_ram_mtd_read;
	mtd->_write = spi_ram_mtd_write;

	flash->name = data->name;
	flash->size = data->size;
	flash->erase_size = mtd->erasesize;
	flash->sector_size = mtd->erasesize;

	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		return add_mtd_device(mtd);

	return 0;
}

static int spi_ram_remove(struct udevice *dev)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);

	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		return del_mtd_device(&flash->mtd);

	return 0;
}

static const struct dm_spi_flash_ops spi_ram_ops = {
	.read = spi_ram_read,
	.write = spi_ram_write,
	.erase = spi_ram_erase,
};

static const struct spi_ram_data mchp23lcv1024_data = {
	.size = SZ_128K,
	.name = "mchp23lcv1024",
};

static const struct udevice_id spi_ram_ids[] = {
	{ .compatible = "microchip,mchp23lcv1024",
	  .data = (ulong)&mchp23lcv1024_data },
	{ }
};

U_BOOT_DRIVER(spi_ram) = {
	.name		= "spi_ram",
	.id		= UCLASS_SPI_FLASH,
	.of_match	= spi_ram_ids,
	.probe		= spi_ram_probe,
	.remove		= spi_ram_remove,
	.ops		= &spi_ram_ops,
};
