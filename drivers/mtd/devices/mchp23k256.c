// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip 23K256 and 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2019 Stefan Bigler <stefan.bigler@securiton.ch>
 * Copyright (C) 2023 Heiko Schocher, DENX Software Engineering, hs@denx.de
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <spi.h>
#include <vsprintf.h>
#include <linux/bitops.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>

/* Instruction set of the 23x series serial SRAMs */
#define MCHP23K256_CMD_WRITE_STATUS	0x01
#define MCHP23K256_CMD_WRITE		0x02
#define MCHP23K256_CMD_READ		0x03

/* MODE register, bits 7:6 select byte, page or sequential operation */
#define MCHP23K256_MODE_SEQ		BIT(6)

/* The widest address of the supported parts, in bytes */
#define MCHP23K256_ADDR_MAX		3

/**
 * struct mchp23_caps - what one supported device looks like
 *
 * @size:	usable size of the array in bytes
 * @addr_width:	width of the address a command carries, in bytes
 */
struct mchp23_caps {
	u32 size;
	u8 addr_width;
};

/**
 * struct mchp23k256_priv - per device state
 *
 * @name:	MTD name, built from the technology and the sequence number
 *		so that several of these can coexist
 */
struct mchp23k256_priv {
	char name[16];
};

/**
 * mchp23k256_command() - run one read or write command over the whole length
 *
 * Sequential operation was selected during probe, so the device serves a
 * transfer of any length from a single command and the bus is claimed once.
 *
 * @dev:	the SRAM device
 * @opcode:	MCHP23K256_CMD_READ or MCHP23K256_CMD_WRITE
 * @offset:	byte offset into the array
 * @data_out:	data to write, NULL when reading
 * @data_in:	where to put data read, NULL when writing
 * @len:	number of bytes to transfer
 * Return: 0 on success, negative on error
 */
static int mchp23k256_command(struct udevice *dev, u8 opcode, u32 offset,
			      const void *data_out, void *data_in, size_t len)
{
	const struct mchp23_caps *caps =
		(const struct mchp23_caps *)dev_get_driver_data(dev);
	struct spi_slave *spi = dev_get_parent_priv(dev);
	u8 cmd[1 + MCHP23K256_ADDR_MAX];
	uint i;
	int ret;

	cmd[0] = opcode;
	for (i = 0; i < caps->addr_width; i++)
		cmd[1 + i] = offset >> (8 * (caps->addr_width - i - 1));

	ret = spi_claim_bus(spi);
	if (ret) {
		dev_dbg(dev, "failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_write_then_read(spi, cmd, 1 + caps->addr_width, data_out,
				  data_in, len);

	spi_release_bus(spi);

	return ret;
}

static int mchp23k256_read(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	int ret;

	ret = mchp23k256_command(mtd->dev, MCHP23K256_CMD_READ, from, NULL,
				 buf, len);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int mchp23k256_write(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, const u_char *buf)
{
	int ret;

	ret = mchp23k256_command(mtd->dev, MCHP23K256_CMD_WRITE, to, buf,
				 NULL, len);
	if (ret)
		return ret;

	*retlen = len;

	return 0;
}

static int mchp23k256_probe(struct udevice *dev)
{
	const struct mchp23_caps *caps =
		(const struct mchp23_caps *)dev_get_driver_data(dev);
	struct mtd_info *mtd = dev_get_uclass_priv(dev);
	struct mchp23k256_priv *priv = dev_get_priv(dev);
	struct spi_slave *spi = dev_get_parent_priv(dev);
	u8 cmd[2];
	int ret;

	ret = spi_claim_bus(spi);
	if (ret) {
		dev_dbg(dev, "failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	cmd[0] = MCHP23K256_CMD_WRITE_STATUS;
	cmd[1] = MCHP23K256_MODE_SEQ;
	ret = spi_write_then_read(spi, cmd, sizeof(cmd), NULL, NULL, 0);

	spi_release_bus(spi);

	if (ret) {
		dev_dbg(dev, "failed to select sequential operation: %d\n",
			ret);
		return ret;
	}

	/*
	 * Name the MTD device after the technology, the way nand0 and
	 * spi-nor0 are named, not after the part.
	 */
	snprintf(priv->name, sizeof(priv->name), "spi-ram%d", dev_seq(dev));
	mtd->name = priv->name;
	mtd->dev = dev;
	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = caps->size;
	mtd->writesize = 1;
	mtd->writebufsize = 1;

	mtd->_read = mchp23k256_read;
	mtd->_write = mchp23k256_write;
	mtd->_erase = NULL;

	return add_mtd_device(mtd);
}

static int mchp23k256_remove(struct udevice *dev)
{
	return del_mtd_device(dev_get_uclass_priv(dev));
}

static const struct mchp23_caps mchp23k256_caps = {
	.size = SZ_32K,
	.addr_width = 2,
};

static const struct mchp23_caps mchp23lcv1024_caps = {
	.size = SZ_128K,
	.addr_width = 3,
};

static const struct udevice_id mchp23k256_ids[] = {
	{ .compatible = "microchip,mchp23k256",
	  .data = (ulong)&mchp23k256_caps },
	{ .compatible = "microchip,mchp23lcv1024",
	  .data = (ulong)&mchp23lcv1024_caps },
	{ }
};

U_BOOT_DRIVER(mchp23k256) = {
	.name		= "mchp23k256",
	.id		= UCLASS_MTD,
	.of_match	= mchp23k256_ids,
	.probe		= mchp23k256_probe,
	.remove		= mchp23k256_remove,
	.priv_auto	= sizeof(struct mchp23k256_priv),
};
