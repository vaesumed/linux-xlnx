/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */



#ifndef _LLD_
#define _LLD_

#include "ffsport.h"
#include "spectraswconfig.h"
#include "flash.h"

#define GOOD_BLOCK 0
#define DEFECTIVE_BLOCK 1
#define READ_ERROR 2

#define CLK_X  5
#define CLK_MULTI 4

/* Max main & spare sizes supported in LLD */
#define MAX_PAGE_MAIN_AREA          8192
#define MAX_PAGE_SPARE_AREA          512
#define MAX_PAGE_MAINSPARE_AREA     8704

/* for GLOB_LLD_Enable_Disable_Interrupts */
#define ENABLE_INTERRUPTS           0x0001
#define DISABLE_INTERRUPTS          0x0000



/* Typedefs */

/*  prototypes: API for LLD */
/* Currently, Write_Page_Main
 *                       MemCopy
 *                       Read_Page_Main_Spare
 * do not have flag because they were not implemented prior to this
 * They are not being added to keep changes to a minimum for now.
 * Currently, they are not required (only reqd for Wr_P_M_S.)
 * Later on, these NEED to be changed.
 */
 extern void GLOB_LLD_ECC_Control(int enable);
extern u16   GLOB_LLD_Flash_Release(void);
extern u16   GLOB_LLD_Flash_Reset(void);
extern u16   GLOB_LLD_Read_Device_ID(void);
#if CMD_DMA
extern u16   GLOB_LLD_Flash_Init(u16 Flags);
extern u16   GLOB_LLD_Execute_CMDs(u16 count);
extern u16   GLOB_LLD_Erase_Block(u32 block, u8 TagCount,
                                       u16 Flags);
extern u16   GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block,
                                       u16 Page, u16
                                       PageCount, u8 CommandCount);
extern u16   GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block,
                                       u16 Page, u16
                               PageCount, u8 CommandCount, u16 Flags);
extern u16  GLOB_LLD_MemCopy_CMD(u8 tag, u8 *dest, u8 *src,
                               u16 ByteCount, u16 flag);
#else
extern u16   GLOB_LLD_Flash_Init(void);
extern u16   GLOB_LLD_Erase_Block(u32 block_add);
extern u16   GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block,
                                       u16 Page, u16 PageCount);
extern u16   GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block,
                                       u16 Page, u16 PageCount);
extern u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
                       u32 block, u16 page, u16 page_count);
#endif

extern int GLOB_LLD_is_cdma_int(void);
extern u16   GLOB_LLD_Event_Status(void);
extern void     GLOB_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE);

extern u16   GLOB_LLD_UnlockArrayAll(void);
extern u16   GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block,
                                       u16 Page, u16 PageCount);
extern u16   GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block,
                                       u16 Page, u16 PageCount);
extern u16   GLOB_LLD_Get_Bad_Block(u32 block);
#if CMD_DMA
extern u16   GLOB_LLD_Write_Page_Main_Spare(u8 *write_data,
                                       u32 block, u16 Page, u16
                               PageCount, u8 CommandCount, u16 Flags);
extern u16   GLOB_LLD_Read_Page_Main_Spare(u8 *read_data,
                                       u32 block, u16 Page, u16
                                       PageCount, u8 CommandCount);
#else
extern u16   GLOB_LLD_Write_Page_Main_Spare(u8 *write_data,
                       u32 block, u16 Page, u16 PageCount);
extern u16   GLOB_LLD_Read_Page_Main_Spare(u8 *read_data,
                                       u32 block, u16 Page, u16
                                       PageCount);
#endif /* CMD_DMA */

extern u32  GLOB_LLD_Memory_Pool_Size(void);
extern int GLOB_LLD_Mem_Config(u8 *pMem);

#if CMD_DMA
#define LLD_CMD_FLAG_ORDER_BEFORE_REST         (0x1)
#define LLD_CMD_FLAG_MODE_POLL                 (0x4)
#define LLD_CMD_FLAG_MODE_CDMA                 (0x8)
#endif /* CMD_DMA */


#endif /*_LLD_ */


