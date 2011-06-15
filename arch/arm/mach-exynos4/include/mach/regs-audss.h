/* arch/arm/mach-exynos4/include/mach/regs-audss.h
 *
 * Copyright (c) 2011 Samsung Electronics
 *		http://www.samsung.com
 *
 * Exynos4 Audio SubSystem clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_REGS_AUDSS_H
#define __PLAT_REGS_AUDSS_H __FILE__

#define S5P_AUDSSREG(x)		(S5P_VA_AUDSS + (x))

#define S5P_CLKGATE_AUDSS	S5P_AUDSSREG(0x8)

#define PCM_EXTCLK0		16934400

/* IP Clock Gate 0 Registers */
#define S5P_AUDSS_CLKGATE_PCMSPECIAL	(1<<5)

#endif /* _PLAT_REGS_AUDSS_H */
