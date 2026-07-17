// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Rockchip SoCs
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/clocksource.h>
#include <linux/smp.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "core.h"
#include "pm.h"

#define RK3288_TIMER6_7_PHYS 0xff810000

static void __init rockchip_timer_init(void)
{
	if (of_machine_is_compatible("rockchip,rk3288")) {
		void __iomem *reg_base;

		/*
		 * Most/all uboot versions for rk3288 don't enable timer7
		 * which is needed for the architected timer to work.
		 * So make sure it is running during early boot.
		 */
		reg_base = ioremap(RK3288_TIMER6_7_PHYS, SZ_16K);
		if (reg_base) {
			writel(0, reg_base + 0x30);
			writel(0xffffffff, reg_base + 0x20);
			writel(0xffffffff, reg_base + 0x24);
			writel(1, reg_base + 0x30);
			dsb();
			iounmap(reg_base);
		} else {
			pr_err("rockchip: could not map timer7 registers\n");
		}
	}

	of_clk_init(NULL);
	timer_probe();
}

#ifdef CONFIG_SMP
#define SCU_CTRL		0x00
#define SCU_CTRL_ENABLE		BIT(0)
#define SCU_CTRL_NCPU_SHIFT	4
#define SCU_CTRL_NCPU_MASK	GENMASK(5, 4)

#define ACTLR_SMP		BIT(6)
#define NSACR_NS_SMP		BIT(18)

static phys_addr_t __init rockchip_read_periphbase(void)
{
	u32 periphbase;

	asm volatile("mrc p15, 4, %0, c15, c0, 0" : "=r"(periphbase));
	return (phys_addr_t)(periphbase & 0xffffe000);
}

static void __init rockchip_smp_check_coherency(void)
{
	u32 actlr, nsacr;
	phys_addr_t periphbase;
	void __iomem *scu_base;
	u32 scu_ctrl;
	int ncpu;
	unsigned long flags;

	/* Read ACTLR and NSACR for diagnostics */
	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r"(actlr));
	asm volatile("mrc p15, 0, %0, c1, c1, 2" : "=r"(nsacr));

	pr_info("rk3506: ACTLR=0x%08x SMP=%s  NSACR=0x%08x NS_SMP=%s\n",
		actlr, (actlr & ACTLR_SMP) ? "YES" : "NO",
		nsacr, (nsacr & NSACR_NS_SMP) ? "YES" : "NO");

	if (!(actlr & ACTLR_SMP)) {
		if (!(nsacr & NSACR_NS_SMP))
			pr_warn("rk3506: NSACR.NS_SMP not set."
				" OP-TEE must set NSACR.NS_SMP=1"
				" for each CPU else SMP coherency"
				" is broken.\n");
		else
			pr_warn("rk3506: NSACR set but ACTLR.SMP"
				" still 0 - unexpected.\n");
	} else {
		pr_info("rk3506: ACTLR.SMP correctly set.\n");
	}

	/* Check and enable SCU via PERIPHBASE */
	periphbase = rockchip_read_periphbase();
	if (periphbase) {
		scu_base = ioremap(periphbase, SZ_4K);
		if (scu_base) {
			scu_ctrl = readl_relaxed(scu_base + SCU_CTRL);
			ncpu = (scu_ctrl & SCU_CTRL_NCPU_MASK)
			       >> SCU_CTRL_NCPU_SHIFT;
			pr_info("rk3506: SCU@0x%pa ctrl=0x%08x en=%d"
				" ncpu=%d\n", &periphbase, scu_ctrl,
				!!(scu_ctrl & SCU_CTRL_ENABLE),
				ncpu ? ncpu : 1);

			if (!(scu_ctrl & SCU_CTRL_ENABLE)) {
				pr_warn("rk3506: SCU disabled,"
					" enabling.\n");
				local_irq_save(flags);
				writel_relaxed(scu_ctrl | SCU_CTRL_ENABLE,
					       scu_base + SCU_CTRL);
				mb();
				local_irq_restore(flags);
			}
			iounmap(scu_base);
		}
	}
}

static void __init rockchip_dt_init(void)
{
	rockchip_smp_check_coherency();
	rockchip_suspend_init();
}
#else
static void __init rockchip_dt_init(void)
{
	rockchip_suspend_init();
}
#endif

static const char * const rockchip_board_dt_compat[] = {
	"rockchip,rk2928",
	"rockchip,rk3066a",
	"rockchip,rk3066b",
	"rockchip,rk3188",
	"rockchip,rk3228",
	"rockchip,rk3288",
	"rockchip,rk3506",
	"rockchip,rv1108",
	NULL,
};

DT_MACHINE_START(ROCKCHIP_DT, "Rockchip (Device Tree)")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_time	= rockchip_timer_init,
	.dt_compat	= rockchip_board_dt_compat,
	.init_machine	= rockchip_dt_init,
MACHINE_END
