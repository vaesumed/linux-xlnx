#ifndef _ASM_VOYAGER_VECTORS_H
#define _ASM_VOYAGER_VECTORS_H

/* These define the CPIs we use in linux */
#define VIC_CPI_LEVEL0			0
#define VIC_CPI_LEVEL1			1
/* now the fake CPIs */
#define VIC_TIMER_CPI			2
#define VIC_INVALIDATE_CPI		3
#define VIC_RESCHEDULE_CPI		4
#define VIC_ENABLE_IRQ_CPI		5
#define VIC_CALL_FUNCTION_CPI		6
#define VIC_CALL_FUNCTION_SINGLE_CPI	7

/* Now the QIC CPIs:  Since we don't need the two initial levels,
 * these are 2 less than the VIC CPIs */
#define QIC_CPI_OFFSET			1
#define QIC_TIMER_CPI			(VIC_TIMER_CPI - QIC_CPI_OFFSET)
#define QIC_INVALIDATE_CPI		(VIC_INVALIDATE_CPI - QIC_CPI_OFFSET)
#define QIC_RESCHEDULE_CPI		(VIC_RESCHEDULE_CPI - QIC_CPI_OFFSET)
#define QIC_ENABLE_IRQ_CPI		(VIC_ENABLE_IRQ_CPI - QIC_CPI_OFFSET)
#define QIC_CALL_FUNCTION_CPI		(VIC_CALL_FUNCTION_CPI - QIC_CPI_OFFSET)
#define QIC_CALL_FUNCTION_SINGLE_CPI	(VIC_CALL_FUNCTION_SINGLE_CPI - QIC_CPI_OFFSET)

#define VIC_START_FAKE_CPI		VIC_TIMER_CPI
#define VIC_END_FAKE_CPI		VIC_CALL_FUNCTION_SINGLE_CPI

/* this is the SYS_INT CPI. */
#define VIC_SYS_INT			8
#define VIC_CMN_INT			15

/* This is the boot CPI for alternate processors.  It gets overwritten
 * by the above once the system has activated all available processors */
#define VIC_CPU_BOOT_CPI		VIC_CPI_LEVEL0
#define VIC_CPU_BOOT_ERRATA_CPI		(VIC_CPI_LEVEL0 + 8)

#endif
