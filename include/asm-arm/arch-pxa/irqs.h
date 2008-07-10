/*
 *  linux/include/asm-arm/arch-pxa/irqs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#define PXA_IRQ(x)	(x)

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#define IRQ_SSP3	PXA_IRQ(0)	/* SSP3 service request */
#define IRQ_MSL		PXA_IRQ(1)	/* MSL Interface interrupt */
#define IRQ_USBH2	PXA_IRQ(2)	/* USB Host interrupt 1 (OHCI) */
#define IRQ_USBH1	PXA_IRQ(3)	/* USB Host interrupt 2 (non-OHCI) */
#define IRQ_KEYPAD	PXA_IRQ(4)	/* Key pad controller */
#define IRQ_MEMSTK	PXA_IRQ(5)	/* Memory Stick interrupt */
#define IRQ_PWRI2C	PXA_IRQ(6)	/* Power I2C interrupt */
#endif

#define IRQ_HWUART	PXA_IRQ(7)	/* HWUART Transmit/Receive/Error (PXA26x) */
#define IRQ_OST_4_11	PXA_IRQ(7)	/* OS timer 4-11 matches (PXA27x) */
#define	IRQ_GPIO0	PXA_IRQ(8)	/* GPIO0 Edge Detect */
#define	IRQ_GPIO1	PXA_IRQ(9)	/* GPIO1 Edge Detect */
#define	IRQ_GPIO_2_x	PXA_IRQ(10)	/* GPIO[2-x] Edge Detect */
#define	IRQ_USB		PXA_IRQ(11)	/* USB Service */
#define	IRQ_PMU		PXA_IRQ(12)	/* Performance Monitoring Unit */
#define	IRQ_I2S		PXA_IRQ(13)	/* I2S Interrupt */
#define	IRQ_AC97	PXA_IRQ(14)	/* AC97 Interrupt */
#define IRQ_ASSP	PXA_IRQ(15)	/* Audio SSP Service Request (PXA25x) */
#define IRQ_USIM	PXA_IRQ(15)     /* Smart Card interface interrupt (PXA27x) */
#define IRQ_NSSP	PXA_IRQ(16)	/* Network SSP Service Request (PXA25x) */
#define IRQ_SSP2	PXA_IRQ(16)	/* SSP2 interrupt (PXA27x) */
#define	IRQ_LCD		PXA_IRQ(17)	/* LCD Controller Service Request */
#define	IRQ_I2C		PXA_IRQ(18)	/* I2C Service Request */
#define	IRQ_ICP		PXA_IRQ(19)	/* ICP Transmit/Receive/Error */
#define	IRQ_STUART	PXA_IRQ(20)	/* STUART Transmit/Receive/Error */
#define	IRQ_BTUART	PXA_IRQ(21)	/* BTUART Transmit/Receive/Error */
#define	IRQ_FFUART	PXA_IRQ(22)	/* FFUART Transmit/Receive/Error*/
#define	IRQ_MMC		PXA_IRQ(23)	/* MMC Status/Error Detection */
#define	IRQ_SSP		PXA_IRQ(24)	/* SSP Service Request */
#define	IRQ_DMA 	PXA_IRQ(25)	/* DMA Channel Service Request */
#define	IRQ_OST0 	PXA_IRQ(26)	/* OS Timer match 0 */
#define	IRQ_OST1 	PXA_IRQ(27)	/* OS Timer match 1 */
#define	IRQ_OST2 	PXA_IRQ(28)	/* OS Timer match 2 */
#define	IRQ_OST3 	PXA_IRQ(29)	/* OS Timer match 3 */
#define	IRQ_RTC1Hz	PXA_IRQ(30)	/* RTC HZ Clock Tick */
#define	IRQ_RTCAlrm	PXA_IRQ(31)	/* RTC Alarm */

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#define IRQ_TPM		PXA_IRQ(32)	/* TPM interrupt */
#define IRQ_CAMERA	PXA_IRQ(33)	/* Camera Interface */
#endif

#ifdef CONFIG_PXA3xx
#define IRQ_SSP4	PXA_IRQ(13)	/* SSP4 service request */
#define IRQ_CIR		PXA_IRQ(34)	/* Consumer IR */
#define IRQ_TSI		PXA_IRQ(36)	/* Touch Screen Interface (PXA320) */
#define IRQ_USIM2	PXA_IRQ(38)	/* USIM2 Controller */
#define IRQ_GRPHICS	PXA_IRQ(39)	/* Graphics Controller */
#define IRQ_MMC2	PXA_IRQ(41)	/* MMC2 Controller */
#define IRQ_1WIRE	PXA_IRQ(44)	/* 1-Wire Controller */
#define IRQ_NAND	PXA_IRQ(45)	/* NAND Controller */
#define IRQ_USB2	PXA_IRQ(46)	/* USB 2.0 Device Controller */
#define IRQ_WAKEUP0	PXA_IRQ(49)	/* EXT_WAKEUP0 */
#define IRQ_WAKEUP1	PXA_IRQ(50)	/* EXT_WAKEUP1 */
#define IRQ_DMEMC	PXA_IRQ(51)	/* Dynamic Memory Controller */
#define IRQ_MMC3	PXA_IRQ(55)	/* MMC3 Controller (PXA310) */
#endif

#define PXA_GPIO_IRQ_BASE	(64)
#define PXA_GPIO_IRQ_NUM	(128)

#define GPIO_2_x_TO_IRQ(x)	(PXA_GPIO_IRQ_BASE + (x))
#define IRQ_GPIO(x)	(((x) < 2) ? (IRQ_GPIO0 + (x)) : GPIO_2_x_TO_IRQ(x))

#define IRQ_TO_GPIO_2_x(i)	((i) - PXA_GPIO_IRQ_BASE)
#define IRQ_TO_GPIO(i)	(((i) < IRQ_GPIO(2)) ? ((i) - IRQ_GPIO0) : IRQ_TO_GPIO_2_x(i))

/*
 * Board IRQs start from the last built-in GPIO IRQ, since the kernel
 * can only run on one machine at a time, these numbers can be reused.
 * The end of board IRQ (PXA_BOARD_IRQ_END) is maintained to be the
 * largest one of all the selected boards.
 *
 * NOTE: PXA_BOARD_IRQ_END by default leaves 16 IRQs for backward
 * compatibility
 */
#define PXA_BOARD_IRQ_START	(PXA_GPIO_IRQ_BASE + PXA_GPIO_IRQ_NUM)
#define PXA_BOARD_IRQ_END	(PXA_BOARD_IRQ_START + 16)
#define PXA_BOARD_IRQ(x)	(PXA_BOARD_IRQ_START + (x))

/* the macros below are kept here for backward compatibility */
#define IRQ_BOARD_START		PXA_BOARD_IRQ_START
#define IRQ_BOARD_END		PXA_BOARD_IRQ_END

#define LUBBOCK_IRQ(x)		PXA_BOARD_IRQ(x)

#ifdef CONFIG_ARCH_LUBBOCK
#define LUBBOCK_SD_IRQ		LUBBOCK_IRQ(0)
#define LUBBOCK_SA1111_IRQ	LUBBOCK_IRQ(1)
#define LUBBOCK_USB_IRQ		LUBBOCK_IRQ(2)  /* usb connect */
#define LUBBOCK_ETH_IRQ		LUBBOCK_IRQ(3)
#define LUBBOCK_UCB1400_IRQ	LUBBOCK_IRQ(4)
#define LUBBOCK_BB_IRQ		LUBBOCK_IRQ(5)
#define LUBBOCK_USB_DISC_IRQ	LUBBOCK_IRQ(6)  /* usb disconnect */
#define LUBBOCK_LAST_IRQ	LUBBOCK_IRQ(6)

#if PXA_BOARD_IRQ_END < LUBBOCK_LAST_IRQ
#undef  PXA_BOARD_IRQ_END
#define PXA_BOARD_IRQ_END	LUBBOCK_LAST_IRQ
#endif
#endif /* CONFIG_ARCH_LUBBOCK */

#define MAINSTONE_IRQ(x)	PXA_BOARD_IRQ(x)

#ifdef CONFIG_MACH_MAINSTONE
#define MAINSTONE_MMC_IRQ	MAINSTONE_IRQ(0)
#define MAINSTONE_USIM_IRQ	MAINSTONE_IRQ(1)
#define MAINSTONE_USBC_IRQ	MAINSTONE_IRQ(2)
#define MAINSTONE_ETHERNET_IRQ	MAINSTONE_IRQ(3)
#define MAINSTONE_AC97_IRQ	MAINSTONE_IRQ(4)
#define MAINSTONE_PEN_IRQ	MAINSTONE_IRQ(5)
#define MAINSTONE_MSINS_IRQ	MAINSTONE_IRQ(6)
#define MAINSTONE_EXBRD_IRQ	MAINSTONE_IRQ(7)
#define MAINSTONE_S0_CD_IRQ	MAINSTONE_IRQ(9)
#define MAINSTONE_S0_STSCHG_IRQ	MAINSTONE_IRQ(10)
#define MAINSTONE_S0_IRQ	MAINSTONE_IRQ(11)
#define MAINSTONE_S1_CD_IRQ	MAINSTONE_IRQ(13)
#define MAINSTONE_S1_STSCHG_IRQ	MAINSTONE_IRQ(14)
#define MAINSTONE_S1_IRQ	MAINSTONE_IRQ(15)
#define MAINSTONE_LAST_IRQ	MAINSTONE_S1_IRQ

#if PXA_BOARD_IRQ_END < MAINSTONE_LAST_IRQ
#undef  PXA_BOARD_IRQ_END
#define PXA_BOARD_IRQ_END	MAINSTONE_LAST_IRQ
#endif
#endif /* CONFIG_MACH_MAINSTONE */

#ifdef CONFIG_MACH_ZYLONITE
/*
 * Zylonite has 2 GPIO expanders each with 16 GPIOs,
 * reserve 32 IRQs for them
 */
#define ZYLONITE_LAST_IRQ      PXA_BOARD_IRQ(31)

#if PXA_BOARD_IRQ_END < ZYLONITE_LAST_IRQ
#undef  PXA_BOARD_IRQ_END
#define PXA_BOARD_IRQ_END      ZYLONITE_LAST_IRQ
#endif
#endif /* CONFIG_MACH_ZYLONITE */

#define LPD270_IRQ(x)		PXA_BOARD_IRQ(x)

#ifdef CONFIG_MACH_LOGICPD_PXA270
#define LPD270_USBC_IRQ		LPD270_IRQ(2)
#define LPD270_ETHERNET_IRQ	LPD270_IRQ(3)
#define LPD270_AC97_IRQ		LPD270_IRQ(4)
#define LPD270_LAST_IRQ		LPD270_AC97_IRQ

#if PXA_BOARD_IRQ_END < LPD270_LAST_IRQ
#undef  PXA_BOARD_IRQ_END
#define PXA_BOARD_IRQ_END	LPD270_LAST_IRQ
#endif
#endif /* CONFIG_MACH_LOGICPD_PXA270 */

#define PCM027_IRQ(x)		PXA_BOARD_IRQ(x)

#ifdef CONFIG_MACH_PCM027
#define PCM027_BTDET_IRQ	PCM027_IRQ(0)
#define PCM027_FF_RI_IRQ	PCM027_IRQ(1)
#define PCM027_MMCDET_IRQ	PCM027_IRQ(2)
#define PCM027_PM_5V_IRQ	PCM027_IRQ(3)
#define PCM027_LAST_IRQ		PCM027_PM_5V_IRQ

#if PXA_BOARD_IRQ_END < PCM027_LAST_IRQ
#undef  PXA_BOARD_IRQ_END
#define PXA_BOARD_IRQ_END	PCM027_LAST_IRQ
#endif
#endif /* CONFIG_MACH_PCM027 */

/*
 * Extended IRQs for companion chips start from the last board-specific IRQ.
 * NOTE: unlike board specific IRQs, the number space for these IRQs cannot
 * be reused, since there could be multiple companion chips on a same plat-
 * form. So the IRQ numbers are arranged here one chip by one, if selected.
 *
 * To add IRQ numbers for a new chip, follow the LAST_CHIP_IRQ_LAST
 * The total IRQ number will be the (LAST_CHIP_IRQ + 1)
 */
#define PXA_EXT_IRQ_START	(PXA_BOARD_IRQ_END + 1)

#define SA1111_IRQ(x)		(PXA_BOARD_IRQ_END + 1 + (x))

#ifdef CONFIG_SA1111
#define IRQ_SA1111_START	SA1111_IRQ(0)
#define IRQ_GPAIN0		SA1111_IRQ(0)
#define IRQ_GPAIN1		SA1111_IRQ(1)
#define IRQ_GPAIN2		SA1111_IRQ(2)
#define IRQ_GPAIN3		SA1111_IRQ(3)
#define IRQ_GPBIN0		SA1111_IRQ(4)
#define IRQ_GPBIN1		SA1111_IRQ(5)
#define IRQ_GPBIN2		SA1111_IRQ(6)
#define IRQ_GPBIN3		SA1111_IRQ(7)
#define IRQ_GPBIN4		SA1111_IRQ(8)
#define IRQ_GPBIN5		SA1111_IRQ(9)
#define IRQ_GPCIN0		SA1111_IRQ(10)
#define IRQ_GPCIN1		SA1111_IRQ(11)
#define IRQ_GPCIN2		SA1111_IRQ(12)
#define IRQ_GPCIN3		SA1111_IRQ(13)
#define IRQ_GPCIN4		SA1111_IRQ(14)
#define IRQ_GPCIN5		SA1111_IRQ(15)
#define IRQ_GPCIN6		SA1111_IRQ(16)
#define IRQ_GPCIN7		SA1111_IRQ(17)
#define IRQ_MSTXINT		SA1111_IRQ(18)
#define IRQ_MSRXINT		SA1111_IRQ(19)
#define IRQ_MSSTOPERRINT	SA1111_IRQ(20)
#define IRQ_TPTXINT		SA1111_IRQ(21)
#define IRQ_TPRXINT		SA1111_IRQ(22)
#define IRQ_TPSTOPERRINT	SA1111_IRQ(23)
#define SSPXMTINT		SA1111_IRQ(24)
#define SSPRCVINT		SA1111_IRQ(25)
#define SSPROR			SA1111_IRQ(26)
#define AUDXMTDMADONEA		SA1111_IRQ(32)
#define AUDRCVDMADONEA		SA1111_IRQ(33)
#define AUDXMTDMADONEB		SA1111_IRQ(34)
#define AUDRCVDMADONEB		SA1111_IRQ(35)
#define AUDTFSR			SA1111_IRQ(36)
#define AUDRFSR			SA1111_IRQ(37)
#define AUDTUR			SA1111_IRQ(38)
#define AUDROR			SA1111_IRQ(39)
#define AUDDTS			SA1111_IRQ(40)
#define AUDRDD			SA1111_IRQ(41)
#define AUDSTO			SA1111_IRQ(42)
#define IRQ_USBPWR		SA1111_IRQ(43)
#define IRQ_HCIM		SA1111_IRQ(44)
#define IRQ_HCIBUFFACC		SA1111_IRQ(45)
#define IRQ_HCIRMTWKP		SA1111_IRQ(46)
#define IRQ_NHCIMFCIR		SA1111_IRQ(47)
#define IRQ_USB_PORT_RESUME	SA1111_IRQ(48)
#define IRQ_S0_READY_NINT	SA1111_IRQ(49)
#define IRQ_S1_READY_NINT	SA1111_IRQ(50)
#define IRQ_S0_CD_VALID		SA1111_IRQ(51)
#define IRQ_S1_CD_VALID		SA1111_IRQ(52)
#define IRQ_S0_BVD1_STSCHG	SA1111_IRQ(53)
#define IRQ_S1_BVD1_STSCHG	SA1111_IRQ(54)

#define SA1111_LAST_IRQ		IRQ_S1_BVD1_STSCHG
#else
#define SA1111_LAST_IRQ		SA1111_IRQ(-1)
#endif /* CONFIG_SA1111 */

#define LOCOMO_IRQ(x)		(SA1111_LAST_IRQ + 1 + (x))

#ifdef CONFIG_SHARP_LOCOMO

#define IRQ_LOCOMO_KEY		LOCOMO_IRQ(0)
#define IRQ_LOCOMO_GPIO0	LOCOMO_IRQ(1)
#define IRQ_LOCOMO_GPIO1	LOCOMO_IRQ(2)
#define IRQ_LOCOMO_GPIO2	LOCOMO_IRQ(3)
#define IRQ_LOCOMO_GPIO3	LOCOMO_IRQ(4)
#define IRQ_LOCOMO_GPIO4	LOCOMO_IRQ(5)
#define IRQ_LOCOMO_GPIO5	LOCOMO_IRQ(6)
#define IRQ_LOCOMO_GPIO6	LOCOMO_IRQ(7)
#define IRQ_LOCOMO_GPIO7	LOCOMO_IRQ(8)
#define IRQ_LOCOMO_GPIO8	LOCOMO_IRQ(9)
#define IRQ_LOCOMO_GPIO9	LOCOMO_IRQ(10)
#define IRQ_LOCOMO_GPIO10	LOCOMO_IRQ(11)
#define IRQ_LOCOMO_GPIO11	LOCOMO_IRQ(12)
#define IRQ_LOCOMO_GPIO12	LOCOMO_IRQ(13)
#define IRQ_LOCOMO_GPIO13	LOCOMO_IRQ(14)
#define IRQ_LOCOMO_GPIO14	LOCOMO_IRQ(15)
#define IRQ_LOCOMO_GPIO15	LOCOMO_IRQ(16)
#define IRQ_LOCOMO_LT		LOCOMO_IRQ(17)
#define IRQ_LOCOMO_SPI_RFR	LOCOMO_IRQ(18)
#define IRQ_LOCOMO_SPI_RFW	LOCOMO_IRQ(19)
#define IRQ_LOCOMO_SPI_OVRN	LOCOMO_IRQ(20)
#define IRQ_LOCOMO_SPI_TEND	LOCOMO_IRQ(21)

#define LOCOMO_LAST_IRQ		IRQ_LOCOMO_SPI_TEND
#else
#define LOCOMO_LAST_IRQ		LOCOMO_IRQ(-1)
#endif

#define IT8152_IRQ(x)		(LOCOMO_LAST_IRQ + 1 + (x))

#ifdef CONFIG_PCI_HOST_ITE8152
#endif
/* ITE8152 irqs */
/* add IT8152 IRQs beyond BOARD_END */
#ifdef CONFIG_PCI_HOST_ITE8152

/* IRQ-sources in 3 groups - local devices, LPC (serial), and external PCI */
#define IT8152_LD_IRQ_COUNT     9
#define IT8152_LP_IRQ_COUNT     16
#define IT8152_PD_IRQ_COUNT     15

/* Priorities: */
#define IT8152_PD_IRQ(i)        IT8152_IRQ(i)
#define IT8152_LP_IRQ(i)        (IT8152_IRQ(i) + IT8152_PD_IRQ_COUNT)
#define IT8152_LD_IRQ(i)        (IT8152_IRQ(i) + IT8152_PD_IRQ_COUNT + IT8152_LP_IRQ_COUNT)

#define IT8152_LAST_IRQ         IT8152_LD_IRQ(IT8152_LD_IRQ_COUNT - 1)
#else
#define IT8152_LAST_IRQ		IT8152_IRQ(-1)
#endif

#define NR_IRQS (IT8152_LAST_IRQ + 1)

#endif /* __ASM_ARCH_IRQS_H */
