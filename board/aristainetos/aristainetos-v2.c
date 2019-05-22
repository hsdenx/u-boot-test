// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015
 * Heiko Schocher, DENX Software Engineering, hs@denx.de.
 *
 * Based on:
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
 */

#include <asm/arch/clock.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/iomux.h>
#include <asm/arch/mx6-pins.h>
#include <env.h>
#include <linux/errno.h>
#include <asm/gpio.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/video.h>
#include <asm/arch/crm_regs.h>
#include <asm/io.h>
#include <asm/arch/sys_proto.h>
#include <pwm.h>
#include <micrel.h>
#include <bmp_logo.h>
#include <bmp_logo_data.h>
#include <splash.h>
#include <lcd.h>
#include <led.h>

#if (CONFIG_SYS_BOARD_VERSION == 2)
	/* 4.3 display controller */
	#define ECSPI1_CS0		IMX_GPIO_NR(4, 9)
	#define ECSPI4_CS0		IMX_GPIO_NR(3, 29)
#elif (CONFIG_SYS_BOARD_VERSION == 3)
	#define ECSPI1_CS0		IMX_GPIO_NR(2, 30)   /* NOR flash */
	/* 4.3 display controller */
	#define ECSPI1_CS1		IMX_GPIO_NR(4, 10)
#endif

static iomux_v3_cfg_t const misc_pads[] = {
	/* USB_OTG_ID = GPIO1_24*/
	MX6_PAD_ENET_RX_ER__USB_OTG_ID		| MUX_PAD_CTRL(NO_PAD_CTRL),
	/* H1 Power enable = GPIO1_0*/
	MX6_PAD_GPIO_0__USB_H1_PWR		| MUX_PAD_CTRL(NO_PAD_CTRL),
	/* OTG Power enable = GPIO4_15*/
	MX6_PAD_KEY_ROW4__USB_OTG_PWR		| MUX_PAD_CTRL(NO_PAD_CTRL),
};

int board_phy_config(struct phy_device *phydev)
{
	/* control data pad skew - devaddr = 0x02, register = 0x04 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_CTRL_SIG_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
	/* rx data pad skew - devaddr = 0x02, register = 0x05 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_RX_DATA_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
	/* tx data pad skew - devaddr = 0x02, register = 0x06 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_TX_DATA_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
	/* gtx and rx clock pad skew - devaddr = 0x02, register = 0x08 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_CLOCK_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x03FF);

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}

static int rotate_logo_one(unsigned char *out, unsigned char *in)
{
	int   i, j;

	for (i = 0; i < BMP_LOGO_WIDTH; i++)
		for (j = 0; j < BMP_LOGO_HEIGHT; j++)
			out[j * BMP_LOGO_WIDTH + BMP_LOGO_HEIGHT - 1 - i] =
			in[i * BMP_LOGO_WIDTH + j];
	return 0;
}

/*
 * Rotate the BMP_LOGO (only)
 * Will only work, if the logo is square, as
 * BMP_LOGO_HEIGHT and BMP_LOGO_WIDTH are defines, not variables
 */
void rotate_logo(int rotations)
{
	unsigned char out_logo[BMP_LOGO_WIDTH * BMP_LOGO_HEIGHT];
	unsigned char *in_logo;
	int   i, j;

	if (BMP_LOGO_WIDTH != BMP_LOGO_HEIGHT)
		return;

	in_logo = bmp_logo_bitmap;

	/* one 90 degree rotation */
	if (rotations == 1  ||  rotations == 2  ||  rotations == 3)
		rotate_logo_one(out_logo, in_logo);

	/* second 90 degree rotation */
	if (rotations == 2  ||  rotations == 3)
		rotate_logo_one(in_logo, out_logo);

	/* third 90 degree rotation */
	if (rotations == 3)
		rotate_logo_one(out_logo, in_logo);

	/* copy result back to original array */
	if (rotations == 1  ||  rotations == 3)
		for (i = 0; i < BMP_LOGO_WIDTH; i++)
			for (j = 0; j < BMP_LOGO_HEIGHT; j++)
				in_logo[i * BMP_LOGO_WIDTH + j] =
				out_logo[i * BMP_LOGO_WIDTH + j];
}

static void enable_lvds(struct display_info_t const *dev)
{
	struct iomuxc *iomux = (struct iomuxc *)IOMUXC_BASE_ADDR;
	struct mxc_ccm_reg *ccm = (struct mxc_ccm_reg *)CCM_BASE_ADDR;
	int reg;
	s32 timeout = 100000;

	/* set PLL5 clock */
	reg = readl(&ccm->analog_pll_video);
	reg |= BM_ANADIG_PLL_VIDEO_POWERDOWN;
	writel(reg, &ccm->analog_pll_video);

	/* set PLL5 to 232720000Hz */
	reg &= ~BM_ANADIG_PLL_VIDEO_DIV_SELECT;
	reg |= BF_ANADIG_PLL_VIDEO_DIV_SELECT(0x26);
	reg &= ~BM_ANADIG_PLL_VIDEO_POST_DIV_SELECT;
	reg |= BF_ANADIG_PLL_VIDEO_POST_DIV_SELECT(0);
	writel(reg, &ccm->analog_pll_video);

	writel(BF_ANADIG_PLL_VIDEO_NUM_A(0xC0238),
	       &ccm->analog_pll_video_num);
	writel(BF_ANADIG_PLL_VIDEO_DENOM_B(0xF4240),
	       &ccm->analog_pll_video_denom);

	reg &= ~BM_ANADIG_PLL_VIDEO_POWERDOWN;
	writel(reg, &ccm->analog_pll_video);

	while (timeout--)
		if (readl(&ccm->analog_pll_video) & BM_ANADIG_PLL_VIDEO_LOCK)
			break;
	if (timeout < 0)
		printf("Warning: video pll lock timeout!\n");

	reg = readl(&ccm->analog_pll_video);
	reg |= BM_ANADIG_PLL_VIDEO_ENABLE;
	reg &= ~BM_ANADIG_PLL_VIDEO_BYPASS;
	writel(reg, &ccm->analog_pll_video);

	/* set LDB0, LDB1 clk select to 000/000 (PLL5 clock) */
	reg = readl(&ccm->cs2cdr);
	reg &= ~(MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_MASK
		 | MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_MASK);
	reg |= (0 << MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_OFFSET)
		| (0 << MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_OFFSET);
	writel(reg, &ccm->cs2cdr);

	reg = readl(&ccm->cscmr2);
	reg |= MXC_CCM_CSCMR2_LDB_DI0_IPU_DIV;
	writel(reg, &ccm->cscmr2);

	reg = readl(&ccm->chsccdr);
	reg |= (CHSCCDR_CLK_SEL_LDB_DI0
		<< MXC_CCM_CHSCCDR_IPU1_DI0_CLK_SEL_OFFSET);
	writel(reg, &ccm->chsccdr);

	reg = IOMUXC_GPR2_BGREF_RRMODE_EXTERNAL_RES
	      | IOMUXC_GPR2_DI1_VS_POLARITY_ACTIVE_HIGH
	      | IOMUXC_GPR2_DI0_VS_POLARITY_ACTIVE_HIGH
	      | IOMUXC_GPR2_BIT_MAPPING_CH0_SPWG
	      | IOMUXC_GPR2_DATA_WIDTH_CH0_24BIT
	      | IOMUXC_GPR2_LVDS_CH1_MODE_DISABLED
	      | IOMUXC_GPR2_LVDS_CH0_MODE_ENABLED_DI0;
	writel(reg, &iomux->gpr[2]);

	reg = readl(&iomux->gpr[3]);
	reg = (reg & ~IOMUXC_GPR3_LVDS0_MUX_CTL_MASK)
	       | (IOMUXC_GPR3_MUX_SRC_IPU1_DI0
		  << IOMUXC_GPR3_LVDS0_MUX_CTL_OFFSET);
	writel(reg, &iomux->gpr[3]);

	return;
}

static void enable_spi_display(struct display_info_t const *dev)
{
	struct iomuxc *iomux = (struct iomuxc *)IOMUXC_BASE_ADDR;
	struct mxc_ccm_reg *ccm = (struct mxc_ccm_reg *)CCM_BASE_ADDR;
	int reg;
	s32 timeout = 100000;

#if defined(CONFIG_VIDEO_BMP_LOGO)
	rotate_logo(3);  /* portrait display in landscape mode */
#endif

	/*
	 * set ldb clock to 28341000 Hz calculated through the formula:
	 * (XRES + LEFT_M + RIGHT_M + HSYNC_LEN) *
	 * (YRES + UPPER_M + LOWER_M + VSYNC_LEN) * REFRESH)
	 * see:
	 * https://community.freescale.com/thread/308170
	 */
	ipu_set_ldb_clock(28341000);

	reg = readl(&ccm->cs2cdr);

	/* select pll 5 clock */
	reg &= ~(MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_MASK
		| MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_MASK);
	writel(reg, &ccm->cs2cdr);

	/* set PLL5 to 197994996Hz */
	reg &= ~BM_ANADIG_PLL_VIDEO_DIV_SELECT;
	reg |= BF_ANADIG_PLL_VIDEO_DIV_SELECT(0x21);
	reg &= ~BM_ANADIG_PLL_VIDEO_POST_DIV_SELECT;
	reg |= BF_ANADIG_PLL_VIDEO_POST_DIV_SELECT(0);
	writel(reg, &ccm->analog_pll_video);

	writel(BF_ANADIG_PLL_VIDEO_NUM_A(0xfbf4),
	       &ccm->analog_pll_video_num);
	writel(BF_ANADIG_PLL_VIDEO_DENOM_B(0xf4240),
	       &ccm->analog_pll_video_denom);

	reg &= ~BM_ANADIG_PLL_VIDEO_POWERDOWN;
	writel(reg, &ccm->analog_pll_video);

	while (timeout--)
		if (readl(&ccm->analog_pll_video) & BM_ANADIG_PLL_VIDEO_LOCK)
			break;
	if (timeout < 0)
		printf("Warning: video pll lock timeout!\n");

	reg = readl(&ccm->analog_pll_video);
	reg |= BM_ANADIG_PLL_VIDEO_ENABLE;
	reg &= ~BM_ANADIG_PLL_VIDEO_BYPASS;
	writel(reg, &ccm->analog_pll_video);

	/* set LDB0, LDB1 clk select to 000/000 (PLL5 clock) */
	reg = readl(&ccm->cs2cdr);
	reg &= ~(MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_MASK
		 | MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_MASK);
	reg |= (0 << MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_OFFSET)
		| (0 << MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_OFFSET);
	writel(reg, &ccm->cs2cdr);

	reg = readl(&ccm->cscmr2);
	reg |= MXC_CCM_CSCMR2_LDB_DI0_IPU_DIV;
	writel(reg, &ccm->cscmr2);

	reg = readl(&ccm->chsccdr);
	reg |= (CHSCCDR_CLK_SEL_LDB_DI0
		<< MXC_CCM_CHSCCDR_IPU1_DI0_CLK_SEL_OFFSET);
	reg &= ~MXC_CCM_CHSCCDR_IPU1_DI0_PODF_MASK;
	reg |= (2 << MXC_CCM_CHSCCDR_IPU1_DI0_PODF_OFFSET);
	reg &= ~MXC_CCM_CHSCCDR_IPU1_DI0_PRE_CLK_SEL_MASK;
	reg |= (2 << MXC_CCM_CHSCCDR_IPU1_DI0_PRE_CLK_SEL_OFFSET);
	writel(reg, &ccm->chsccdr);

	reg = IOMUXC_GPR2_BGREF_RRMODE_EXTERNAL_RES
	      | IOMUXC_GPR2_DI1_VS_POLARITY_ACTIVE_HIGH
	      | IOMUXC_GPR2_DI0_VS_POLARITY_ACTIVE_HIGH
	      | IOMUXC_GPR2_BIT_MAPPING_CH0_SPWG
	      | IOMUXC_GPR2_DATA_WIDTH_CH0_24BIT
	      | IOMUXC_GPR2_LVDS_CH1_MODE_DISABLED
	      | IOMUXC_GPR2_LVDS_CH0_MODE_ENABLED_DI0;
	writel(reg, &iomux->gpr[2]);

	reg = readl(&iomux->gpr[3]);
	reg = (reg & ~IOMUXC_GPR3_LVDS0_MUX_CTL_MASK)
	       | (IOMUXC_GPR3_MUX_SRC_IPU1_DI0
		  << IOMUXC_GPR3_LVDS0_MUX_CTL_OFFSET);
	writel(reg, &iomux->gpr[3]);

	return;
}
static void setup_display(void)
{
	enable_ipu_clock();
}

static void set_gpr_register(void)
{
	struct iomuxc *iomuxc_regs = (struct iomuxc *)IOMUXC_BASE_ADDR;

	writel(IOMUXC_GPR1_APP_CLK_REQ_N | IOMUXC_GPR1_PCIE_RDY_L23 |
	       IOMUXC_GPR1_EXC_MON_SLVE |
	       (2 << IOMUXC_GPR1_ADDRS0_OFFSET) |
	       IOMUXC_GPR1_ACT_CS0,
	       &iomuxc_regs->gpr[1]);
	writel(0x0, &iomuxc_regs->gpr[8]);
	writel(IOMUXC_GPR12_ARMP_IPG_CLK_EN | IOMUXC_GPR12_ARMP_AHB_CLK_EN |
	       IOMUXC_GPR12_ARMP_ATB_CLK_EN | IOMUXC_GPR12_ARMP_APB_CLK_EN,
	       &iomuxc_regs->gpr[12]);
}

int board_early_init_f(void)
{
	set_gpr_register();
	return 0;
}

static void setup_one_led(char *label, int state)
{
	struct udevice *dev;
	int ret;

	ret = led_get_by_label(label, &dev);
	if (ret == 0)
		led_set_state(dev, state);
}

static void setup_board_gpio(void)
{
	setup_one_led("led_ena", LEDST_ON);
	/* switch off Status LEDs */
	setup_one_led("led_yellow", LEDST_OFF);
	setup_one_led("led_red", LEDST_OFF);
	setup_one_led("led_green", LEDST_OFF);
	setup_one_led("led_blue", LEDST_OFF);
}

#include <i2c_eeprom.h>
#include <i2c.h>
#include <video_fb.h>

#define ARI_RESC_FMT "setenv rescue_reason setenv bootargs \\${bootargs} rescueReason=%d "

static void aristainetos_run_rescue_command(int reason)
{
	char rescue_reason_command[80];

	sprintf(rescue_reason_command, ARI_RESC_FMT, reason);
	run_command(rescue_reason_command, 0);
}

static int aristainetos_eeprom(void)
{
	struct udevice *dev;
	int off;
	int ret;
	uint8_t rescue_reason;
	uint8_t data[16];


	off = fdt_path_offset(gd->fdt_blob, "eeprom0");
	if (off < 0) {
		printf("%s: No eeprom0 path offset\n", __func__);
		return off;
	}

	ret = uclass_get_device_by_of_offset(UCLASS_I2C_EEPROM, off, &dev);
	if (ret) {
		printf("%s: Could not find EEPROM\n", __func__);
		return ret;
	}

	ret = i2c_set_chip_offset_len(dev, 2);
	if (ret)
		return ret;

	/* check if at offset 0x1ff0 the string "Rescue" */
	ret = i2c_eeprom_read(dev, 0x1ff0, (uint8_t *)data, sizeof(data));
	if (ret) {
		printf("%s: Could not read EEPROM\n", __func__);
		return ret;
	}

	if (strncmp((char *)&data[3], "ReScUe", 6) == 0) {
		rescue_reason = *(uint8_t *) &eepromdata[9];
		memset(&data[3], 0xff, 7);
		i2c_eeprom_write(dev, 0x1ff0, (uint8_t *)&data[3], 7);
		printf("\nBooting into Rescue System (EEPROM)\n");
		aristainetos_run_rescue_command(rescue_reason);
		run_command("run rescue_load_fit rescueboot",0);
	} else if (strncmp((char *)data, "DeF", 3) == 0) {
		memset(data, 0xff, 3);
		i2c_eeprom_write(dev, 0x1ff0, (uint8_t *)data, 3);
		printf("\nClear u-boot environment (set back to defaults)\n");
		run_command("run default_env; saveenv; saveenv",0);
	}

	return 0;
};

int board_late_init(void)
{
	struct gpio_desc *desc;
	char *my_bootdelay;
	char bootmode = 0;
	int x, y;

	/*
	 * Check the boot-source. If booting from NOR Flash,
	 * disable bootdelay
	 */
	desc = gpio_hog_lookup_name("bootsel0");
	if (desc)
		bootmode |= (dm_gpio_get_value(desc) ? 1 : 0) << 0;
	desc = gpio_hog_lookup_name("bootsel1");
	if (desc)
		bootmode |= (dm_gpio_get_value(desc) ? 1 : 0) << 1;
	desc = gpio_hog_lookup_name("bootsel2");
	if (desc)
		bootmode |= (dm_gpio_get_value(desc) ? 1 : 0) << 2;

	if (bootmode == 7) {
		my_bootdelay = env_get("nor_bootdelay");
		if (my_bootdelay != NULL)
			env_set("bootdelay", my_bootdelay);
		else
			env_set("bootdelay", "-2");
	}

	splash_get_pos(&x, &y);
	bmp_display((ulong)&bmp_logo_bitmap[0], x, y);

	/* read out some jumper values*/
	desc = gpio_hog_lookup_name("env_reset");
	if (desc) {
		if (dm_gpio_get_value(desc)) {
			printf("\nClear u-boot environment (set back to defaults)\n");
			run_command("run default_env; saveenv; saveenv", 0);
		}
	}
	desc = gpio_hog_lookup_name("boot_rescue");
	if (desc) {
		if (dm_gpio_get_value(desc)) {
			printf("\nBooting into Rescue System\n");
			aristainetos_run_rescue_command(16);
			run_command("run rescue_xload_boot", 0);
		}
	}

	/* eeprom work */
	aristainetos_eeprom();
	return 0;
}
