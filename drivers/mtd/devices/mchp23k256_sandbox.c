// SPDX-License-Identifier: GPL-2.0
/*
 * Sandbox emulation of the Microchip 23K256 and 23LCV1024 SPI SRAM
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
#define MCHP23K256_CMD_WRITE_STATUS	0x01
#define MCHP23K256_CMD_WRITE		0x02
#define MCHP23K256_CMD_READ		0x03

/* MODE register, bits 7:6 select byte, page or sequential operation */
#define MCHP23K256_MODE_MASK		GENMASK(7, 6)
#define MCHP23K256_MODE_BYTE		0x00
#define MCHP23K256_MODE_SEQ		BIT(6)
#define MCHP23K256_MODE_PAGE		BIT(7)

/* Page of the 23LCV1024, the part the page mode of this emulation models */
#define MCHP23K256_PAGE_SIZE		32

/**
 * struct sandbox_mchp23_caps - what the emulated device looks like
 *
 * @size:	size of the emulated array in bytes
 * @addr_width:	width of the address a command carries, in bytes
 */
struct sandbox_mchp23_caps {
	u32 size;
	u8 addr_width;
};

/**
 * struct sandbox_mchp23k256_priv - state of the emulated SRAM
 *
 * @caps:	the device being emulated
 * @data:	the emulated memory array
 * @mode:	contents of the MODE register, see MCHP23K256_MODE_*
 * @cmd:	opcode of the command being processed
 * @addr:	address that command started at
 * @pos:	data bytes of that command transferred so far
 */
struct sandbox_mchp23k256_priv {
	const struct sandbox_mchp23_caps *caps;
	u8 *data;
	u8 mode;
	u8 cmd;
	u32 addr;
	uint pos;
};

bool sandbox_mchp23k256_is_sequential(struct udevice *dev)
{
	struct sandbox_mchp23k256_priv *priv = dev_get_priv(dev);

	return priv->mode == MCHP23K256_MODE_SEQ;
}

void sandbox_mchp23k256_force_byte_mode(struct udevice *dev)
{
	struct sandbox_mchp23k256_priv *priv = dev_get_priv(dev);

	priv->mode = MCHP23K256_MODE_BYTE;
}

/**
 * sandbox_mchp23k256_addr() - address of the n-th byte of the current command
 *
 * The three modes differ in how the device address counter changes
 * during multi-byte transfers.
 *
 * @priv:	state of the emulated SRAM
 * @n:		position of the byte within the data of the command
 * Return: index into the emulated memory array
 */
static u32 sandbox_mchp23k256_addr(struct sandbox_mchp23k256_priv *priv,
				   uint n)
{
	switch (priv->mode) {
	case MCHP23K256_MODE_SEQ:
		/* The counter runs across the whole array and wraps around */
		return (priv->addr + n) % priv->caps->size;
	case MCHP23K256_MODE_PAGE:
		/* The counter wraps around within the addressed page */
		return (priv->addr & ~(MCHP23K256_PAGE_SIZE - 1)) |
		       ((priv->addr + n) & (MCHP23K256_PAGE_SIZE - 1));
	default:
		/* Byte operation transfers one byte and gets no further */
		return priv->addr;
	}
}

/**
 * sandbox_mchp23k256_data() - move the data bytes of the current command
 *
 * @priv:	state of the emulated SRAM
 * @out:	bytes clocked into the device, NULL when reading
 * @in:		where to put the bytes clocked out, NULL when writing
 * @bytes:	number of bytes
 * Return: 0 on success, negative on error
 */
static int sandbox_mchp23k256_data(struct sandbox_mchp23k256_priv *priv,
				   const u8 *out, u8 *in, uint bytes)
{
	uint n;

	for (n = 0; n < bytes; n++) {
		u32 addr = 0;

		if (priv->cmd != MCHP23K256_CMD_WRITE_STATUS)
			addr = sandbox_mchp23k256_addr(priv, priv->pos + n);

		switch (priv->cmd) {
		case MCHP23K256_CMD_READ:
			if (!in)
				return -EINVAL;
			in[n] = priv->data[addr];
			break;
		case MCHP23K256_CMD_WRITE:
			if (!out)
				return -EINVAL;
			priv->data[addr] = out[n];
			break;
		case MCHP23K256_CMD_WRITE_STATUS:
			if (!out)
				return -EINVAL;
			/* Only the byte right after the opcode is the register */
			if (!priv->pos && !n)
				priv->mode = out[n] & MCHP23K256_MODE_MASK;
			break;
		default:
			return -EINVAL;
		}
	}

	priv->pos += bytes;

	return 0;
}

/*
 * The device sees the opcode, address and data as one continuous byte
 * stream while chip select remains asserted, regardless of transfer
 * boundaries.
 */
static int sandbox_mchp23k256_xfer(struct udevice *dev, unsigned int bitlen,
				   const void *dout, void *din,
				   unsigned long flags)
{
	struct sandbox_mchp23k256_priv *priv = dev_get_priv(dev);
	const u8 *out = dout;
	u8 *in = din;
	uint bytes = bitlen / 8;
	uint cmd_len;
	uint i;

	if (bitlen % 8) {
		log_err("transfer of %u bits is not a whole number of bytes\n",
			bitlen);
		return -EINVAL;
	}

	if (flags & SPI_XFER_BEGIN) {
		if (!out || !bytes)
			return -EINVAL;

		priv->cmd = out[0];
		priv->pos = 0;

		switch (priv->cmd) {
		case MCHP23K256_CMD_WRITE_STATUS:
			/* The register follows the opcode, no address */
			cmd_len = 1;
			break;
		case MCHP23K256_CMD_READ:
		case MCHP23K256_CMD_WRITE:
			cmd_len = 1 + priv->caps->addr_width;
			if (bytes < cmd_len)
				return -EINVAL;

			priv->addr = 0;
			for (i = 1; i < cmd_len; i++)
				priv->addr = (priv->addr << 8) | out[i];

			if (priv->addr >= priv->caps->size)
				return -EINVAL;
			break;
		default:
			log_err("unknown command %#x\n", priv->cmd);
			return -EINVAL;
		}

		/* Whatever follows the address in this transfer is data */
		bytes -= cmd_len;
		out += cmd_len;
		if (in)
			in += cmd_len;
	}

	return sandbox_mchp23k256_data(priv, out, in, bytes);
}

static int sandbox_mchp23k256_probe(struct udevice *dev)
{
	struct sandbox_mchp23k256_priv *priv = dev_get_priv(dev);

	priv->caps = (const struct sandbox_mchp23_caps *)
			dev_get_driver_data(dev);

	/* The device comes out of reset in sequential operation */
	priv->mode = MCHP23K256_MODE_SEQ;

	priv->data = calloc(1, priv->caps->size);
	if (!priv->data)
		return -ENOMEM;

	return 0;
}

static int sandbox_mchp23k256_remove(struct udevice *dev)
{
	struct sandbox_mchp23k256_priv *priv = dev_get_priv(dev);

	free(priv->data);
	priv->data = NULL;

	return 0;
}

static const struct dm_spi_emul_ops sandbox_mchp23k256_emul_ops = {
	.xfer = sandbox_mchp23k256_xfer,
};

static const struct sandbox_mchp23_caps sandbox_mchp23k256_caps = {
	.size = SZ_32K,
	.addr_width = 2,
};

static const struct sandbox_mchp23_caps sandbox_mchp23lcv1024_caps = {
	.size = SZ_128K,
	.addr_width = 3,
};

static const struct udevice_id sandbox_mchp23k256_ids[] = {
	{ .compatible = "sandbox,mchp23k256-emul",
	  .data = (ulong)&sandbox_mchp23k256_caps },
	{ .compatible = "sandbox,mchp23lcv1024-emul",
	  .data = (ulong)&sandbox_mchp23lcv1024_caps },
	{ }
};

U_BOOT_DRIVER(sandbox_mchp23k256_emul) = {
	.name		= "sandbox_mchp23k256_emul",
	.id		= UCLASS_SPI_EMUL,
	.of_match	= sandbox_mchp23k256_ids,
	.probe		= sandbox_mchp23k256_probe,
	.remove		= sandbox_mchp23k256_remove,
	.priv_auto	= sizeof(struct sandbox_mchp23k256_priv),
	.ops		= &sandbox_mchp23k256_emul_ops,
};
