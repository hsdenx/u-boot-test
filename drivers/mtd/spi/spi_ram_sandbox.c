// SPDX-License-Identifier: GPL-2.0
/*
 * Sandbox emulation of the Microchip 23LCV1024 SPI SRAM
 *
 * Copyright (C) 2026 Heiko Schocher, Nabla Software Engineering <hs@nabladev.com>
 */

#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <spi.h>
#include <asm/test.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

/* Instruction set of the 23x series serial SRAMs */
#define MCHP23_CMD_WRITE_STATUS	0x01
#define MCHP23_CMD_WRITE	0x02
#define MCHP23_CMD_READ		0x03

/* MODE register, bits 7:6 select byte, page or sequential operation */
#define MCHP23_MODE_MASK	GENMASK(7, 6)
#define MCHP23_MODE_BYTE	0x00
#define MCHP23_MODE_SEQ		BIT(6)
#define MCHP23_MODE_PAGE	BIT(7)

#define MCHP23_PAGE_SIZE	32
#define MCHP23_ADDR_LEN		3
#define MCHP23_SIZE		SZ_128K

/**
 * struct sandbox_spi_ram_priv - state of the emulated SRAM
 *
 * @data:	the emulated memory array
 * @mode:	contents of the MODE register, see MCHP23_MODE_*
 * @cmd:	opcode of the command being processed
 * @addr:	address that command started at
 */
struct sandbox_spi_ram_priv {
	u8 *data;
	u8 mode;
	u8 cmd;
	u32 addr;
};

bool sandbox_spi_ram_is_sequential(struct udevice *dev)
{
	struct sandbox_spi_ram_priv *priv = dev_get_priv(dev);

	return priv->mode == MCHP23_MODE_SEQ;
}

/**
 * sandbox_spi_ram_addr() - address of the n-th byte of the current command
 *
 * The internal address counter of the device is advanced differently in
 * each of the three operating modes, which is what makes the mode matter
 * for transfers of more than one byte.
 *
 * @priv:	state of the emulated SRAM
 * @n:		position of the byte within the data phase
 * Return: index into the emulated memory array
 */
static u32 sandbox_spi_ram_addr(struct sandbox_spi_ram_priv *priv, uint n)
{
	switch (priv->mode) {
	case MCHP23_MODE_SEQ:
		/* The counter runs across the whole array and wraps around */
		return (priv->addr + n) % MCHP23_SIZE;
	case MCHP23_MODE_PAGE:
		/* The counter wraps around within the addressed page */
		return (priv->addr & ~(MCHP23_PAGE_SIZE - 1)) |
		       ((priv->addr + n) & (MCHP23_PAGE_SIZE - 1));
	default:
		/* Byte operation transfers one byte and gets no further */
		return priv->addr;
	}
}

/*
 * The command and the data it carries arrive as two transfers, the first
 * one holding the opcode and, for a read or a write, the address.
 */
static int sandbox_spi_ram_xfer(struct udevice *dev, unsigned int bitlen,
				const void *dout, void *din, unsigned long flags)
{
	struct sandbox_spi_ram_priv *priv = dev_get_priv(dev);
	const u8 *out = dout;
	u8 *in = din;
	uint bytes = bitlen / 8;
	uint n;

	if (bitlen % 8) {
		log_err("transfer of %u bits is not a whole number of bytes\n",
			bitlen);
		return -EINVAL;
	}

	if (flags & SPI_XFER_BEGIN) {
		if (!out || !bytes)
			return -EINVAL;

		priv->cmd = out[0];

		switch (priv->cmd) {
		case MCHP23_CMD_WRITE_STATUS:
			if (bytes < 2)
				return -EINVAL;
			priv->mode = out[1] & MCHP23_MODE_MASK;
			break;
		case MCHP23_CMD_READ:
		case MCHP23_CMD_WRITE:
			if (bytes < 1 + MCHP23_ADDR_LEN)
				return -EINVAL;
			priv->addr = (out[1] << 16) | (out[2] << 8) | out[3];
			if (priv->addr >= MCHP23_SIZE)
				return -EINVAL;
			break;
		default:
			log_err("unknown command %#x\n", priv->cmd);
			return -EINVAL;
		}

		return 0;
	}

	for (n = 0; n < bytes; n++) {
		u32 addr = sandbox_spi_ram_addr(priv, n);

		switch (priv->cmd) {
		case MCHP23_CMD_READ:
			if (!in)
				return -EINVAL;
			in[n] = priv->data[addr];
			break;
		case MCHP23_CMD_WRITE:
			if (!out)
				return -EINVAL;
			priv->data[addr] = out[n];
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int sandbox_spi_ram_probe(struct udevice *dev)
{
	struct sandbox_spi_ram_priv *priv = dev_get_priv(dev);

	/* The device comes out of reset in byte operation */
	priv->mode = MCHP23_MODE_BYTE;

	priv->data = calloc(1, MCHP23_SIZE);
	if (!priv->data)
		return -ENOMEM;

	return 0;
}

static int sandbox_spi_ram_remove(struct udevice *dev)
{
	struct sandbox_spi_ram_priv *priv = dev_get_priv(dev);

	free(priv->data);
	priv->data = NULL;

	return 0;
}

static const struct dm_spi_emul_ops sandbox_spi_ram_emul_ops = {
	.xfer = sandbox_spi_ram_xfer,
};

/*
 * No of_match, so that a device tree scan never binds this driver. It is
 * bound by name, on the node of the slave it emulates, which already
 * carries the compatible of the real device.
 */
U_BOOT_DRIVER(sandbox_spi_ram_emul) = {
	.name		= "sandbox_spi_ram_emul",
	.id		= UCLASS_SPI_EMUL,
	.probe		= sandbox_spi_ram_probe,
	.remove		= sandbox_spi_ram_remove,
	.priv_auto	= sizeof(struct sandbox_spi_ram_priv),
	.ops		= &sandbox_spi_ram_emul_ops,
};
