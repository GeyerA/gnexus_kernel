/*
 * arch/arm/mach-omap2/board-omap5evm.h
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MACH_OMAP_BOARD_OMAP5EVM_H
#define _MACH_OMAP_BOARD_OMAP5EVM_H

int omap5evm_sensor_init(void);
void omap5_create_board_props(char *bname, char *rev, char *vname);

extern struct usbhs_omap_board_data usbhs_bdata;



#ifdef CONFIG_OMAP_HSI
extern void __init omap5evm_modem_init(bool force_mux);
#else
static inline void __init omap5evm_modem_init(bool force_mux) { }
#endif

#endif
