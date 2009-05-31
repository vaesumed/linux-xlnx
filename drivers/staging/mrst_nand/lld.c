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

#ifdef ELDORA
#include "defs.h"
#include "lld.h"
#else
#include "spectraswconfig.h"
#include "ffsport.h"
#include "ffsdefs.h"
#include "lld.h"

#ifdef NEW_LLD_API
#include "flash.h"
#endif

#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
#if FLASH_EMU          /* vector all the LLD calls to the LLD_EMU code */
#include "lld_emu.h"
#include "lld_cdma.h"

/* common functions: */
u16 GLOB_LLD_Flash_Reset(void)
{
       return emu_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
       return emu_Read_Device_ID();
}

u16 GLOB_LLD_Flash_Release(void)
{
       return emu_Flash_Release();
}

#if CMD_DMA                    /* new APIs with tags */
u16 GLOB_LLD_Flash_Init(u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return emu_Flash_Init();
       else
               return emu_CDMA_Flash_Init();
}

u16 GLOB_LLD_Erase_Block(u32 block, u8 TagCount, u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return emu_Erase_Block(block);
       else
               return CDMA_Data_CMD(TagCount, ERASE_CMD, 0, block, 0, 0,
                                    Flags);
}

u16 GLOB_LLD_Write_Page_Main(u8 *data, u32 block, u16 page,
                               u16 count, u8 TagCount)
{
       return CDMA_Data_CMD(TagCount, WRITE_MAIN_CMD, data, block, page, count,
                            0);
}

u16 GLOB_LLD_Read_Page_Main(u8 *data, u32 block, u16 page,
                              u16 count, u8 TagCount, u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return emu_Read_Page_Main(data, block, page, count);
       else
               return CDMA_Data_CMD(TagCount, READ_MAIN_CMD, data, block, page,
                                    count, Flags);
}

u16 GLOB_LLD_MemCopy_CMD(u8 TagCount, u8 *dest, u8 *src,
                       u16 ByteCount, u16 flag)
{
       return CDMA_MemCopy_CMD(TagCount, dest, src, ByteCount, flag);
}

u16 GLOB_LLD_Execute_CMDs(u16 count)
{
       return emu_CDMA_Execute_CMDs(count);
}

u16 GLOB_LLD_Event_Status(void)
{
       return emu_CDMA_Event_Status();
}

#ifndef ELDORA
void GLOB_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
       emu_Enable_Disable_Interrupts(INT_ENABLE);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
                                     u16 Page, u16 PageCount,
                                     u8 TagCount, u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return emu_Write_Page_Main_Spare(write_data, block, Page,
                                                PageCount);
       else
               return CDMA_Data_CMD(TagCount, WRITE_MAIN_SPARE_CMD, write_data,
                                    block, Page, PageCount, Flags);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 Block,
                                    u16 Page, u16 PageCount,
                                    u8 TagCount)
{
       return CDMA_Data_CMD(TagCount, READ_MAIN_SPARE_CMD,
               read_data, Block, Page, PageCount,
               LLD_CMD_FLAG_MODE_CDMA);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 Block, u16 Page,
                                u16 PageCount)
{
       return emu_Write_Page_Spare(write_data, Block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 Block, u16 Page,
                               u16 PageCount)
{
       return emu_Read_Page_Spare(read_data, Block, Page, PageCount);
}

u32  GLOB_LLD_Memory_Pool_Size(void)
{
    return CDMA_Memory_Pool_Size();
}

int GLOB_LLD_Mem_Config(u8 *pMem)
{
    return CDMA_Mem_Config(pMem);
}
#endif /* !ELDORA */

#else /* if not CMD_DMA, use old style parameters without tags */
u16 GLOB_LLD_Flash_Init(void)
{
       return emu_Flash_Init();
}

u16 GLOB_LLD_Erase_Block(u32 block_add)
{
       return emu_Erase_Block(block_add);
}

u16 GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block, u16 Page,
                               u16 PageCount)
{
       return emu_Write_Page_Main(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block, u16 Page,
                              u16 PageCount)
{
       return emu_Read_Page_Main(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
                       u32 block, u16 page, u16 page_count)
{
       return emu_Read_Page_Main(read_data, block, page, page_count);
}
#ifndef ELDORA
void GLOB_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
       emu_Enable_Disable_Interrupts(INT_ENABLE);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
                                     u16 Page, u16 PageCount)
{
       return emu_Write_Page_Main_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 block,
                                    u16 Page, u16 PageCount)
{
       return emu_Read_Page_Main_Spare(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block, u16 Page,
                                u16 PageCount)
{
       return emu_Write_Page_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block, u16 Page,
                               u16 PageCount)
{
       return emu_Read_Page_Spare(read_data, block, Page, PageCount);
}

u32  GLOB_LLD_Memory_Pool_Size(void)
{
    return 0;
}

int GLOB_LLD_Mem_Config(u8 *pMem)
{
    return 0;
}

#endif /* !ELDORA */
#endif /* CMD_DMA or not */

#ifndef ELDORA
u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
    return  emu_Get_Bad_Block(block);
}
#endif /* !ELDORA */

#endif /* FLASH_EMU */
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
#if FLASH_NAND /* vector all the LLD calls to the NAND controller code */
#include "lld_nand.h"
#ifndef ELDORA
#include "flash.h"
#endif

/* common functions for LLD_NAND */
void GLOB_LLD_ECC_Control(int enable)
{
       NAND_ECC_Ctrl(enable);
}

/* common functions for LLD_NAND */
u16 GLOB_LLD_Flash_Reset(void)
{
       return NAND_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
       return NAND_Read_Device_ID();
}

u16 GLOB_LLD_UnlockArrayAll(void)
{
       return NAND_UnlockArrayAll();
}

void GLOB_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
       NAND_LLD_Enable_Disable_Interrupts(INT_ENABLE);
}

u16 GLOB_LLD_Flash_Init(void)
{
       return NAND_Flash_Init();
}

u16 GLOB_LLD_Flash_Release(void)
{
       return 0;
}

u16 GLOB_LLD_Event_Status(void)
{
       return NAND_LLD_Event_Status();
}

u16 GLOB_LLD_Erase_Block(u32 block_add)
{
       return NAND_Erase_Block(block_add);
}


u16 GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block, u16 Page,
                               u16 PageCount)
{
       return NAND_Write_Page_Main(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block, u16 page,
                              u16 page_count)
{
       return NAND_Read_Page_Main(read_data, block, page, page_count);
}

u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
                       u32 block, u16 page, u16 page_count)
{
       return NAND_Read_Page_Main_Polling(read_data,
                       block, page, page_count);
}

#ifndef ELDORA
u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
                                     u16 Page, u16 PageCount)
{
       return NAND_Write_Page_Main_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block, u16 Page,
                                u16 PageCount)
{
       return NAND_Write_Page_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 block,
                                    u16 page, u16 page_count)
{
       return NAND_Read_Page_Main_Spare(read_data, block, page, page_count);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block, u16 Page,
                               u16 PageCount)
{
       return NAND_Read_Page_Spare(read_data, block, Page, PageCount);
}

u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
       return  NAND_Get_Bad_Block(block);
}

u32  GLOB_LLD_Memory_Pool_Size(void)
{
       return NAND_Memory_Pool_Size();
}

int GLOB_LLD_Mem_Config(u8 *pMem)
{
       return NAND_Mem_Config(pMem);
}

#endif /* !ELDORA */

#endif /* FLASH_NAND */
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

/* CMD DMA is not applicable for Eldora */
#ifndef ELDORA

#if FLASH_CDMA /* vector all the LLD data calls to the LLD_CDMA module */
                  /* vector some other  LLD  calls to the LLD_CDMA module*/
                  /* vector the common  LLD  calls to the LLD_NAND module*/
#include "lld_cdma.h"
#include "lld_nand.h"

u16 GLOB_LLD_Flash_Reset(void)
{
       return NAND_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
       return NAND_Read_Device_ID();
}

u16 GLOB_LLD_UnlockArrayAll(void)
{
       return NAND_UnlockArrayAll();
}

void GLOB_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
       NAND_LLD_Enable_Disable_Interrupts(INT_ENABLE);
}

u16 GLOB_LLD_Flash_Release(void)       /* not used; NOP */
{
       return 0;
}

u16 GLOB_LLD_Flash_Init(u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return NAND_Flash_Init();
       else
               return CDMA_Flash_Init();
}

int GLOB_LLD_is_cdma_int(void)
{
       return is_cdma_interrupt();
}

u16 GLOB_LLD_Event_Status(void)
{
       return CDMA_Event_Status();
}

u16 GLOB_LLD_MemCopy_CMD(u8 TagCount, u8 *dest, u8 *src,
                       u16 ByteCount, u16 flag)
{
       return CDMA_MemCopy_CMD(TagCount, dest, src, ByteCount, flag);
}

u16 GLOB_LLD_Execute_CMDs(u16 count)
{
       return CDMA_Execute_CMDs(count);
}

u16 GLOB_LLD_Erase_Block(u32 block, u8 TagCount, u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL)
               return NAND_Erase_Block(block);
       else
               return CDMA_Data_CMD(TagCount, ERASE_CMD, 0, block, 0, 0,
                                    Flags);
}

u16 GLOB_LLD_Write_Page_Main(u8 *data, u32 block, u16 page,
                               u16 count, u8 TagCount)
{
       return CDMA_Data_CMD(TagCount, WRITE_MAIN_CMD, data, block, page, count,
                            0);
}

u16 GLOB_LLD_Read_Page_Main(u8 *data, u32 block, u16 page,
                              u16 count, u8 TagCount, u16 Flags)
{
       if (Flags & LLD_CMD_FLAG_MODE_POLL) {
               return NAND_Read_Page_Main(data, block, page, count);
       } else
               return CDMA_Data_CMD(TagCount, READ_MAIN_CMD, data, block, page,
                                    count, Flags);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *data, u32 block, u16 page,
                                u16 count)
{
       return NAND_Write_Page_Spare(data, block, page, count);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *data, u32 block, u16 page,
                               u16 count)
{
       return NAND_Read_Page_Spare(data, block, page, count);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *data, u32 block, u16 page,
                                     u16 count, u8 TagCount, u16 Flags)
{
       return CDMA_Data_CMD(TagCount, WRITE_MAIN_SPARE_CMD, data, block, page,
                            count, Flags);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *data, u32 block, u16 page,
                                    u16 count, u8 TagCount)
{
       return CDMA_Data_CMD(TagCount, READ_MAIN_SPARE_CMD, data, block, page,
                            count, LLD_CMD_FLAG_MODE_CDMA);
}

u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
    return  NAND_Get_Bad_Block(block);
}

u32  GLOB_LLD_Memory_Pool_Size(void)
{
    return CDMA_Memory_Pool_Size();
}

int GLOB_LLD_Mem_Config(u8 *pMem)
{
    return CDMA_Mem_Config(pMem);
}
#endif /* FLASH_CDMA */

#endif /* !ELDORA */
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

/* end of LLD.c */
