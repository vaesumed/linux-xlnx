/****************************************************************************/

/*
 *  m520xsim.h -- ColdFire 5207/5208 System Integration Module support.
 *
 *  (C) Copyright 2005, Intec Automation (mike@steroidmicros.com)
 */

/****************************************************************************/
#ifndef m520xsim_h
#define m520xsim_h
/****************************************************************************/

/*
 *  Define the 520x SIM register set addresses.
 */
#define MCFICM_INTC0        0x48000     /* Base for Interrupt Ctrl 0 */
#define MCFINTC_IPRH        0x00        /* Interrupt pending 32-63 */
#define MCFINTC_IPRL        0x04        /* Interrupt pending 1-31 */
#define MCFINTC_IMRH        0x08        /* Interrupt mask 32-63 */
#define MCFINTC_IMRL        0x0c        /* Interrupt mask 1-31 */
#define MCFINTC_INTFRCH     0x10        /* Interrupt force 32-63 */
#define MCFINTC_INTFRCL     0x14        /* Interrupt force 1-31 */
#define MCFINTC_SIMR        0x1c        /* Set interrupt mask 0-63 */
#define MCFINTC_CIMR        0x1d        /* Clear interrupt mask 0-63 */
#define MCFINTC_ICR0        0x40        /* Base ICR register */

/*
 *  The common interrupt controller code just wants to know the absolute
 *  address to the SIMR and CIMR registers (not offsets into IPSBAR).
 *  The 520x family only has a single INTC unit.
 */
#define MCFINTC0_SIMR       (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_SIMR)
#define MCFINTC0_CIMR       (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_CIMR)
#define	MCFINTC0_ICR0       (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0)
#define MCFINTC1_SIMR       (0)
#define MCFINTC1_CIMR       (0)
#define	MCFINTC1_ICR0       (0)

#define MCFINT_VECBASE      64
#define MCFINT_UART0        26          /* Interrupt number for UART0 */
#define MCFINT_UART1        27          /* Interrupt number for UART1 */
#define MCFINT_UART2        28          /* Interrupt number for UART2 */
#define MCFINT_QSPI         31          /* Interrupt number for QSPI */
#define MCFINT_PIT1         4           /* Interrupt number for PIT1 (PIT0 in processor) */

/*
 *  SDRAM configuration registers.
 */
#define MCFSIM_SDMR         0x000a8000	/* SDRAM Mode/Extended Mode Register */
#define MCFSIM_SDCR         0x000a8004	/* SDRAM Control Register */
#define MCFSIM_SDCFG1       0x000a8008	/* SDRAM Configuration Register 1 */
#define MCFSIM_SDCFG2       0x000a800c	/* SDRAM Configuration Register 2 */
#define MCFSIM_SDCS0        0x000a8110	/* SDRAM Chip Select 0 Configuration */
#define MCFSIM_SDCS1        0x000a8114	/* SDRAM Chip Select 1 Configuration */


#define MCF_GPIO_PAR_UART                   (0xA4036)
#define MCF_GPIO_PAR_FECI2C                 (0xA4033)
#define MCF_GPIO_PAR_FEC                    (0xA4038)

#define MCF_GPIO_PAR_UART_PAR_URXD0         (0x0001)
#define MCF_GPIO_PAR_UART_PAR_UTXD0         (0x0002)

#define MCF_GPIO_PAR_UART_PAR_URXD1         (0x0040)
#define MCF_GPIO_PAR_UART_PAR_UTXD1         (0x0080)

#define MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2   (0x02)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2   (0x04)

/*
 *  Reset Controll Unit.
 */
#define	MCF_RCR			0xFC0A0000
#define	MCF_RSR			0xFC0A0001

#define	MCF_RCR_SWRESET		0x80		/* Software reset bit */
#define	MCF_RCR_FRCSTOUT	0x40		/* Force external reset */

/****************************************************************************/
#endif  /* m520xsim_h */
