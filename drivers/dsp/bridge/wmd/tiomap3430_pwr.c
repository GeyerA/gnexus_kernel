/*
 * tiomap_pwr.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of DSP wake/sleep routines.
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbg.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/brddefs.h>
#include <dspbridge/dev.h>
#include <dspbridge/iodefs.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_dspssC64P.h>
#include <hw_prcm.h>
#include <hw_mmu.h>

#include <dspbridge/pwr_sh.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmddeh.h>

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "_tiomap_util.h"
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>

#ifdef CONFIG_PM
#include <mach/board-3430sdp.h>
#endif
extern struct MAILBOX_CONTEXT mboxsetting;
extern unsigned short enable_off_mode;

extern unsigned short min_active_opp;
/*
 *  ======== handle_constraints_set ========
 *  	Sets new DSP constraint
 */
DSP_STATUS handle_constraints_set(struct WMD_DEV_CONTEXT *pDevContext,
				  IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 pConstraintVal;
	struct dspbridge_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	pConstraintVal = *(((u32 *)pArgs) + 1);
	/* Read the target value requested by DSP  */
	dev_dbg(bridge, "OPP: %s opp requested = %p\n", __func__,
						(u32 *)pConstraintVal);

	/* Set the new opp value */
	if (pdata->dsp_set_min_opp) {
		/*
		 * When Smartreflex is ON, DSP requires at least OPP level 3
		 * to operate reliably. So boost lower OPP levels to OPP3.
		 */
		if (pConstraintVal < min_active_opp) {
			pr_debug("DSPBRIDGE: VDD1 OPP%x elevated to OPP%x\n",
					pConstraintVal, min_active_opp);
			(*pdata->dsp_set_min_opp)(min_active_opp);
		} else
			(*pdata->dsp_set_min_opp)(pConstraintVal);
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ======== handle_hibernation_fromDSP ========
 *  	Handle Hibernation requested from DSP
 */
DSP_STATUS handle_hibernation_fromDSP(struct WMD_DEV_CONTEXT *pDevContext)
{
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_PM
	u16 timeout = PWRSTST_TIMEOUT / 10;
	enum HW_PwrState_t pwrState;
	u32  prev_state;
#ifdef BRIDGE_NTFY_PWRERR
	struct DEH_MGR *hDehMgr;
#endif /* #ifdef BRIDGE_NTFY_PWRERR */
#ifdef CONFIG_BRIDGE_DVFS
	u32 opplevel;
	struct IO_MGR *hIOMgr;
	struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
#endif

	prev_state = pDevContext->dwBrdState;
	pDevContext->dwBrdState = BRD_SLEEP_TRANSITION;

	HW_PWR_IVA2StateGet(pDevContext->prmbase, HW_PWR_DOMAIN_DSP,
			    &pwrState);
	/* Wait for DSP to move into OFF state */
	while ((pwrState != HW_PWR_STATE_OFF) && --timeout) {
		if (msleep_interruptible(10)) {
			pr_err("Waiting for DSP OFF mode interrupted\n");
			return DSP_EFAIL;
		}
		HW_PWR_IVA2StateGet(pDevContext->prmbase, HW_PWR_DOMAIN_DSP,
				    &pwrState);
	}
	if (timeout == 0) {
		pDevContext->dwBrdState = prev_state;
		pr_err("Timed out waiting for DSP self hibernation\n");
#ifdef BRIDGE_NTFY_PWRERR
		DEV_GetDehMgr(pDevContext->hDevObject, &hDehMgr);
		WMD_DEH_Notify(hDehMgr, DSP_PWRERROR, 0);
#endif /* #ifdef BRIDGE_NTFY_PWRERR */
		status = WMD_E_TIMEOUT;
	} else {
		/* Save mailbox settings */
		omap_mbox_save_ctx(pDevContext->mbox);

		/* Turn off DSP Peripheral clocks and DSP Load monitor timer */
		status = DSP_PeripheralClocks_Disable(pDevContext, NULL);

		if (DSP_SUCCEEDED(status)) {
			/* Update the Bridger Driver state */
			pDevContext->dwBrdState = BRD_DSP_HIBERNATION;
#ifdef CONFIG_BRIDGE_DVFS
			status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);
			if (DSP_FAILED(status))
				return status;
			IO_SHMsetting(hIOMgr, SHM_GETOPP, &opplevel);

			/*
			 * Set the OPP to low level before moving to OFF
			 * mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp)(VDD1_OPP1);
			status = DSP_SOK;
#endif /* CONFIG_BRIDGE_DVFS */
		} else {
			pDevContext->dwBrdState = prev_state;
		}
	}
#endif
	return status;
}

/*
 *  ======== SleepDSP ========
 *  	Put DSP in low power consuming state.
 */
DSP_STATUS SleepDSP(struct WMD_DEV_CONTEXT *pDevContext, IN u32 dwCmd,
		   IN void *pArgs)
{
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_PM
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
	struct DEH_MGR *hDehMgr;
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
	u16 timeout = PWRSTST_TIMEOUT / 10;
	u32 prev_state;
	enum HW_PwrState_t pwrState, targetPwrState;

	/* Check if sleep code is valid */
	if ((dwCmd != PWR_DEEPSLEEP) && (dwCmd != PWR_EMERGENCYDEEPSLEEP))
		return DSP_EINVALIDARG;

	switch (pDevContext->dwBrdState) {
	case BRD_RUNNING:
		omap_mbox_save_ctx(pDevContext->mbox);
		if (enable_off_mode) {
			sm_interrupt_dsp(pDevContext,
					     MBX_PM_DSPHIBERNATE);
			dev_dbg(bridge, "PM: %s - sent hibernate cmd to DSP\n",
								__func__);
			targetPwrState = HW_PWR_STATE_OFF;
		} else {
			sm_interrupt_dsp(pDevContext,
					     MBX_PM_DSPRETENTION);
			targetPwrState = HW_PWR_STATE_RET;
		}
		prev_state = BRD_RUNNING;
		break;
	case BRD_RETENTION:
		omap_mbox_save_ctx(pDevContext->mbox);
		if (enable_off_mode) {
			sm_interrupt_dsp(pDevContext,
					     MBX_PM_DSPHIBERNATE);
			targetPwrState = HW_PWR_STATE_OFF;
		} else
			return DSP_SOK;

		prev_state = BRD_RETENTION;
		break;
	case BRD_HIBERNATION:
	case BRD_DSP_HIBERNATION:
		/* Already in Hibernation, so just return */
		dev_dbg(bridge, "PM: %s - DSP already in hibernation\n",
								__func__);
		return DSP_SOK;
	case BRD_STOPPED:
		dev_dbg(bridge, "PM: %s - Board in STOP state\n", __func__);
		return DSP_SALREADYASLEEP;
	default:
		dev_dbg(bridge, "PM: %s - Bridge in Illegal state\n", __func__);
			return DSP_EFAIL;
	}
	pDevContext->dwBrdState = BRD_SLEEP_TRANSITION;
	/* Get the PRCM DSP power domain status */
	HW_PWR_IVA2StateGet(pDevContext->prmbase, HW_PWR_DOMAIN_DSP,
			&pwrState);

	/* Wait for DSP to move into target power state */
	while ((pwrState != targetPwrState) && --timeout) {
		if (msleep_interruptible(10)) {
			pr_err("Waiting for DSP to Suspend interrupted\n");
			return DSP_EFAIL;
		}
		HW_PWR_IVA2StateGet(pDevContext->prmbase, HW_PWR_DOMAIN_DSP,
				    &pwrState);
	}

	if (!timeout) {
		pDevContext->dwBrdState = prev_state;
		pr_err("Timed out waiting for DSP suspend %x\n", pwrState);
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
		DEV_GetDehMgr(pDevContext->hDevObject, &hDehMgr);
		WMD_DEH_Notify(hDehMgr, DSP_PWRERROR, 0);
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
		return WMD_E_TIMEOUT;
	} else {
		/* Update the Bridger Driver state */
		if (enable_off_mode)
			pDevContext->dwBrdState = BRD_HIBERNATION;
		else
			pDevContext->dwBrdState = BRD_RETENTION;

		/* Turn off DSP Peripheral clocks  */
		status = DSP_PeripheralClocks_Disable(pDevContext, NULL);
		if (DSP_FAILED(status)) {
			return status;
		}
#ifdef CONFIG_BRIDGE_DVFS
		else if (enable_off_mode) {
			struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
			/*
			 * Set the OPP to low level before moving to OFF mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp)(VDD1_OPP1);
		}
#endif /* CONFIG_BRIDGE_DVFS */
	}
#endif /* CONFIG_PM */
	return status;
}


/*
 *  ======== WakeDSP ========
 *  	Wake up DSP from sleep.
 */
DSP_STATUS WakeDSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
#ifdef CONFIG_PM
#ifdef CONFIG_BRIDGE_DEBUG
	enum HW_PwrState_t pwrState;
#endif /* CONFIG_BRIDGE_DEBUG */

	/* Check the BRD/WMD state, if it is not 'SLEEP' then return failure */
	if (pDevContext->dwBrdState == BRD_RUNNING ||
	    pDevContext->dwBrdState == BRD_STOPPED) {
		/* The Device is in 'RET' or 'OFF' state and WMD state is not
		 * 'SLEEP', this means state inconsistency, so return  */
		return DSP_SOK;
	}

	/* Send a wakeup message to DSP */
	sm_interrupt_dsp(pDevContext, MBX_PM_DSPWAKEUP);

#ifdef CONFIG_BRIDGE_DEBUG
	HW_PWR_IVA2StateGet(pDevContext->prmbase, HW_PWR_DOMAIN_DSP,
			&pwrState);
#endif /* CONFIG_BRIDGE_DEBUG */

	/* Set the device state to RUNNIG */
	pDevContext->dwBrdState = BRD_RUNNING;
#endif /* CONFIG_PM */
	return DSP_SOK;
}

/*
 *  ======== DSPPeripheralClkCtrl ========
 *  	Enable/Disable the DSP peripheral clocks as needed..
 */
DSP_STATUS DSPPeripheralClkCtrl(struct WMD_DEV_CONTEXT *pDevContext,
				IN void *pArgs)
{
	u32 extClk = 0;
	u32 extClkId = 0;
	u32 extClkCmd = 0;
	u32 clkIdIndex = MBX_PM_MAX_RESOURCES;
	u32 tmpIndex;
	u32 dspPerClksBefore;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	u32 value;

	dspPerClksBefore = pDevContext->uDspPerClks;

	extClk = (u32)*((u32 *)pArgs);

	extClkId = extClk & MBX_PM_CLK_IDMASK;

	/* process the power message -- TODO, keep it in a separate function */
	for (tmpIndex = 0; tmpIndex < MBX_PM_MAX_RESOURCES; tmpIndex++) {
		if (extClkId == BPWR_CLKID[tmpIndex]) {
			clkIdIndex = tmpIndex;
			break;
		}
	}
	/* TODO -- Assert may be a too hard restriction here.. May be we should
	 * just return with failure when the CLK ID does not match */
	/* DBC_Assert(clkIdIndex < MBX_PM_MAX_RESOURCES);*/
	if (clkIdIndex == MBX_PM_MAX_RESOURCES) {
		/* return with a more meaningfull error code */
		return DSP_EFAIL;
	}
	extClkCmd = (extClk >> MBX_PM_CLK_CMDSHIFT) & MBX_PM_CLK_CMDMASK;
	switch (extClkCmd) {
	case BPWR_DisableClock:
		/* Call BP to disable the needed clock */
		status1 = CLK_Disable(BPWR_Clks[clkIdIndex].intClk);
		status = CLK_Disable(BPWR_Clks[clkIdIndex].funClk);
		if (BPWR_CLKID[clkIdIndex] == BPWR_MCBSP1) {
			/* clear MCBSP1_CLKS, on McBSP1 OFF */
			value = __raw_readl(pDevContext->sysctrlbase + 0x274);
			value &= ~(1 << 2);
			__raw_writel(value, pDevContext->sysctrlbase + 0x274);
		} else if (BPWR_CLKID[clkIdIndex] == BPWR_MCBSP2) {
			/* clear MCBSP2_CLKS, on McBSP2 OFF */
			value = __raw_readl(pDevContext->sysctrlbase + 0x274);
			value &= ~(1 << 6);
			__raw_writel(value, pDevContext->sysctrlbase + 0x274);
		}
		DSPClkWakeupEventCtrl(BPWR_Clks[clkIdIndex].clkId, false);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(pDevContext->uDspPerClks) &=
				(~((u32) (1 << clkIdIndex)));
		}
		break;
	case BPWR_EnableClock:
		status1 = CLK_Enable(BPWR_Clks[clkIdIndex].intClk);
		status = CLK_Enable(BPWR_Clks[clkIdIndex].funClk);
		if (BPWR_CLKID[clkIdIndex] == BPWR_MCBSP1) {
			/* set MCBSP1_CLKS, on McBSP1 ON */
			value = __raw_readl(pDevContext->sysctrlbase + 0x274);
			value |= 1 << 2;
			__raw_writel(value, pDevContext->sysctrlbase + 0x274);
		} else if (BPWR_CLKID[clkIdIndex] == BPWR_MCBSP2) {
			/* set MCBSP2_CLKS, on McBSP2 ON */
			value = __raw_readl(pDevContext->sysctrlbase + 0x274);
			value |= 1 << 6;
			__raw_writel(value, pDevContext->sysctrlbase + 0x274);
		}
		DSPClkWakeupEventCtrl(BPWR_Clks[clkIdIndex].clkId, true);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(pDevContext->uDspPerClks) |= (1 << clkIdIndex);
		}
		break;
	default:
		dev_dbg(bridge, "%s: Unsupported CMD\n", __func__);
		/* unsupported cmd */
		/* TODO -- provide support for AUTOIDLE Enable/Disable
		 * commands */
	}
	return status;
}

/*
 *  ========PreScale_DSP========
 *  Sends prescale notification to DSP
 *
 */
DSP_STATUS PreScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;

	voltage_domain = *((u32 *)pArgs);
	level = *((u32 *)pArgs + 1);

	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
					__func__, voltage_domain, level);
	if ((pDevContext->dwBrdState == BRD_HIBERNATION) ||
			(pDevContext->dwBrdState == BRD_RETENTION) ||
			(pDevContext->dwBrdState == BRD_DSP_HIBERNATION)) {
		dev_dbg(bridge, "OPP: %s IVA in sleep. No message to DSP\n",
								__func__);
		return DSP_SOK;
	} else if ((pDevContext->dwBrdState == BRD_RUNNING)) {
		/* Send a prenotificatio to DSP */
		dev_dbg(bridge, "OPP: %s sent notification to DSP\n", __func__);
		sm_interrupt_dsp(pDevContext, MBX_PM_SETPOINT_PRENOTIFY);
		return DSP_SOK;
	} else {
		return DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ========PostScale_DSP========
 *  Sends postscale notification to DSP
 *
 */
DSP_STATUS PostScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;
	struct IO_MGR *hIOMgr;

	status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);
	if (!hIOMgr)
		return DSP_EHANDLE;

	voltage_domain = *((u32 *)pArgs);
	level = *((u32 *)pArgs + 1);
	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
					__func__, voltage_domain, level);
	if ((pDevContext->dwBrdState == BRD_HIBERNATION) ||
			(pDevContext->dwBrdState == BRD_RETENTION) ||
			(pDevContext->dwBrdState == BRD_DSP_HIBERNATION) ||
			(pDevContext->dwBrdState == BRD_SLEEP_TRANSITION)) {
		/* Update the OPP value in shared memory */
		IO_SHMsetting(hIOMgr, SHM_CURROPP, &level);
		dev_dbg(bridge, "OPP: %s IVA in sleep. Wrote to SHM\n",
								__func__);
	} else  if ((pDevContext->dwBrdState == BRD_RUNNING)) {
		/* Update the OPP value in shared memory */
		IO_SHMsetting(hIOMgr, SHM_CURROPP, &level);
		/* Send a post notification to DSP */
		sm_interrupt_dsp(pDevContext, MBX_PM_SETPOINT_POSTNOTIFY);
		dev_dbg(bridge, "OPP: %s wrote to SHM. Sent post notification "
							"to DSP\n", __func__);
	} else {
		status = DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return status;
}

/*
 *  ========DSP_PeripheralClocks_Disable========
 *  Disables all the peripheral clocks that were requested by DSP
 */
DSP_STATUS DSP_PeripheralClocks_Disable(struct WMD_DEV_CONTEXT *pDevContext,
					IN void *pArgs)
{
	u32 clkIdx;
	DSP_STATUS status = DSP_SOK;
	u32 value;

	for (clkIdx = 0; clkIdx < MBX_PM_MAX_RESOURCES; clkIdx++) {
		if (((pDevContext->uDspPerClks) >> clkIdx) & 0x01) {
			/* Disables the interface clock of the peripheral */
			status = CLK_Disable(BPWR_Clks[clkIdx].intClk);
			if (BPWR_CLKID[clkIdx] == BPWR_MCBSP1) {
				/* clear MCBSP1_CLKS, on McBSP1 OFF */
				value = __raw_readl(pDevContext->sysctrlbase
								+ 0x274);
				value &= ~(1 << 2);
				__raw_writel(value, pDevContext->sysctrlbase
								+ 0x274);
			} else if (BPWR_CLKID[clkIdx] == BPWR_MCBSP2) {
				/* clear MCBSP2_CLKS, on McBSP2 OFF */
				value = __raw_readl(pDevContext->sysctrlbase
								+ 0x274);
				value &= ~(1 << 6);
				__raw_writel(value, pDevContext->sysctrlbase
								+ 0x274);
			}

			/* Disables the functional clock of the periphearl */
			status = CLK_Disable(BPWR_Clks[clkIdx].funClk);
		}
	}
	return status;
}

/*
 *  ========DSP_PeripheralClocks_Enable========
 *  Enables all the peripheral clocks that were requested by DSP
 */
DSP_STATUS DSP_PeripheralClocks_Enable(struct WMD_DEV_CONTEXT *pDevContext,
				      IN void *pArgs)
{
	u32 clkIdx;
	DSP_STATUS int_clk_status = DSP_EFAIL, fun_clk_status = DSP_EFAIL;
	u32 value;

	for (clkIdx = 0; clkIdx < MBX_PM_MAX_RESOURCES; clkIdx++) {
		if (((pDevContext->uDspPerClks) >> clkIdx) & 0x01) {
			/* Enable the interface clock of the peripheral */
			int_clk_status = CLK_Enable(BPWR_Clks[clkIdx].intClk);
			if (BPWR_CLKID[clkIdx] == BPWR_MCBSP1) {
				/* set MCBSP1_CLKS, on McBSP1 ON */
				value = __raw_readl(pDevContext->sysctrlbase
								+ 0x274);
				value |= 1 << 2;
				__raw_writel(value, pDevContext->sysctrlbase
								+ 0x274);
			} else if (BPWR_CLKID[clkIdx] == BPWR_MCBSP2) {
				/* set MCBSP2_CLKS, on McBSP2 ON */
				value = __raw_readl(pDevContext->sysctrlbase
								+ 0x274);
				value |= 1 << 6;
				__raw_writel(value, pDevContext->sysctrlbase
								+ 0x274);
			}
			/* Enable the functional clock of the periphearl */
			fun_clk_status = CLK_Enable(BPWR_Clks[clkIdx].funClk);
		}
	}
	if ((int_clk_status | fun_clk_status) != DSP_SOK)
		return DSP_EFAIL;
	return DSP_SOK;
}

void DSPClkWakeupEventCtrl(u32 ClkId, bool enable)
{
	struct CFG_HOSTRES resources;
	DSP_STATUS status = DSP_SOK;
	u32 iva2_grpsel;
	u32 mpu_grpsel;

	status = CFG_GetHostResources(
		(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		return;

	switch (ClkId) {
	case BPWR_GPTimer5:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
				= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
				= mpu_grpsel;
	break;
	case BPWR_GPTimer6:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT6;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT6;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_GPTimer7:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT7;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT7;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_GPTimer8:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT8;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT8;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP1:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP2:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP3:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP4:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP5:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA4))
							= mpu_grpsel;
	break;
	}
}

/*
 *  ======== tiomap3430_bump_dsp_opp_level ========
 *  	This function bumps DSP OPP level if it is OPP1
 */
DSP_STATUS tiomap3430_bump_dsp_opp_level(void)
{
#ifdef CONFIG_BRIDGE_DVFS
	struct WMD_DEV_CONTEXT *dwContext;
	struct DEV_OBJECT *hDevObject =
			(struct DEV_OBJECT *)DRV_GetFirstDevObject();
	struct dspbridge_platform_data *pdata =
			omap_dspbridge_dev->dev.platform_data;

	if (DSP_FAILED(DEV_GetWMDContext(hDevObject, &dwContext)))
		return DSP_EFAIL;

	if (dwContext->dwBrdState == BRD_DSP_HIBERNATION ||
	    dwContext->dwBrdState == BRD_HIBERNATION) {
		/*
		 * Increase OPP before waking up the DSP.
		 */
		(*pdata->dsp_set_min_opp)(min_active_opp);
		dev_dbg(bridge, "CHNLSM_InterruptDSP: Setting "
			"the vdd1 constraint level to %d before "
			"waking DSP \n", min_active_opp);
	}
#endif
	return DSP_SOK;
}
