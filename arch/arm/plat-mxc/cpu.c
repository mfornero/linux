#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>

unsigned int __mxc_cpu_type;
EXPORT_SYMBOL(__mxc_cpu_type);

void mxc_set_cpu_type(unsigned int type)
{
	__mxc_cpu_type = type;
}

void imx_print_silicon_rev(const char *cpu, int srev)
{
	if (srev == IMX_CHIP_REVISION_UNKNOWN)
		pr_info("CPU identified as %s, unknown revision\n", cpu);
	else
		pr_info("CPU identified as %s, silicon rev %d.%d\n",
				cpu, (srev >> 4) & 0xf, srev & 0xf);
}

#ifdef CONFIG_IPIPE
void ipipe_mach_allow_hwtimer_uaccess(unsigned long aips1, unsigned long aips2)
{
	volatile unsigned long aips_reg;

	if (!cpu_is_mx27()) {
		/*
		 * S/W workaround: Clear the off platform peripheral modules
		 * Supervisor Protect bit for SDMA to access them.
		 */
		__raw_writel(0x0, aips1 + 0x40);
		__raw_writel(0x0, aips1 + 0x44);
		__raw_writel(0x0, aips1 + 0x48);
		__raw_writel(0x0, aips1 + 0x4C);
		aips_reg = __raw_readl(aips1 + 0x50);
		aips_reg &= 0x00FFFFFF;
		__raw_writel(aips_reg, aips1 + 0x50);

		__raw_writel(0x0, aips2 + 0x40);
		__raw_writel(0x0, aips2 + 0x44);
		__raw_writel(0x0, aips2 + 0x48);
		__raw_writel(0x0, aips2 + 0x4C);
		aips_reg = __raw_readl(aips2 + 0x50);
		aips_reg &= 0x00FFFFFF;
		__raw_writel(aips_reg, aips2 + 0x50);
	} else {
		aips_reg = __raw_readl(aips1 + 8);
		aips_reg &= ~(1 << aips2);
		__raw_writel(aips_reg, aips1 + 8);
	}
}
#endif /* CONFIG_IPIPE */

void __init imx_set_aips(void __iomem *base)
{
	unsigned int reg;
/*
 * Set all MPROTx to be non-bufferable, trusted for R/W,
 * not forced to user-mode.
 */
	__raw_writel(0x77777777, base + 0x0);
	__raw_writel(0x77777777, base + 0x4);

/*
 * Set all OPACRx to be non-bufferable, to not require
 * supervisor privilege level for access, allow for
 * write access and untrusted master access.
 */
	__raw_writel(0x0, base + 0x40);
	__raw_writel(0x0, base + 0x44);
	__raw_writel(0x0, base + 0x48);
	__raw_writel(0x0, base + 0x4C);
	reg = __raw_readl(base + 0x50) & 0x00FFFFFF;
	__raw_writel(reg, base + 0x50);
}
