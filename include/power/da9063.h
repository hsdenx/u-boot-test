/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright (C) 2019 Heiko Schocher <hs@denx.de>
 */

#ifndef __DA9063_PMIC_H_
#define __DA9063_PMIC_H_

#define DA9063		1

/* Drivers name */
#define DA9063_LDO_DRIVER	"da9063_ldo"
#define DA9063_BUCK_DRIVER	"da9063_buck"

#define MODE_MANUAL	0x0
#define MODE_SLEEP	0x1
#define MODE_SYNC	0x2
#define MODE_AUTO	0x3

#endif
