/*
 *	Machine specific setup for generic
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/apic.h>
#include <asm/voyager.h>
#include <asm/e820.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/timer.h>
#include <asm/cpu.h>

static int __init voyager_intr_init(void)
{
#ifdef CONFIG_SMP
	voyager_smp_intr_init();
#endif

	/* need to do the irq2 cascade setup */
	return 0;
}

static void voyager_disable_tsc(void)
{
	/* Voyagers run their CPUs from independent clocks, so disable
	 * the TSC code because we can't sync them */
	setup_clear_cpu_cap(X86_FEATURE_TSC);
}

int __init voyager_pre_time_init(void)
{
	voyager_disable_tsc();
	return 0;
}

static struct irqaction irq0 = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING | IRQF_IRQPOLL | IRQF_TIMER,
	.mask = CPU_MASK_NONE,
	.name = "timer"
};

static int __init voyager_time_init(void)
{
	irq0.mask = cpumask_of_cpu(safe_smp_processor_id());
	setup_irq(0, &irq0);

	/* return 1 to not do standard timer setup */
	return 1;
}

/* Hook for machine specific memory setup. */

static char *__init voyager_memory_setup(void)
{
	char *who;

	who = "NOT VOYAGER";

	if (voyager_level == 5) {
		__u32 addr, length;
		int i;

		who = "Voyager-SUS";

		e820.nr_map = 0;
		for (i = 0; voyager_memory_detect(i, &addr, &length); i++) {
			e820_add_region(addr, length, E820_RAM);
		}
		return who;
	} else if (voyager_level == 4) {
		__u32 tom;
		__u16 catbase = inb(VOYAGER_SSPB_RELOCATION_PORT) << 8;
		/* select the DINO config space */
		outb(VOYAGER_DINO, VOYAGER_CAT_CONFIG_PORT);
		/* Read DINO top of memory register */
		tom = ((inb(catbase + 0x4) & 0xf0) << 16)
		    + ((inb(catbase + 0x5) & 0x7f) << 24);

		if (inb(catbase) != VOYAGER_DINO) {
			printk(KERN_ERR
			       "Voyager: Failed to get DINO for L4, setting tom to EXT_MEM_K\n");
			tom = (boot_params.screen_info.ext_mem_k) << 10;
		}
		who = "Voyager-TOM";
		e820_add_region(0, 0x9f000, E820_RAM);
		/* map from 1M to top of memory */
		e820_add_region(1 * 1024 * 1024, tom - 1 * 1024 * 1024,
				  E820_RAM);
		/* FIXME: Should check the ASICs to see if I need to
		 * take out the 8M window.  Just do it at the moment
		 * */
		e820_add_region(8 * 1024 * 1024, 8 * 1024 * 1024,
				  E820_RESERVED);
		return who;
	}

	return NULL;
}

static struct x86_quirks voyager_x86_quirks __initdata = {
	.arch_time_init		= voyager_time_init,
	.arch_intr_init		= voyager_intr_init,
	.arch_pre_time_init	= voyager_pre_time_init,
	.arch_memory_setup	= voyager_memory_setup,
};

void __init voyager_early_detect(void)
{
	if (!is_voyager())
		return;

	voyager_detect();

	skip_ioapic_setup = 1;
	voyager_disable_tsc();
	disable_APIC();
	voyager_smp_detect(&voyager_x86_quirks);
	x86_quirks = &voyager_x86_quirks;

}
