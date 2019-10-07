// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2019 Heiko Schocher <hs@denx.de>
 */

#include <common.h>
#include <fdtdec.h>
#include <errno.h>
#include <dm.h>
#include <i2c.h>
#include <power/pmic.h>
#include <power/regulator.h>
#include <power/da9063.h>
#include <power/da9063_register.h>
#include <dm/device.h>

#undef debug
#define debug printf

static int da9063_set_page(struct udevice *dev, uint reg)
{
	unsigned char data;

	/* set page */
	if (reg < DA9063_REG_SEQ) {
		data = 0;
	} else if ((reg >= DA9063_REG_SEQ) && (reg < DA9063_REG_OTP_CONT)) {
		data = 1;
	} else {
		data = 2;
	}
	if (dm_i2c_write(dev, DA9063_REG_PAGE_CON, &data, 1)) {
		printf("%s: could not set reg: %d page: %d\n", __func__, reg,
		       data);
		return -EIO;
	}

	return 0;
}

static int da9063_write(struct udevice *dev, uint reg, const uint8_t *buf,
			int len)
{
	int ret;

	/* set page */
	ret = da9063_set_page(dev, reg);
	if (ret)
		return ret;

	if (dm_i2c_write(dev, reg, buf, len)) {
		pr_err("write error to device: %p register: %#x!\n", dev, reg);
		return -EIO;
	}

	return 0;
}

static int da9063_read(struct udevice *dev, uint reg, uint8_t *buf, int len)
{
	int ret;

	/* set page */
	ret = da9063_set_page(dev, reg);
	if (ret)
		return ret;

	if (dm_i2c_read(dev, reg, buf, len)) {
		pr_err("read error from device: %p register: %#x!\n", dev, reg);
		return -EIO;
	}

	return 0;
}
static const struct pmic_child_info pmic_children_info[] = {
	{ .prefix = "ldo", .driver = DA9063_LDO_DRIVER },
	{ .prefix = "bcore", .driver = DA9063_BUCK_DRIVER },
	{ .prefix = "bpro", .driver = DA9063_BUCK_DRIVER },
	{ .prefix = "bperi", .driver = DA9063_BUCK_DRIVER },
	{ .prefix = "bmem", .driver = DA9063_BUCK_DRIVER },
	{ .prefix = "bio", .driver = DA9063_BUCK_DRIVER },
	{ },
};

static int da9063_bind(struct udevice *dev)
{
	ofnode regulators_node;
	int children;

	regulators_node = dev_read_subnode(dev, "regulators");
	if (!ofnode_valid(regulators_node)) {
		debug("%s: %s reg subnode not found!\n", __func__, dev->name);
		return -ENXIO;
	}

	children = pmic_bind_children(dev, regulators_node, pmic_children_info);
	if (!children)
		debug("%s: %s - no child found\n", __func__, dev->name);

	/* Always return success for this device */
	return 0;
}

static struct dm_pmic_ops da9063_ops = {
	.read = da9063_read,
	.write = da9063_write,
};

static const struct udevice_id da9063_ids[] = {
	{ .compatible = "dlg,da9063", .data = DA9063 },
	{ }
};

U_BOOT_DRIVER(pmic_da9063) = {
	.name = "da9063_pmic",
	.id = UCLASS_PMIC,
	.of_match = da9063_ids,
	.bind = da9063_bind,
	.ops = &da9063_ops,
};
