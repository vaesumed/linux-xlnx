/*
 * arch/arm/mach-pxa/include/mach/gpio.h
 *
 * PXA GPIO wrappers for arch-neutral GPIO calls
 *
 * Written by Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_PXA_GPIO_H
#define __ASM_ARCH_PXA_GPIO_H

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <asm-generic/gpio.h>

#define GPIO_REGS_VIRT	io_p2v(0x40E00000)
#define GPIO_REG(x)	(*((volatile u32 *)(GPIO_REGS_VIRT + (x))))

/* GPIO Pin Level Registers */
#define GPLR0		GPIO_REG(0x0000)
#define GPLR1		GPIO_REG(0x0004)
#define GPLR2		GPIO_REG(0x0008)
#define GPLR3		GPIO_REG(0x0100)

/* GPIO Pin Direction Registers */
#define GPDR0		GPIO_REG(0x000C)
#define GPDR1		GPIO_REG(0x0010)
#define GPDR2		GPIO_REG(0x0014)
#define GPDR3		GPIO_REG(0x010C)

/* GPIO Pin Output Set Registers */
#define GPSR0		GPIO_REG(0x0018)
#define GPSR1		GPIO_REG(0x001C)
#define GPSR2		GPIO_REG(0x0020)
#define GPSR3		GPIO_REG(0x0118)

/* GPIO Pin Output Clear Registers */
#define GPCR0		GPIO_REG(0x0024)
#define GPCR1		GPIO_REG(0x0028)
#define GPCR2		GPIO_REG(0x002C)
#define GPCR3		GPIO_REG(0x0124)

/* GPIO Rising Edge Detect Registers */
#define GRER0		GPIO_REG(0x0030)
#define GRER1		GPIO_REG(0x0034)
#define GRER2		GPIO_REG(0x0038)
#define GRER3		GPIO_REG(0x0130)

/* GPIO Falling Edge Detect Registers */
#define GFER0		GPIO_REG(0x003C)
#define GFER1		GPIO_REG(0x0040)
#define GFER2		GPIO_REG(0x0044)
#define GFER3		GPIO_REG(0x013C)

/* GPIO Edge Detect Status Registers */
#define GEDR0		GPIO_REG(0x0048)
#define GEDR1		GPIO_REG(0x004C)
#define GEDR2		GPIO_REG(0x0050)
#define GEDR3		GPIO_REG(0x0148)

/* GPIO Alternate Function Select Registers */
#define GAFR0_L		GPIO_REG(0x0054)
#define GAFR0_U		GPIO_REG(0x0058)
#define GAFR1_L		GPIO_REG(0x005C)
#define GAFR1_U		GPIO_REG(0x0060)
#define GAFR2_L		GPIO_REG(0x0064)
#define GAFR2_U		GPIO_REG(0x0068)
#define GAFR3_L		GPIO_REG(0x006C)
#define GAFR3_U		GPIO_REG(0x0070)

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

#define GPLR(x)		GPIO_REG((((x) < 96) ? 0x000 : 0x100) + (((x) & 0x60) >> 3))
#define GPDR(x)		GPIO_REG((((x) < 96) ? 0x00c : 0x10c) + (((x) & 0x60) >> 3))
#define GPSR(x)		GPIO_REG((((x) < 96) ? 0x018 : 0x118) + (((x) & 0x60) >> 3))
#define GPCR(x)		GPIO_REG((((x) < 96) ? 0x024 : 0x124) + (((x) & 0x60) >> 3))
#define GRER(x)		GPIO_REG((((x) < 96) ? 0x030 : 0x130) + (((x) & 0x60) >> 3))
#define GFER(x)		GPIO_REG((((x) < 96) ? 0x03c : 0x13c) + (((x) & 0x60) >> 3))
#define GEDR(x)		GPIO_REG((((x) < 96) ? 0x048 : 0x148) + (((x) & 0x60) >> 3))
#define GAFR(x)		GPIO_REG(0x054 + (((x) & 0x70) >> 2))

/* NOTE: some PXAs have fewer on-chip GPIOs (like PXA255, with 85).
 * Those cases currently cause holes in the GPIO number space.
 */
#define NR_BUILTIN_GPIO 128

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO))
		return GPLR(gpio) & GPIO_bit(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO)) {
		if (value)
			GPSR(gpio) = GPIO_bit(gpio);
		else
			GPCR(gpio) = GPIO_bit(gpio);
	} else {
		__gpio_set_value(gpio, value);
	}
}

#define gpio_cansleep		__gpio_cansleep
#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)
#define irq_to_gpio(irq)	IRQ_TO_GPIO(irq)


#ifdef CONFIG_CPU_PXA26x
/* GPIO86/87/88/89 on PXA26x have their direction bits in GPDR2 inverted,
 * as well as their Alternate Function value being '1' for GPIO in GAFRx.
 */
static inline int __gpio_is_inverted(unsigned gpio)
{
	return cpu_is_pxa25x() && gpio > 85;
}
#else
static inline int __gpio_is_inverted(unsigned gpio) { return 0; }
#endif

/*
 * On PXA25x and PXA27x, GAFRx and GPDRx together decide the alternate
 * function of a GPIO, and GPDRx cannot be altered once configured. It
 * is attributed as "occupied" here (I know this terminology isn't
 * accurate, you are welcome to propose a better one :-)
 */
static inline int __gpio_is_occupied(unsigned gpio)
{
	if (cpu_is_pxa27x() || cpu_is_pxa25x()) {
		int af = (GAFR(gpio) >> ((gpio & 0xf) * 2)) & 0x3;
		int dir = GPDR(gpio) & GPIO_bit(gpio);

		if (__gpio_is_inverted(gpio))
			return af != 1 || dir == 0;
		else
			return af != 0 || dir != 0;
	} else
		return GPDR(gpio) & GPIO_bit(gpio);
}

typedef int (*set_wake_t)(unsigned int irq, unsigned int on);

extern void pxa_init_gpio(int mux_irq, int start, int end, set_wake_t fn);
#endif
