// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2019 Stefan Bigler <stefan.bigler@securiton.ch>
 * Copyright (C) 2023 Heiko Schocher, DENX Software Engineering, hs@denx.de
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <log.h>
#include <spi.h>
#include <vsprintf.h>
#include <linux/bitops.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>

/* Instruction set of the 23x series serial SRAMs */
#define MCHP23_CMD_WRITE_STATUS	0x01
#define MCHP23_CMD_WRITE	0x02
#define MCHP23_CMD_READ		0x03

/* MODE register, bits 7:6 select byte, page or sequential operation */
#define MCHP23_MODE_SEQ		BIT(6)

/* The 1 Mbit parts take a 24 bit address */
#define MCHP23_ADDR_LEN		3

/**
 * struct spi_ram_data - what one supported device looks like
 *
 * @size:	usable size of the array in bytes
 * @name:	name the MTD device is registered under
 */
struct spi_ram_data {
	u32 size;
	const char *name;
};

/**
 * struct spi_ram_priv - per device state
 *
 * @name:	MTD name, built from the device name and the sequence number
 *		so that several of these can coexist
 */
struct spi_ram_priv {
	char name[16];
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

/**
 * spi_ram_command() - run one read or write command over the whole length
 *
 * Sequential operation was selected during probe, so the device serves a
 * transfer of any length from a single command and the bus is claimed once.
 *
 * @dev:	the SRAM device
 * @opcode:	MCHP23_CMD_READ or MCHP23_CMD_WRITE
 * @offset:	byte offset into the array
 * @data_out:	data to write, NULL when reading
 * @data_in:	where to put data read, NULL when writing
 * @len:	number of bytes to transfer
 * Return: 0 on success, negative on error
 */
static int spi_ram_command(struct udevice *dev, u8 opcode, u32 offset,
			   const void *data_out, void *data_in, size_t len)
{
	struct spi_slave *spi = dev_get_parent_priv(dev);
	u8 cmd[1 + MCHP23_ADDR_LEN];
	int ret;

	cmd[0] = opcode;
	cmd[1] = offset >> 16;
	cmd[2] = offset >> 8;
	cmd[3] = offset;

	ret = spi_claim_bus(spi);
	if (ret) {
		dev_dbg(dev, "failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_ram_xfer(spi, cmd, sizeof(cmd), data_out, data_in, len);

	spi_release_bus(spi);

	return ret;
}

static int spi_ram_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	int ret;

	ret = spi_ram_command(mtd->dev, MCHP23_CMD_READ, from, NULL, buf, len);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int spi_ram_write(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int ret;

	ret = spi_ram_command(mtd->dev, MCHP23_CMD_WRITE, to, buf, NULL, len);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int spi_ram_probe(struct udevice *dev)
{
	const struct spi_ram_data *data =
		(const struct spi_ram_data *)dev_get_driver_data(dev);
	struct mtd_info *mtd = dev_get_uclass_priv(dev);
	struct spi_ram_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = dev_get_parent_priv(dev);
	u8 cmd[2];
	int ret;

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

	snprintf(priv->name, sizeof(priv->name), "%s%d", data->name,
		 dev_seq(dev));
	mtd->name = priv->name;
	mtd->dev = dev;
	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = data->size;
	mtd->writesize = 1;
	mtd->writebufsize = 1;

	/*
	 * No _erase: a RAM has nothing to erase, which MTD_NO_ERASE in the
	 * flags above states, and mtd_erase() answers -EOPNOTSUPP for a device
	 * that provides none.
	 */
	mtd->_read = spi_ram_read;
	mtd->_write = spi_ram_write;

	return add_mtd_device(mtd);
}

static int spi_ram_remove(struct udevice *dev)
{
	return del_mtd_device(dev_get_uclass_priv(dev));
}

static const struct spi_ram_data mchp23lcv1024_data = {
	.size = SZ_128K,
	.name = "spi-ram",
};

static const struct udevice_id spi_ram_ids[] = {
	{ .compatible = "microchip,mchp23lcv1024",
	  .data = (ulong)&mchp23lcv1024_data },
	{ }
};

U_BOOT_DRIVER(spi_ram) = {
	.name		= "spi_ram",
	.id		= UCLASS_MTD,
	.of_match	= spi_ram_ids,
	.probe		= spi_ram_probe,
	.remove		= spi_ram_remove,
	.priv_auto	= sizeof(struct spi_ram_priv),
};
