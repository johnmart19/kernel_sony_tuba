/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/of_fdt.h>
#include <asm/setup.h>

#ifndef CONFIG_ARM64
#include <mach/irqs.h>
#else
#include <linux/irqchip/mt-gic.h>
#endif
#if defined(CONFIG_MTK_SYS_CIRQ)
#include <mt-plat/mt_cirq.h>
#endif
#include <mach/mt_clkmgr.h>
#include "mt_cpuidle.h"
#ifdef CONFIG_MTK_WD_KICKER
#include <mach/wd_api.h>
#endif
#include "mt_cpufreq.h"
#include <mt-plat/upmu_common.h>
#include "mt_spm_misc.h"
#include <mt_clkbuf_ctl.h>

#if 1
#include <mt_dramc.h>
#endif

#include "mt_spm_internal.h"
#if !defined(CONFIG_FPGA_EARLY_PORTING)
#include "mt_spm_pmic_wrap.h"
#endif

#include <mt-plat/mt_ccci_common.h>

#ifdef CONFIG_MTK_USB2JTAG_SUPPORT
#include <mt-plat/mt_usb2jtag.h>
#endif

#if defined(CONFIG_ARCH_MT6797)
#include "mt_vcorefs_governor.h"
#endif

/**************************************
 * only for internal debug
 **************************************/
#ifdef CONFIG_MTK_LDVT
#define SPM_PWAKE_EN            0
#define SPM_PCMWDT_EN           0
#define SPM_BYPASS_SYSPWREQ     1
#else
#define SPM_PWAKE_EN            1
#define SPM_PCMWDT_EN           1
#define SPM_BYPASS_SYSPWREQ     0
#endif

#ifdef CONFIG_OF
#define MCUCFG_BASE          spm_mcucfg
#else
#define MCUCFG_BASE          (0xF0200000)	/* 0x1020_0000 */
#endif
#define MP0_AXI_CONFIG          (MCUCFG_BASE + 0x2C)
#define MP1_AXI_CONFIG          (MCUCFG_BASE + 0x22C)
#if defined(CONFIG_ARCH_MT6797)
#define MP2_AXI_CONFIG          (MCUCFG_BASE + 0x220C)
#endif
#define ACINACTM                (1<<4)

int spm_dormant_sta = MT_CPU_DORMANT_RESET;
int spm_ap_mdsrc_req_cnt = 0;

struct wake_status suspend_info[20];
u32 log_wakesta_cnt = 0;
u32 log_wakesta_index = 0;
u8 spm_snapshot_golden_setting = 0;

struct wake_status spm_wakesta; /* record last wakesta */

/**************************************

 * SW code for suspend
 **************************************/
#define SPM_SYSCLK_SETTLE       99	/* 3ms */

#define WAIT_UART_ACK_TIMES     10	/* 10 * 10us */

#if defined(CONFIG_ARCH_MT6755) || defined(CONFIG_ARCH_MT6757)

#if defined(CONFIG_MICROTRUST_TEE_SUPPORT)
#define WAKE_SRC_FOR_SUSPEND \
	(WAKE_SRC_R12_MD32_WDT_EVENT_B | \
	WAKE_SRC_R12_KP_IRQ_B | \
	WAKE_SRC_R12_PCM_TIMER | \
	WAKE_SRC_R12_CONN2AP_SPM_WAKEUP_B | \
	WAKE_SRC_R12_EINT_EVENT_B | \
	WAKE_SRC_R12_CONN_WDT_IRQ_B | \
	WAKE_SRC_R12_CCIF0_EVENT_B | \
	WAKE_SRC_R12_CCIF1_EVENT_B | \
	WAKE_SRC_R12_MD32_SPM_IRQ_B | \
	WAKE_SRC_R12_USB_CDSC_B | \
	WAKE_SRC_R12_USB_POWERDWN_B | \
	WAKE_SRC_R12_EINT_EVENT_SECURE_B | \
	WAKE_SRC_R12_MD1_WDT_B | \
	WAKE_SRC_R12_MD2_WDT_B | \
	WAKE_SRC_R12_CLDMA_EVENT_B | \
	WAKE_SRC_R12_ALL_MD32_WAKEUP_B)
#else
#define WAKE_SRC_FOR_SUSPEND \
	(WAKE_SRC_R12_MD32_WDT_EVENT_B | \
	WAKE_SRC_R12_KP_IRQ_B | \
	WAKE_SRC_R12_PCM_TIMER | \
	WAKE_SRC_R12_CONN2AP_SPM_WAKEUP_B | \
	WAKE_SRC_R12_EINT_EVENT_B | \
	WAKE_SRC_R12_CONN_WDT_IRQ_B | \
	WAKE_SRC_R12_CCIF0_EVENT_B | \
	WAKE_SRC_R12_CCIF1_EVENT_B | \
	WAKE_SRC_R12_MD32_SPM_IRQ_B | \
	WAKE_SRC_R12_USB_CDSC_B | \
	WAKE_SRC_R12_USB_POWERDWN_B | \
	WAKE_SRC_R12_EINT_EVENT_SECURE_B | \
	WAKE_SRC_R12_MD1_WDT_B | \
	WAKE_SRC_R12_MD2_WDT_B | \
	WAKE_SRC_R12_CLDMA_EVENT_B | \
	WAKE_SRC_R12_SEJ_WDT_GPT_B | \
	WAKE_SRC_R12_ALL_MD32_WAKEUP_B)
#endif /* #if defined(CONFIG_MICROTRUST_TEE_SUPPORT) */

#else

#if defined(CONFIG_MICROTRUST_TEE_SUPPORT)
#define WAKE_SRC_FOR_SUSPEND \
	(WAKE_SRC_R12_PCM_TIMER | \
	WAKE_SRC_R12_MD32_WDT_EVENT_B | \
	WAKE_SRC_R12_KP_IRQ_B | \
	WAKE_SRC_R12_CONN2AP_SPM_WAKEUP_B | \
	WAKE_SRC_R12_EINT_EVENT_B | \
	WAKE_SRC_R12_CONN_WDT_IRQ_B | \
	WAKE_SRC_R12_CCIF0_EVENT_B | \
	WAKE_SRC_R12_MD32_SPM_IRQ_B | \
	WAKE_SRC_R12_USB_CDSC_B | \
	WAKE_SRC_R12_USB_POWERDWN_B | \
	WAKE_SRC_R12_C2K_WDT_IRQ_B | \
	WAKE_SRC_R12_EINT_EVENT_SECURE_B | \
	WAKE_SRC_R12_CCIF1_EVENT_B | \
	WAKE_SRC_R12_SYS_CIRQ_IRQ_B | \
	WAKE_SRC_R12_CSYSPWREQ_B | \
	WAKE_SRC_R12_MD1_WDT_B | \
	WAKE_SRC_R12_CLDMA_EVENT_B)
#else
#define WAKE_SRC_FOR_SUSPEND \
	(WAKE_SRC_R12_PCM_TIMER | \
	WAKE_SRC_R12_MD32_WDT_EVENT_B | \
	WAKE_SRC_R12_KP_IRQ_B | \
	WAKE_SRC_R12_CONN2AP_SPM_WAKEUP_B | \
	WAKE_SRC_R12_EINT_EVENT_B | \
	WAKE_SRC_R12_CONN_WDT_IRQ_B | \
	WAKE_SRC_R12_CCIF0_EVENT_B | \
	WAKE_SRC_R12_MD32_SPM_IRQ_B | \
	WAKE_SRC_R12_USB_CDSC_B | \
	WAKE_SRC_R12_USB_POWERDWN_B | \
	WAKE_SRC_R12_C2K_WDT_IRQ_B | \
	WAKE_SRC_R12_EINT_EVENT_SECURE_B | \
	WAKE_SRC_R12_CCIF1_EVENT_B | \
	WAKE_SRC_R12_SYS_CIRQ_IRQ_B | \
	WAKE_SRC_R12_CSYSPWREQ_B | \
	WAKE_SRC_R12_MD1_WDT_B | \
	WAKE_SRC_R12_CLDMA_EVENT_B | \
	WAKE_SRC_R12_SEJ_WDT_GPT_B)
#endif /* #if defined(CONFIG_MICROTRUST_TEE_SUPPORT) */

#endif

#define WAKE_SRC_FOR_MD32  0 \
				/* (WAKE_SRC_AUD_MD32) */

#define spm_is_wakesrc_invalid(wakesrc)     (!!((u32)(wakesrc) & 0xc0003803))

/* FIXME: check mt_cpu_dormant */
int __attribute__ ((weak)) mt_cpu_dormant(unsigned long flags)
{
	return 0;
}

void __attribute__((weak)) mt_eint_print_status(void)
{
}

#if defined(CONFIG_ARCH_MT6797)
void __attribute__((weak)) mt_cirq_clone_gic(void)
{
}

void __attribute__((weak)) mt_cirq_enable(void)
{
}

void __attribute__((weak)) mt_cirq_flush(void)
{
}

void __attribute__((weak)) mt_cirq_disable(void)
{
}
#endif

#if SPM_AEE_RR_REC
enum spm_suspend_step {
	SPM_SUSPEND_ENTER = 0,
	SPM_SUSPEND_ENTER_WFI = 0xff,
	SPM_SUSPEND_LEAVE_WFI = 0x1ff,
	SPM_SUSPEND_LEAVE
};
#endif

#if defined(CONFIG_ARCH_MT6757)
static struct pwr_ctrl suspend_ctrl = {
	.wake_src = WAKE_SRC_FOR_SUSPEND,
	.wake_src_md32 = WAKE_SRC_FOR_MD32,
	.r0_ctrl_en = 1,
	.r7_ctrl_en = 1,
	.infra_dcm_lock = 1,

	/* SPM_AP_STANDBY_CON */
	.wfi_op = WFI_OP_AND,
	.mp0_cputop_idle_mask = 0,
	.mp1_cputop_idle_mask = 0,
	.mcusys_idle_mask = 0,
	.mm_mask_b = 0,
	.md_ddr_en_dbc_en = 0,
	.md_mask_b = 1,
	.scp_mask_b = 0,
	.lte_mask_b = 0,
	.srcclkeni_mask_b = 0,
	.md_apsrc_1_sel = 0,
	.md_apsrc_0_sel = 0,
	.conn_mask_b = 1,
	.conn_apsrc_sel = 0,

	/* SPM_SRC_REQ */
	.spm_apsrc_req = 1,
	.spm_f26m_req = 1,
	.spm_lte_req = 1,
	.spm_infra_req = 1,
	.spm_vrf18_req = 1,
	.spm_dvfs_req = 0,
	.spm_dvfs_force_down = 1,
	.spm_ddren_req = 1,
	.spm_rsv_src_req = 0,
	.cpu_md_dvfs_sop_force_on = 0,

#if 0 /* temporarily fix build fail */ /* !defined(CONFIG_FPGA_EARLY_PORTING) */
	/* for DVFS */
	.sw_crtl_event_on = 1,
#endif

	/* SPM_SRC_MASK */
#if SPM_BYPASS_SYSPWREQ
	.csyspwreq_mask = 1,
#endif
	.ccif0_md_event_mask_b = 1,
	.ccif0_ap_event_mask_b = 1,
	.ccif1_md_event_mask_b = 1,
	.ccif1_ap_event_mask_b = 1,
	.ccifmd_md1_event_mask_b = 1,
	.ccifmd_md2_event_mask_b = 1,
	.dsi0_vsync_mask_b = 0,
	.dsi1_vsync_mask_b = 0,
	.dpi_vsync_mask_b = 0,
	.isp0_vsync_mask_b = 0,
	.isp1_vsync_mask_b = 0,
	.md_srcclkena_0_infra_mask_b = 0,
	.md_srcclkena_1_infra_mask_b = 0,
	.conn_srcclkena_infra_mask_b = 0,
	.md32_srcclkena_infra_mask_b = 0,
#if defined(CONFIG_ARCH_MT6755)
	.srcclkeni_infra_mask_b = 1,
#else
	.srcclkeni_infra_mask_b = 0,
#endif
	.md_apsrc_req_0_infra_mask_b = 1,
	.md_apsrc_req_1_infra_mask_b = 0,
	.conn_apsrcreq_infra_mask_b = 1,
	.md32_apsrcreq_infra_mask_b = 0,
	.md_ddr_en_0_mask_b = 1,
	.md_ddr_en_1_mask_b = 0,
	.md_vrf18_req_0_mask_b = 1,
	.md_vrf18_req_1_mask_b = 0,
	.md1_dvfs_req_mask = 0,
	.cpu_dvfs_req_mask = 0,
	.emi_bw_dvfs_req_mask = 1,
	.md_srcclkena_0_dvfs_req_mask_b = 0,
	.md_srcclkena_1_dvfs_req_mask_b = 0,
	.conn_srcclkena_dvfs_req_mask_b = 0,

	/* SPM_SRC2_MASK */
	.dvfs_halt_mask_b = 0x1f,
	.vdec_req_mask_b = 0,
	.gce_req_mask_b = 0,
	.cpu_md_dvfs_req_merge_mask_b = 0,
	.md_ddr_en_dvfs_halt_mask_b = 0,
	.dsi0_vsync_dvfs_halt_mask_b = 0,
	.dsi1_vsync_dvfs_halt_mask_b = 0,
	.dpi_vsync_dvfs_halt_mask_b = 0,
	.isp0_vsync_dvfs_halt_mask_b = 0,
	.isp1_vsync_dvfs_halt_mask_b = 0,
	.conn_ddr_en_mask_b = 1,
	.disp_req_mask_b = 0,
	.disp1_req_mask_b = 0,
	.mfg_req_mask_b = 0,
	.c2k_ps_rccif_wake_mask_b = 1,
	.c2k_l1_rccif_wake_mask_b = 1,
	.ps_c2k_rccif_wake_mask_b = 1,
	.l1_c2k_rccif_wake_mask_b = 1,
	.sdio_on_dvfs_req_mask_b = 0,
	.emi_boost_dvfs_req_mask_b = 0,
	.cpu_md_emi_dvfs_req_prot_dis = 0,
	.dramc_spcmd_apsrc_req_mask_b = 0,

#if 0
	/* SPM_WAKEUP_EVENT_MASK */
	.spm_wakeup_event_mask = 0,

	/* SPM_WAKEUP_EVENT_EXT_MASK */
	.spm_wakeup_event_ext_mask = 0,
#endif

	/* SPM_CLK_CON */
	.srclkenai_mask = 1,

	.mp0_cpu0_wfi_en = 1,
	.mp0_cpu1_wfi_en = 1,
	.mp0_cpu2_wfi_en = 1,
	.mp0_cpu3_wfi_en = 1,
	.mp1_cpu0_wfi_en = 1,
	.mp1_cpu1_wfi_en = 1,
	.mp1_cpu2_wfi_en = 1,
	.mp1_cpu3_wfi_en = 1,
};
#else
static struct pwr_ctrl suspend_ctrl = {

	.wake_src = WAKE_SRC_FOR_SUSPEND,
	.wake_src_md32 = WAKE_SRC_FOR_MD32,
	.r0_ctrl_en = 1,
	.r7_ctrl_en = 1,
	.infra_dcm_lock = 1,
	.wfi_op = WFI_OP_AND,

	.mp0top_idle_mask = 0,
	.mp1top_idle_mask = 0,
#if defined(CONFIG_ARCH_MT6797)
	.mp2top_idle_mask = 0,
	.mp3top_idle_mask = 1,
	.mptop_idle_mask = 0,
#endif
	.mcusys_idle_mask = 0,
	.md_ddr_dbc_en = 0,
	.md1_req_mask_b = 1,
	.md2_req_mask_b = 0,
#if defined(CONFIG_ARCH_MT6755)
	.scp_req_mask_b = 0,
#elif defined(CONFIG_ARCH_MT6797)
	.scp_req_mask_b = 1,
#endif
	.lte_mask_b = 0,
	.md_apsrc1_sel = 0,
	.md_apsrc0_sel = 0,
	.conn_mask_b = 1,
	.conn_apsrc_sel = 0,

	.ccif0_to_ap_mask_b = 1,
	.ccif0_to_md_mask_b = 1,
	.ccif1_to_ap_mask_b = 1,
	.ccif1_to_md_mask_b = 1,
	.ccifmd_md1_event_mask_b = 1,
	.ccifmd_md2_event_mask_b = 1,
	.vsync_mask_b = 0,	/* 5bit */
	.md_srcclkena_0_infra_mask_b = 0,
	.md_srcclkena_1_infra_mask_b = 0,
	.conn_srcclkena_infra_mask_b = 0,
	.md32_srcclkena_infra_mask_b = 0,
#if defined(CONFIG_ARCH_MT6755)
	.srcclkeni_infra_mask_b = 1,
#elif defined(CONFIG_ARCH_MT6797)
	.srcclkeni_infra_mask_b = 0,
#endif

	.md_apsrcreq_0_infra_mask_b = 1,
	.md_apsrcreq_1_infra_mask_b = 0,
	.conn_apsrcreq_infra_mask_b = 1,
	.md32_apsrcreq_infra_mask_b = 0,
	.md_ddr_en_0_mask_b = 1,
	.md_ddr_en_1_mask_b = 0,
	.md_vrf18_req_0_mask_b = 1,
	.md_vrf18_req_1_mask_b = 0,
#if defined(CONFIG_ARCH_MT6797)
	.md1_dvfs_req_mask = 0,
	.cpu_dvfs_req_mask = 0,
#endif
	.emi_bw_dvfs_req_mask = 1,
	.md_srcclkena_0_dvfs_req_mask_b = 0,
	.md_srcclkena_1_dvfs_req_mask_b = 0,
	.conn_srcclkena_dvfs_req_mask_b = 0,

	.dvfs_halt_mask_b = 0x1f,	/* 5bit */
	.vdec_req_mask_b = 0,
	.gce_req_mask_b = 0,
	.cpu_md_dvfs_erq_merge_mask_b = 0,
	.md1_ddr_en_dvfs_halt_mask_b = 0,
	.md2_ddr_en_dvfs_halt_mask_b = 0,
	.vsync_dvfs_halt_mask_b = 0,	/* 5bit */
	.conn_ddr_en_mask_b = 1,
	.disp_req_mask_b = 0,
	.disp1_req_mask_b = 0,
	.mfg_req_mask_b = 0,
	.c2k_ps_rccif_wake_mask_b = 1,
	.c2k_l1_rccif_wake_mask_b = 1,
	.ps_c2k_rccif_wake_mask_b = 1,
	.l1_c2k_rccif_wake_mask_b = 1,
	.sdio_on_dvfs_req_mask_b = 0,
	.emi_boost_dvfs_req_mask_b = 0,
	.cpu_md_emi_dvfs_req_prot_dis = 0,
#if defined(CONFIG_ARCH_MT6797)
	.disp_od_req_mask_b = 0,
#endif
	.spm_apsrc_req = 0,
	.spm_f26m_req = 0,
	.spm_lte_req = 0,
	.spm_infra_req = 0,
	.spm_vrf18_req = 0,
	.spm_dvfs_req = 0,
	.spm_dvfs_force_down = 1,
	.spm_ddren_req = 0,
	.cpu_md_dvfs_sop_force_on = 0,

	/* SPM_CLK_CON */
	.srclkenai_mask = 1,

	.mp0_cpu0_wfi_en = 1,
	.mp0_cpu1_wfi_en = 1,
	.mp0_cpu2_wfi_en = 1,
	.mp0_cpu3_wfi_en = 1,
	.mp1_cpu0_wfi_en = 1,
	.mp1_cpu1_wfi_en = 1,
	.mp1_cpu2_wfi_en = 1,
	.mp1_cpu3_wfi_en = 1,
#if defined(CONFIG_ARCH_MT6797)
	.mp2_cpu0_wfi_en = 1,
	.mp2_cpu1_wfi_en = 1,
#endif
#if SPM_BYPASS_SYSPWREQ
	.syspwreq_mask = 1,
#endif
};
#endif

/* please put firmware to vendor/mediatek/proprietary/hardware/spm/mtxxxx/ */
struct spm_lp_scen __spm_suspend = {
#if defined(CONFIG_FPGA_EARLY_PORTING)
	.pcmdesc = &suspend_pcm,
#endif
	.pwrctrl = &suspend_ctrl,
	.wakestatus = &suspend_info[0],
};

static void spm_suspend_pre_process(struct pwr_ctrl *pwrctrl)
{
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	unsigned int temp;
#endif

	__spm_pmic_low_iq_mode(1);

	__spm_pmic_pg_force_on();

	spm_pmic_power_mode(PMIC_PWR_SUSPEND, 0, 0);

#if defined(CONFIG_ARCH_MT6755)
#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	spm_vmd_sel_gpio_set();
#endif

	/* set PMIC WRAP table for suspend power control */
#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	pmic_read_interface_nolock(PMIC_LDO_VSRAM_PROC_EN_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_PWR_ON,
			temp | (1 << PMIC_LDO_VSRAM_PROC_EN_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_SHUTDOWN,
			temp & ~(1 << PMIC_LDO_VSRAM_PROC_EN_SHIFT));
	pmic_read_interface_nolock(PMIC_BUCK_VPROC_EN_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VPROC_PWR_ON,
			temp | (1 << PMIC_BUCK_VPROC_EN_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VPROC_SHUTDOWN,
			temp & ~(1 << PMIC_BUCK_VPROC_EN_SHIFT));
#else
	pmic_read_interface_nolock(MT6351_PMIC_RG_VSRAM_PROC_EN_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_PWR_ON,
			temp | (1 << MT6351_PMIC_RG_VSRAM_PROC_EN_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_SHUTDOWN,
			temp & ~(1 << MT6351_PMIC_RG_VSRAM_PROC_EN_SHIFT));
#endif
	/* for dpd */
	if (!(pwrctrl->pcm_flags & SPM_FLAG_DIS_DPD))
		spm_dpd_init();

#elif defined(CONFIG_ARCH_MT6757)
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	/* set PMIC WRAP table for suspend power control */
	pmic_read_interface_nolock(MT6351_PMIC_RG_VSRAM_PROC_EN_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_PWR_ON,
			temp | (1 << MT6351_PMIC_RG_VSRAM_PROC_EN_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VSRAM_SHUTDOWN,
			temp & ~(1 << MT6351_PMIC_RG_VSRAM_PROC_EN_SHIFT));
#endif

#elif defined(CONFIG_ARCH_MT6797)
	/* set PMIC WRAP table for suspend power control */
	pmic_read_interface_nolock(MT6351_PMIC_RG_VCORE_VDIFF_ENLOWIQ_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VCORE_LQ_EN,
			temp | (1 << MT6351_PMIC_RG_VCORE_VDIFF_ENLOWIQ_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VCORE_LQ_DIS,
			temp & ~(1 << MT6351_PMIC_RG_VCORE_VDIFF_ENLOWIQ_SHIFT));

	pmic_read_interface_nolock(MT6351_PMIC_RG_VCORE_VSLEEP_SEL_ADDR, &temp, 0xFFFF, 0);
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VCORE_VSLEEP_SEL_0P6V,
			temp | (3 << MT6351_PMIC_RG_VCORE_VSLEEP_SEL_SHIFT));
	mt_spm_pmic_wrap_set_cmd(PMIC_WRAP_PHASE_SUSPEND,
			IDX_SP_VCORE_VSLEEP_SEL_0P7V,
			temp & ~(3 << MT6351_PMIC_RG_VCORE_VSLEEP_SEL_SHIFT));
	if (spm_read(spm_infracfg_base + 0x4C)) {
		spm_crit2("bsi_bpi_tx_err_addr_reg = 0x%x, clk_cf_8 = 0x%x, mainpll_con0 = 0x%x\n",
			spm_read(spm_infracfg_base + 0x4C),
			spm_read(spm_cksys_base + 0xc0),
			spm_read(spm_apmixed_base + 0x220));
		BUG();
	}
#endif

#if !defined(CONFIG_FPGA_EARLY_PORTING)
	mt_spm_pmic_wrap_set_phase(PMIC_WRAP_PHASE_SUSPEND);
#endif

#if !defined(CONFIG_FPGA_EARLY_PORTING)
	/* for afcdac setting */
	clk_buf_write_afcdac();
#endif

	/* Do more low power setting when MD1/C2K/CONN off */
	if (is_md_c2k_conn_power_off()) {
		__spm_bsi_top_init_setting();

		__spm_backup_pmic_ck_pdn();
	}
}

static void spm_suspend_post_process(struct pwr_ctrl *pwrctrl)
{
	/* Do more low power setting when MD1/C2K/CONN off */
	if (is_md_c2k_conn_power_off())
		__spm_restore_pmic_ck_pdn();

#if defined(CONFIG_ARCH_MT6755)
	/* fpr dpd */
	if (!(pwrctrl->pcm_flags & SPM_FLAG_DIS_DPD))
		spm_dpd_dram_init();
#endif

	/* set PMIC WRAP table for normal power control */
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	mt_spm_pmic_wrap_set_phase(PMIC_WRAP_PHASE_NORMAL);
#endif

	__spm_pmic_low_iq_mode(0);

	__spm_pmic_pg_force_off();
}

static void spm_set_sysclk_settle(void)
{
	u32 md_settle, settle;

	/* get MD SYSCLK settle */
	spm_write(SPM_CLK_CON, spm_read(SPM_CLK_CON) | SYS_SETTLE_SEL_LSB);
	spm_write(SPM_CLK_SETTLE, 0);
	md_settle = spm_read(SPM_CLK_SETTLE);

	/* SYSCLK settle = MD SYSCLK settle but set it again for MD PDN */
	spm_write(SPM_CLK_SETTLE, SPM_SYSCLK_SETTLE - md_settle);
	settle = spm_read(SPM_CLK_SETTLE);

	spm_crit2("md_settle = %u, settle = %u\n", md_settle, settle);
}

static void spm_kick_pcm_to_run(struct pwr_ctrl *pwrctrl)
{
	/* enable PCM WDT (normal mode) to start count if needed */
#if SPM_PCMWDT_EN
	if (!pwrctrl->wdt_disable)
		__spm_set_pcm_wdt(1);
#endif

#if defined(CONFIG_ARCH_MT6797)
	if (spm_save_thermal_adc())
		pwrctrl->pcm_flags = pwrctrl->pcm_flags | 0x80000;
#endif
	__spm_kick_pcm_to_run(pwrctrl);
}

static void spm_trigger_wfi_for_sleep(struct pwr_ctrl *pwrctrl)
{
#if 0
	sync_hw_gating_value();	/* for Vcore DVFS */
#endif

	if (is_cpu_pdn(pwrctrl->pcm_flags)) {
		spm_dormant_sta = mt_cpu_dormant(CPU_SHUTDOWN_MODE /* | DORMANT_SKIP_WFI */);
		switch (spm_dormant_sta) {
		case MT_CPU_DORMANT_RESET:
			break;
		case MT_CPU_DORMANT_ABORT:
			break;
		case MT_CPU_DORMANT_BREAK:
			break;
		case MT_CPU_DORMANT_BYPASS:
			break;
		}
	} else {
		spm_dormant_sta = -1;
		spm_write(MP0_AXI_CONFIG, spm_read(MP0_AXI_CONFIG) | ACINACTM);
		spm_write(MP1_AXI_CONFIG, spm_read(MP1_AXI_CONFIG) | ACINACTM);
#if defined(CONFIG_ARCH_MT6797)
		spm_write(MP2_AXI_CONFIG, spm_read(MP2_AXI_CONFIG) | (ACINACTM + 1));
#endif
		wfi_with_sync();
		spm_write(MP0_AXI_CONFIG, spm_read(MP0_AXI_CONFIG) & ~ACINACTM);
		spm_write(MP1_AXI_CONFIG, spm_read(MP1_AXI_CONFIG) & ~ACINACTM);
#if defined(CONFIG_ARCH_MT6797)
		spm_write(MP2_AXI_CONFIG, spm_read(MP2_AXI_CONFIG) & ~(ACINACTM + 1));
#endif
	}

	if (is_infra_pdn(pwrctrl->pcm_flags))
		mtk_uart_restore();
}

static void spm_clean_after_wakeup(void)
{
	/* disable PCM WDT to stop count if needed */
#if SPM_PCMWDT_EN
		__spm_set_pcm_wdt(0);
#endif

	__spm_clean_after_wakeup();
}

static wake_reason_t spm_output_wake_reason(struct wake_status *wakesta, struct pcm_desc *pcmdesc, int vcore_status)
{
	wake_reason_t wr;
	u32 md32_flag = 0;
	u32 md32_flag2 = 0;

	wr = __spm_output_wake_reason(wakesta, pcmdesc, true);

#if 1
	memcpy(&suspend_info[log_wakesta_cnt], wakesta, sizeof(struct wake_status));
	suspend_info[log_wakesta_cnt].log_index = log_wakesta_index;

	if (10 <= log_wakesta_cnt) {
		log_wakesta_cnt = 0;
		spm_snapshot_golden_setting = 0;
	} else {
		log_wakesta_cnt++;
		log_wakesta_index++;
	}

	if (0xFFFFFFF0 <= log_wakesta_index)
		log_wakesta_index = 0;
#endif

	spm_crit2("suspend dormant state = %d, md32_flag = 0x%x, md32_flag2 = %d, vcore_status = %d\n",
		  spm_dormant_sta, md32_flag, md32_flag2, vcore_status);
	if (0 != spm_ap_mdsrc_req_cnt)
		spm_crit2("warning: spm_ap_mdsrc_req_cnt = %d, r7[ap_mdsrc_req] = 0x%x\n",
			  spm_ap_mdsrc_req_cnt, spm_read(SPM_POWER_ON_VAL1) & (1 << 17));

#if !defined(CONFIG_FPGA_EARLY_PORTING)
	if (wakesta->r12 & WAKE_SRC_R12_EINT_EVENT_B)
		mt_eint_print_status();
#endif

#if 0
	if (wakesta->debug_flag & (1 << 18)) {
		spm_crit2("MD32 suspned pmic wrapper error");
		BUG();
	}

	if (wakesta->debug_flag & (1 << 19)) {
		spm_crit2("MD32 resume pmic wrapper error");
		BUG();
	}
#endif

#ifdef CONFIG_MTK_CCCI_DEVICES
	/* if (wakesta->r13 & 0x18) { */
		spm_crit2("dump ID_DUMP_MD_SLEEP_MODE");
		exec_ccci_kern_func_by_md_id(0, ID_DUMP_MD_SLEEP_MODE, NULL, 0);
	/* } */
#endif

#if !defined(CONFIG_FPGA_EARLY_PORTING)
#ifdef CONFIG_MTK_ECCCI_DRIVER
	if (wakesta->r12 & WAKE_SRC_R12_CLDMA_EVENT_B)
		exec_ccci_kern_func_by_md_id(0, ID_GET_MD_WAKEUP_SRC, NULL, 0);
	if (wakesta->r12 & WAKE_SRC_R12_CCIF1_EVENT_B)
		exec_ccci_kern_func_by_md_id(2, ID_GET_MD_WAKEUP_SRC, NULL, 0);
#endif
#endif
	return wr;
}

/*
 * wakesrc: WAKE_SRC_XXX
 * enable : enable or disable @wakesrc
 * replace: if true, will replace the default setting
 */
int spm_set_sleep_wakesrc(u32 wakesrc, bool enable, bool replace)
{
	unsigned long flags;

	if (spm_is_wakesrc_invalid(wakesrc))
		return -EINVAL;

	spin_lock_irqsave(&__spm_lock, flags);
	if (enable) {
		if (replace)
			__spm_suspend.pwrctrl->wake_src = wakesrc;
		else
			__spm_suspend.pwrctrl->wake_src |= wakesrc;
	} else {
		if (replace)
			__spm_suspend.pwrctrl->wake_src = 0;
		else
			__spm_suspend.pwrctrl->wake_src &= ~wakesrc;
	}
	spin_unlock_irqrestore(&__spm_lock, flags);

	return 0;
}

/*
 * wakesrc: WAKE_SRC_XXX
 */
u32 spm_get_sleep_wakesrc(void)
{
	return __spm_suspend.pwrctrl->wake_src;
}

#if SPM_AEE_RR_REC
void spm_suspend_aee_init(void)
{
	aee_rr_rec_spm_suspend_val(0);
}
#endif

#if !defined(CONFIG_FPGA_EARLY_PORTING)
#ifdef CONFIG_MTK_PMIC
/* #include <cust_pmic.h> */
#ifndef DISABLE_DLPT_FEATURE
/* extern int get_dlpt_imix_spm(void); */
int __attribute__((weak)) get_dlpt_imix_spm(void)
{
	return 0;
}
#endif
#endif
#endif

#ifdef CONFIG_SONY_S1_SUPPORT
#include <linux/of.h>
#include <mt-plat/mt_io.h>

extern volatile unsigned long magic_val;
extern void __iomem *magic_base;

void write_magic(volatile unsigned long magic_write, int log_option)
{
	if (magic_base != 0)
	{
		writel(magic_write, IOMEM(magic_base + 0x834));		

		if( magic_write  == readl(IOMEM(magic_base + 0x834)))
		{
			if(log_option == 1)
				spm_crit2("%lx write_magic success !!\n", magic_write);
			else
				printk("%lx write_magic success !!\n", magic_write);
		}
		else
		{
			if(log_option == 1)
				spm_crit2("%lx write_magic failed !!\n", magic_write);
			else
				printk("%lx write_magic failed !!\n", magic_write);
		}
	}
	else
	{
		if(log_option == 1)
			spm_crit2("magic_base invalid !!\n");
		else
			printk("magic_base invalid !!\n");
	}
}

EXPORT_SYMBOL(write_magic);
#endif

wake_reason_t spm_go_to_sleep(u32 spm_flags, u32 spm_data)
{
	u32 sec = 2;
	/* struct wake_status wakesta; */
	unsigned long flags;
	struct mtk_irq_mask mask;
#ifdef CONFIG_MTK_WD_KICKER
	struct wd_api *wd_api;
	int wd_ret;
#endif
	static wake_reason_t last_wr = WR_NONE;
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	struct pcm_desc *pcmdesc = NULL;
#else
	struct pcm_desc *pcmdesc;
#endif
	struct pwr_ctrl *pwrctrl;
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	int vcore_status;
#endif
	u32 cpu = smp_processor_id();
	int pcm_index;

#if !defined(CONFIG_FPGA_EARLY_PORTING)
#ifdef CONFIG_MTK_PMIC
#ifndef DISABLE_DLPT_FEATURE
	get_dlpt_imix_spm();
#endif
#endif
#endif

#if SPM_AEE_RR_REC
	spm_suspend_aee_init();
	aee_rr_rec_spm_suspend_val(SPM_SUSPEND_ENTER);
#endif

	/* TODO: lpddr support */
	/* pcm_index = DYNA_LOAD_PCM_SUSPEND + cpu / 4 + !!is_lpddr4() * 2; */
	pcm_index = DYNA_LOAD_PCM_SUSPEND + cpu / 4;

	if (dyna_load_pcm[pcm_index].ready)
		pcmdesc = &(dyna_load_pcm[pcm_index].desc);
	else
		BUG();
	spm_crit2("Online CPU is %d, suspend FW ver. is %s\n", cpu, pcmdesc->version);

#if defined(CONFIG_FPGA_EARLY_PORTING)
	pcmdesc = __spm_suspend.pcmdesc;
#endif
	pwrctrl = __spm_suspend.pwrctrl;

	update_pwrctrl_pcm_flags(&spm_flags);
	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);
	set_pwrctrl_pcm_data(pwrctrl, spm_data);

#if SPM_PWAKE_EN
	sec = _spm_get_wake_period(-1 /* FIXME */, last_wr);
#endif
	pwrctrl->timer_val = sec * 32768;

#ifdef CONFIG_MTK_WD_KICKER
	wd_ret = get_wd_api(&wd_api);
	if (!wd_ret) {
		wd_api->wd_spmwdt_mode_config(WD_REQ_EN, WD_REQ_RST_MODE);
		wd_api->wd_suspend_notify();
	} else
		spm_crit2("FAILED TO GET WD API\n");
#endif

	spm_suspend_pre_process(pwrctrl);

#if 0
	/* snapshot golden setting */
	{
		if (!is_already_snap_shot)
			snapshot_golden_setting(__func__, 0);
	}
#endif

	spin_lock_irqsave(&__spm_lock, flags);

	mt_irq_mask_all(&mask);
	mt_irq_unmask_for_sleep(SPM_IRQ0_ID);
#if defined(CONFIG_ARCH_MT6797)
	unmask_edge_trig_irqs_for_cirq();
#endif

#if defined(CONFIG_MTK_SYS_CIRQ)
	mt_cirq_clone_gic();
	mt_cirq_enable();
#endif

	spm_set_sysclk_settle();

	spm_crit2("sec = %u, wakesrc = 0x%x (%u)(%u)\n",
		  sec, pwrctrl->wake_src, is_cpu_pdn(pwrctrl->pcm_flags),
		  is_infra_pdn(pwrctrl->pcm_flags));
	if (request_uart_to_sleep()) {
		last_wr = WR_UART_BUSY;
		goto RESTORE_IRQ;
	}
	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_check_md_pdn_power_control(pwrctrl);

#if !defined(CONFIG_FPGA_EARLY_PORTING)
#if defined(CONFIG_ARCH_MT6755) || defined(CONFIG_ARCH_MT6757)
	__spm_sync_vcore_dvfs_power_control(pwrctrl, __spm_vcore_dvfs.pwrctrl);
#endif

	vcore_status = vcorefs_get_curr_ddr();
#endif

#if defined(CONFIG_ARCH_MT6797)
	pwrctrl->pcm_flags &= ~SPM_FLAG_RUN_COMMON_SCENARIO;
	pwrctrl->pcm_flags &= ~SPM_FLAG_DIS_VCORE_DVS;
	pwrctrl->pcm_flags |= SPM_FLAG_DIS_VCORE_DFS;
#endif

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	spm_kick_pcm_to_run(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(SPM_SUSPEND_ENTER_WFI);
#endif
	spm_trigger_wfi_for_sleep(pwrctrl);
#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(SPM_SUSPEND_LEAVE_WFI);
#endif

#ifdef CONFIG_SONY_S1_SUPPORT
	write_magic(magic_val, 1);
#endif

	/* record last wakesta */
	/* __spm_get_wakeup_status(&wakesta); */
	__spm_get_wakeup_status(&spm_wakesta);

#if defined(CONFIG_ARCH_MT6797)
	spm_wakesta.timer_out += (spm_read(SPM_SCP_MAILBOX) - 1) *
		(1 << SPM_THERMAL_TIMER);
#endif

	spm_clean_after_wakeup();

	request_uart_to_wakeup();

	/* record last wakesta */
	/* last_wr = spm_output_wake_reason(&wakesta, pcmdesc); */
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	last_wr = spm_output_wake_reason(&spm_wakesta, pcmdesc, vcore_status);
#else
	last_wr = spm_output_wake_reason(&spm_wakesta, pcmdesc, 0);
#endif
RESTORE_IRQ:
#if defined(CONFIG_MTK_SYS_CIRQ)
	mt_cirq_flush();
	mt_cirq_disable();
#endif

	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);

	spm_suspend_post_process(pwrctrl);

#ifdef CONFIG_MTK_WD_KICKER
	if (!wd_ret) {
		if (!pwrctrl->wdt_disable)
			wd_api->wd_resume_notify();
		else
			spm_crit2("pwrctrl->wdt_disable %d\n", pwrctrl->wdt_disable);
		wd_api->wd_spmwdt_mode_config(WD_REQ_DIS, WD_REQ_RST_MODE);
	}
#endif

#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(0);
#endif

#if defined(CONFIG_ARCH_MT6797)
	/* Re-kick VCORE DVFS */
	if (last_wr != WR_UART_BUSY)
		if (is_vcorefs_feature_enable()) {
			__spm_backup_vcore_dvfs_dram_shuffle();
			__spm_kick_im_to_fetch(pcmdesc);
			__spm_init_pcm_register();
			__spm_init_event_vector(pcmdesc);
			__spm_check_md_pdn_power_control(pwrctrl);
			__spm_sync_vcore_dvfs_power_control(pwrctrl, __spm_vcore_dvfs.pwrctrl);
			pwrctrl->pcm_flags |= SPM_FLAG_RUN_COMMON_SCENARIO;
			pwrctrl->pcm_flags &= ~SPM_FLAG_DIS_VCORE_DVS;
			pwrctrl->pcm_flags |= SPM_FLAG_DIS_VCORE_DFS;
			__spm_set_power_control(pwrctrl);
			__spm_set_wakeup_event(pwrctrl);
			__spm_set_vcorefs_wakeup_event(__spm_vcore_dvfs.pwrctrl);
			spm_write(PCM_CON1, SPM_REGWR_CFG_KEY | (spm_read(PCM_CON1) & ~PCM_TIMER_EN_LSB));
			__spm_kick_pcm_to_run(pwrctrl);
			spm_crit2("R15: 0x%x\n", spm_read(PCM_REG15_DATA));

#if SPM_AEE_RR_REC
			aee_rr_rec_spm_common_scenario_val(SPM_COMMON_SCENARIO_SUSPEND);
#endif
		}
#endif

#ifdef CONFIG_MTK_USB2JTAG_SUPPORT
	if (usb2jtag_mode())
		mt_usb2jtag_resume();
#endif


	return last_wr;
}

bool spm_is_md_sleep(void)
{
	return !((spm_read(PCM_REG13_DATA) & R13_MD1_SRCCLKENA) |
		 (spm_read(PCM_REG13_DATA) & R13_MD2_SRCCLKENA));
}

bool spm_is_md1_sleep(void)
{
	return !(spm_read(PCM_REG13_DATA) & R13_MD1_SRCCLKENA);
}

bool spm_is_md2_sleep(void)
{
	return !(spm_read(PCM_REG13_DATA) & R13_MD2_SRCCLKENA);
}

bool spm_is_conn_sleep(void)
{
	return !(spm_read(PCM_REG13_DATA) & R13_CONN_SRCCLKENA);
}

void spm_set_wakeup_src_check(void)
{
	/* clean wakeup event raw status */
	spm_write(SPM_WAKEUP_EVENT_MASK, 0xFFFFFFFF);

	/* set wakeup event */
	spm_write(SPM_WAKEUP_EVENT_MASK, ~WAKE_SRC_FOR_SUSPEND);
}

bool spm_check_wakeup_src(void)
{
	u32 wakeup_src;

	/* check wanek event raw status */
	wakeup_src = spm_read(SPM_WAKEUP_STA);

	if (wakeup_src) {
		spm_crit2("WARNING: spm_check_wakeup_src = 0x%x", wakeup_src);
		return 1;
	} else
		return 0;
}

void spm_poweron_config_set(void)
{
	unsigned long flags;

	spin_lock_irqsave(&__spm_lock, flags);
	/* enable register control */
	spm_write(POWERON_CONFIG_EN, (SPM_PROJECT_CODE << 16) | BCLK_CG_EN_LSB);
	spin_unlock_irqrestore(&__spm_lock, flags);
}

void spm_md32_sram_con(u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&__spm_lock, flags);
	/* enable register control */
	spm_write(SCP_SRAM_CON, value);
	spin_unlock_irqrestore(&__spm_lock, flags);
}

#if 0
#define hw_spin_lock_for_ddrdfs()           \
do {                                        \
	spm_write(0xF0050090, 0x8000);          \
} while (!(spm_read(0xF0050090) & 0x8000))

#define hw_spin_unlock_for_ddrdfs()         \
	spm_write(0xF0050090, 0x8000)
#else
#define hw_spin_lock_for_ddrdfs()
#define hw_spin_unlock_for_ddrdfs()
#endif

void spm_ap_mdsrc_req(u8 set)
{
	unsigned long flags;
	u32 i = 0;
	u32 md_sleep = 0;

	if (set) {
		spin_lock_irqsave(&__spm_lock, flags);

		if (spm_ap_mdsrc_req_cnt < 0) {
			spm_crit2("warning: set = %d, spm_ap_mdsrc_req_cnt = %d\n", set,
				  spm_ap_mdsrc_req_cnt);
			/* goto AP_MDSRC_REC_CNT_ERR; */
			spin_unlock_irqrestore(&__spm_lock, flags);
		} else {
			spm_ap_mdsrc_req_cnt++;

			hw_spin_lock_for_ddrdfs();
			spm_write(AP_MDSRC_REQ, spm_read(AP_MDSRC_REQ) | AP_MD1SRC_REQ_LSB);
			hw_spin_unlock_for_ddrdfs();

			spin_unlock_irqrestore(&__spm_lock, flags);

			/* if md_apsrc_req = 1'b0, wait 26M settling time (3ms) */
			if (0 == (spm_read(PCM_REG13_DATA) & R13_MD1_APSRC_REQ)) {
				md_sleep = 1;
				mdelay(3);
			}

			/* Check ap_mdsrc_ack = 1'b1 */
			while (0 == (spm_read(AP_MDSRC_REQ) & AP_MD1SRC_ACK_LSB)) {
				if (10 > i++) {
					mdelay(1);
				} else {
					spm_crit2
					    ("WARNING: MD SLEEP = %d, spm_ap_mdsrc_req CAN NOT polling AP_MD1SRC_ACK\n",
					     md_sleep);
					/* goto AP_MDSRC_REC_CNT_ERR; */
					break;
				}
			}
		}
	} else {
		spin_lock_irqsave(&__spm_lock, flags);

		spm_ap_mdsrc_req_cnt--;

		if (spm_ap_mdsrc_req_cnt < 0) {
			spm_crit2("warning: set = %d, spm_ap_mdsrc_req_cnt = %d\n", set,
				  spm_ap_mdsrc_req_cnt);
			/* goto AP_MDSRC_REC_CNT_ERR; */
		} else {
			if (0 == spm_ap_mdsrc_req_cnt) {
				hw_spin_lock_for_ddrdfs();
				spm_write(AP_MDSRC_REQ, spm_read(AP_MDSRC_REQ) & ~AP_MD1SRC_REQ_LSB);
				hw_spin_unlock_for_ddrdfs();
			}
		}

		spin_unlock_irqrestore(&__spm_lock, flags);
	}

/* AP_MDSRC_REC_CNT_ERR: */
/* spin_unlock_irqrestore(&__spm_lock, flags); */
}

bool spm_set_suspned_pcm_init_flag(u32 *suspend_flags)
{
	return true;
}

void spm_output_sleep_option(void)
{
	spm_notice("PWAKE_EN:%d, PCMWDT_EN:%d, BYPASS_SYSPWREQ:%d\n",
		   SPM_PWAKE_EN, SPM_PCMWDT_EN, SPM_BYPASS_SYSPWREQ);
}

uint32_t get_suspend_debug_regs(uint32_t index)
{
	uint32_t value = 0;

	switch (index) {
	case 0:
#if defined(CONFIG_ARCH_MT6755)
		value = 5;
#elif defined(CONFIG_ARCH_MT6797)
		value = 4;
#elif defined(CONFIG_ARCH_MT6797)
		value = 6;
#endif
		spm_crit("SPM Suspend debug regs count = 0x%.8x\n",  value);
	break;
	case 1:
		value = spm_read(PCM_WDT_LATCH_0);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
	case 2:
		value = spm_read(PCM_WDT_LATCH_1);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
	case 3:
		value = spm_read(PCM_WDT_LATCH_2);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
	case 4:
		value = spm_read(PCM_WDT_LATCH_3);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
	case 5:
		value = spm_read(DRAMC_DBG_LATCH);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
#if defined(CONFIG_ARCH_MT6797)
	case 6:
		value = spm_read(PCM_WDT_LATCH_4);
		spm_crit("SPM Suspend debug regs(0x%x) = 0x%.8x\n", index, value);
	break;
#endif
	}

	return value;
}
EXPORT_SYMBOL(get_suspend_debug_regs);

/* record last wakesta */
u32 spm_get_last_wakeup_src(void)
{
	return spm_wakesta.r12;
}

u32 spm_get_last_wakeup_misc(void)
{
	return spm_wakesta.wake_misc;
}
MODULE_DESCRIPTION("SPM-Sleep Driver v0.1");
