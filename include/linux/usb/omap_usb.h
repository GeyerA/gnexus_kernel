/*
 * omap_usb.h -- omap usb2 phy header file
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __DRIVERS_OMAP_USB2_H
#define __DRIVERS_OMAP_USB2_H

#include <linux/io.h>
#include <linux/usb/otg.h>

struct omap_usb {
	struct usb_phy		phy;
	struct phy_companion	*comparator;
	void __iomem		*pll_ctrl_base;
	struct device		*dev;
	struct device		*scm_dev;
	struct clk		*optclk;
	struct clk		*wkupclk;
	u8			rev_id;
	u8			is_suspended:1;
	u8			pending_work:1;
	struct notifier_block	nb;
	struct delayed_work     wakeup_work;
	struct workqueue_struct	*workqueue;
};

struct usb_dpll_params {
	u16	m;
	u8	n;
	u8	freq:3;
	u8	sd;
	u32	mf;
};

enum sys_clk_rate {
	CLK_RATE_UNDEFINED = -1,
	CLK_RATE_12MHZ,
	CLK_RATE_16MHZ,
	CLK_RATE_19MHZ,
	CLK_RATE_26MHZ,
	CLK_RATE_38MHZ
};

/* Delay for runtime fw to enable all the modules */
#define DELAYED_WORK_DELAY	500

#define	NUM_SYS_CLKS		5
#define	PLL_STATUS		0x00000004
#define	PLL_GO			0x00000008
#define	PLL_CONFIGURATION1	0x0000000C
#define	PLL_CONFIGURATION2	0x00000010
#define	PLL_CONFIGURATION3	0x00000014
#define	PLL_CONFIGURATION4	0x00000020

#define	PLL_REGM_MASK		0x001FFE00
#define	PLL_REGM_SHIFT		0x9
#define	PLL_REGM_F_MASK		0x0003FFFF
#define	PLL_REGM_F_SHIFT	0x0
#define	PLL_REGN_MASK		0x000001FE
#define	PLL_REGN_SHIFT		0x1
#define	PLL_SELFREQDCO_MASK	0x0000000E
#define	PLL_SELFREQDCO_SHIFT	0x1
#define	PLL_SD_MASK		0x0003FC00
#define	PLL_SD_SHIFT		0x9
#define	SET_PLL_GO		0x1
#define	PLL_LOCK		0x2
#define	PLL_IDLE		0x1

struct omap_usb_platform_data {
	u8	rev_id;
};

#define	phy_to_omapusb(x)	container_of((x), struct omap_usb, phy)

static inline u32 omap_usb_readl(const void __iomem *addr, unsigned offset)
{
	return __raw_readl(addr + offset);
}

static inline void omap_usb_writel(const void __iomem *addr, unsigned offset,
								u32 data)
{
	__raw_writel(data, addr + offset);
}

#endif /* __DRIVERS_OMAP_USB_H */
