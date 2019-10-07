// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 Heiko Schocher <hs@denx.de>
 *
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

/**
 * struct da9063_regulator_desc - regulator descriptor
 *
 * @name: Identify name for the regulator.
 * @type: Indicates the regulator type.
 * @uV_step: Voltage increase for each selector.
 * @vsel_reg: Register for adjust regulator voltage for normal.
 * @vsel_mask: Mask bit for setting regulator voltage for normal.
 * @stby_reg: Register for adjust regulator voltage for standby.
 * @stby_mask: Mask bit for setting regulator voltage for standby.
 */
struct da9063_regulator_desc {
	char *name;
	enum regulator_type type;
	unsigned int min_uV;
	unsigned int uV_step;
	unsigned int n_voltages;
	unsigned int vsel_reg;
	unsigned int vsel_mask;
	unsigned int csel_reg;
	unsigned int csel_mask;
	const unsigned int *curr_table;
	unsigned int n_current_limits;
	unsigned int enable_reg;
	unsigned int enable_mask;
	unsigned int mode_reg;
	unsigned int mode_mask;
	unsigned int linear_min_sel;
};

/* Macros for LDO */
#define DA9063_LDO(chip, regl_name, min_mV, step_mV, max_mV) \
	.name = __stringify(regl_name), \
	.min_uV = (min_mV) * 1000, \
	.uV_step = (step_mV) * 1000, \
	.n_voltages = (((max_mV) - (min_mV))/(step_mV) + 1 \
		+ (DA9063_V##regl_name##_BIAS)), \
	.enable_reg = DA9063_REG_##regl_name##_CONT, \
	.enable_mask = DA9063_LDO_EN, \
	.vsel_reg = DA9063_REG_V##regl_name##_A, \
	.vsel_mask = DA9063_V##regl_name##_MASK, \
	.linear_min_sel = DA9063_V##regl_name##_BIAS


/* Macros for voltage DC/DC converters (BUCKs) */
#define DA9063_BUCK(chip, regl_name, min_mV, step_mV, max_mV, limits_array, \
		    creg, cmask) \
	.name = __stringify(regl_name), \
	.min_uV = (min_mV) * 1000, \
	.uV_step = (step_mV) * 1000, \
	.n_voltages = ((max_mV) - (min_mV))/(step_mV) + 1, \
	.csel_reg = (creg), \
	.csel_mask = (cmask), \
	.curr_table = limits_array, \
	.n_current_limits = ARRAY_SIZE(limits_array)

#define DA9063_BUCK_COMMON_FIELDS(regl_name) \
	.enable_reg = DA9063_REG_##regl_name##_CONT, \
	.enable_mask = DA9063_BUCK_EN, \
	.vsel_reg = DA9063_REG_V##regl_name##_A, \
	.vsel_mask = DA9063_VBUCK_MASK, \
	.mode_reg = DA9063_REG_##regl_name##_CFG, \
	.mode_mask = DA9063_BUCK_MODE_MASK

#define MODE(_id, _val, _name) { \
	.id = _id, \
	.register_value = _val, \
	.name = _name, \
}

/* Buck regulator mode */
static struct dm_regulator_mode da9063_modes[] = {
	MODE(MODE_MANUAL, DA9063_BUCK_MODE_MANUAL, "manual"),
	MODE(MODE_SLEEP, DA9063_BUCK_MODE_SLEEP, "sleep"),
	MODE(MODE_SYNC, DA9063_BUCK_MODE_SYNC, "sync"),
	MODE(MODE_AUTO, DA9063_BUCK_MODE_AUTO, "auto"),
};

/**
 * struct da9063_regulator_platdata - platform data for da9063
 *
 * @desc: Points the description entry of one regulator of da9063
 */
struct da9063_regulator_platdata {
	struct da9063_regulator_desc *desc;
};

/* Current limits array (in uA) for BCORE1, BCORE2, BPRO.
   Entry indexes corresponds to register values. */
static const unsigned int da9063_buck_a_limits[] = {
	 500000,  600000,  700000,  800000,  900000, 1000000, 1100000, 1200000,
	1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000
};

/* Current limits array (in uA) for BMEM, BIO, BPERI.
   Entry indexes corresponds to register values. */
static const unsigned int da9063_buck_b_limits[] = {
	1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000,
	2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000
};

/* Current limits array (in uA) for merged BCORE1 and BCORE2.
   Entry indexes corresponds to register values. */
static const unsigned int da9063_bcores_merged_limits[] = {
	1000000, 1200000, 1400000, 1600000, 1800000, 2000000, 2200000, 2400000,
	2600000, 2800000, 3000000, 3200000, 3400000, 3600000, 3800000, 4000000
};

/* Current limits array (in uA) for merged BMEM and BIO.
   Entry indexes corresponds to register values. */
static const unsigned int da9063_bmem_bio_merged_limits[] = {
	3000000, 3200000, 3400000, 3600000, 3800000, 4000000, 4200000, 4400000,
	4600000, 4800000, 5000000, 5200000, 5400000, 5600000, 5800000, 6000000
};


/* Info of regulators for DA9063 */
static struct da9063_regulator_desc da9063_regulator_info[] = {
	{
		DA9063_BUCK(DA9063, BCORE1, 300, 10, 1570,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE1_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BCORE1),
	},
	{
		DA9063_BUCK(DA9063, BCORE2, 300, 10, 1570,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE2_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BCORE2),
	},
	{
		DA9063_BUCK(DA9063, BPRO, 530, 10, 1800,
			    da9063_buck_a_limits,
			    DA9063_REG_BUCK_ILIM_B, DA9063_BPRO_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BPRO),
	},
	{
		DA9063_BUCK(DA9063, BMEM, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BMEM_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BMEM),
	},
	{
		DA9063_BUCK(DA9063, BIO, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BIO_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BIO),
	},
	{
		DA9063_BUCK(DA9063, BPERI, 800, 20, 3340,
			    da9063_buck_b_limits,
			    DA9063_REG_BUCK_ILIM_B, DA9063_BPERI_ILIM_MASK),
		DA9063_BUCK_COMMON_FIELDS(BPERI),
	},
	{
		DA9063_BUCK(DA9063, BCORES_MERGED, 300, 10, 1570,
			    da9063_bcores_merged_limits,
			    DA9063_REG_BUCK_ILIM_C, DA9063_BCORE1_ILIM_MASK),
		/* BCORES_MERGED uses the same register fields as BCORE1 */
		DA9063_BUCK_COMMON_FIELDS(BCORE1),
	},
	{
		DA9063_BUCK(DA9063, BMEM_BIO_MERGED, 800, 20, 3340,
			    da9063_bmem_bio_merged_limits,
			    DA9063_REG_BUCK_ILIM_A, DA9063_BMEM_ILIM_MASK),
		/* BMEM_BIO_MERGED uses the same register fields as BMEM */
		DA9063_BUCK_COMMON_FIELDS(BMEM),
	},
	{
		DA9063_LDO(DA9063, LDO3, 900, 20, 3440),
	},
	{
		DA9063_LDO(DA9063, LDO7, 900, 50, 3600),
	},
	{
		DA9063_LDO(DA9063, LDO8, 900, 50, 3600),
	},
	{
		DA9063_LDO(DA9063, LDO9, 950, 50, 3600),
	},
	{
		DA9063_LDO(DA9063, LDO11, 900, 50, 3600),
	},

	/* The following LDOs are present only on DA9063, not on DA9063L */
	{
		DA9063_LDO(DA9063, LDO1, 600, 20, 1860),
	},
	{
		DA9063_LDO(DA9063, LDO2, 600, 20, 1860),
	},
	{
		DA9063_LDO(DA9063, LDO4, 900, 20, 3440),
	},
	{
		DA9063_LDO(DA9063, LDO5, 900, 50, 3600),
	},
	{
		DA9063_LDO(DA9063, LDO6, 900, 50, 3600),
	},
	{
		DA9063_LDO(DA9063, LDO10, 900, 50, 3600),
	},
};

static int da9063_get_vol(struct udevice *dev)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int ret;

	ret = pmic_reg_read(dev->parent, desc->vsel_reg);
	if (ret < 0)
		return ret;

	ret &= desc->vsel_mask;
	ret = ret * desc->uV_step + desc->min_uV;

	return ret;
}

static int da9063_get_idx(int min, int step, int count, int val)
{
	int i;
	int max = min + count * step;
	int idx = 0;

	if (val < min)
		return -EINVAL;

	if (val > max)
		return -EINVAL;

	for (i = min; i < max; i += step) {
		if ((val >= i) && (val < (i + step)))
			return idx;
		idx++;
	}

	return -EINVAL;
}

static int da9063_set_vol(struct udevice *dev, int uV)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int val;
	unsigned int new;

	val = pmic_reg_read(dev->parent, desc->vsel_reg);
	if (val < 0)
		return val;

	val &= ~desc->vsel_mask;
	new = da9063_get_idx(desc->min_uV, desc->uV_step, desc->n_voltages, uV);
	if (new < 0)
		return new;

	val |= new;
	return pmic_reg_write(dev->parent, desc->vsel_reg, val & desc->vsel_mask);
}

static int da9063_val(struct udevice *dev, int op, int *uV)
{
	int ret;

	if (op == PMIC_OP_GET)
		*uV = 0;

	ret = da9063_get_vol(dev);
	if (ret < 0)
		return ret;

	if (op == PMIC_OP_GET) {
		*uV = ret;
		return 0;
	}

	return da9063_set_vol(dev, *uV);
}


static int da9063_regulator_get_value(struct udevice *dev)
{
	int uV;
	int ret;

	ret = da9063_val(dev, PMIC_OP_GET, &uV);
	if (ret)
		return ret;

	return uV;
}

static int da9063_regulator_set_value(struct udevice *dev, int uV)
{
	return da9063_val(dev, PMIC_OP_SET, &uV);
}

static int da9063_get_cur_idx(struct da9063_regulator_desc *desc, int uA)
{
	int i;

	for (i = desc->n_current_limits - 1; i >= 0; i--)
		if (desc->curr_table[i] <= uA)
			return i;

	return -EFAULT;
}

static int da9063_cur(struct udevice *dev, int op, int *uA)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	int ret;
	int val;
	int org;

	if (op == PMIC_OP_GET)
		*uA = 0;

	ret = pmic_reg_read(dev->parent, desc->csel_reg);
	if (ret < 0)
		return ret;

	org = ret;
	val = ret & desc->csel_mask;
	if (val > desc->n_current_limits) {
		printf("%s: val %d to big\n", __func__, val);
		return -EFAULT;
	}

	val = desc->curr_table[val];
	if (op == PMIC_OP_GET) {
		*uA = val;
		return 0;
	}

	/* write current */
	val = da9063_get_cur_idx(desc, *uA);
	if (val < 0)
		return val;

	val |= (org & ~desc->csel_mask);
	return pmic_reg_write(dev->parent, desc->csel_reg, val);
}

static int da9063_regulator_get_current(struct udevice *dev)
{
	int uA;
	int ret;

	ret = da9063_cur(dev, PMIC_OP_GET, &uA);
	if (ret)
		return ret;

	return uA;
}

static int da9063_regulator_set_current(struct udevice *dev, int uA)
{
	return da9063_cur(dev, PMIC_OP_SET, &uA);
}

static int da9063_regulator_get_enable(struct udevice *dev)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int val;

	val = pmic_reg_read(dev->parent, desc->enable_reg);
	if (val < 0)
		return val;

	return (val & desc->enable_mask ? 1 : 0);
}

static int da9063_regulator_set_enable(struct udevice *dev, bool enable)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int val;

	val = pmic_reg_read(dev->parent, desc->enable_reg);
	if (val < 0)
		return val;

	if (enable)
		val |= desc->enable_mask;
	else
		val &= ~desc->enable_mask;

	return pmic_reg_write(dev->parent, desc->enable_reg, val);
}

static int da9063_regulator_get_mode(struct udevice *dev)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int val;

	val = pmic_reg_read(dev->parent, desc->mode_reg);
	if (val < 0)
		return val;

	return (val & desc->mode_mask) >> (ffs(desc->mode_mask) - 1);
}

static int da9063_regulator_set_mode(struct udevice *dev, int mode)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc = plat->desc;
	unsigned int val;

	val = pmic_reg_read(dev->parent, desc->mode_reg);
	if (val < 0)
		return val;

	val &= ~desc->mode_mask;
	val |= da9063_modes[mode].register_value;

	return pmic_reg_write(dev->parent, desc->mode_reg, val);
}

static const struct dm_regulator_ops da9063_ldo_regulator_ops = {
	.get_value  = da9063_regulator_get_value,
	.set_value  = da9063_regulator_set_value,
	.get_enable = da9063_regulator_get_enable,
	.set_enable = da9063_regulator_set_enable,
};

static struct da9063_regulator_desc *da9063_get_desc(struct udevice *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(da9063_regulator_info); i++ ) {
		struct da9063_regulator_desc *desc = &da9063_regulator_info[i];

		if (!strncasecmp(desc->name, dev->name, strlen(desc->name)))
			return desc;
	}

	return NULL;
}

static int da9063_ldo_regulator_probe(struct udevice *dev)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc;

	desc = da9063_get_desc(dev);
	if (!desc)
		return 0;

	plat->desc = desc;
	desc->type = REGULATOR_TYPE_LDO;
	return 0;
}

U_BOOT_DRIVER(da9063_ldo_regulator) = {
	.name = DA9063_LDO_DRIVER,
	.id = UCLASS_REGULATOR,
	.ops = &da9063_ldo_regulator_ops,
	.probe = da9063_ldo_regulator_probe,
	.platdata_auto_alloc_size = sizeof(struct da9063_regulator_platdata),
};

static const struct dm_regulator_ops da9063_buck_regulator_ops = {
	.get_value  = da9063_regulator_get_value,
	.set_value  = da9063_regulator_set_value,
	.get_current = da9063_regulator_get_current,
	.set_current = da9063_regulator_set_current,
	.get_enable = da9063_regulator_get_enable,
	.set_enable = da9063_regulator_set_enable,
	.get_mode   = da9063_regulator_get_mode,
	.set_mode   = da9063_regulator_set_mode,
};

static int da9063_buck_regulator_probe(struct udevice *dev)
{
	struct da9063_regulator_platdata *plat = dev_get_platdata(dev);
	struct da9063_regulator_desc *desc;
	struct dm_regulator_uclass_platdata *uc_pdata;

	desc = da9063_get_desc(dev);
	if (!desc)
		return 0;

	plat->desc = desc;
	desc->type = REGULATOR_TYPE_BUCK;

	uc_pdata = dev_get_uclass_platdata(dev);
	uc_pdata->mode = da9063_modes;
	uc_pdata->mode_count = ARRAY_SIZE(da9063_modes);
	return 0;
}

U_BOOT_DRIVER(da9063_buck_regulator) = {
	.name = DA9063_BUCK_DRIVER,
	.id = UCLASS_REGULATOR,
	.ops = &da9063_buck_regulator_ops,
	.probe = da9063_buck_regulator_probe,
	.platdata_auto_alloc_size = sizeof(struct da9063_regulator_platdata),
};

