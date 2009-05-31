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


/* note: compile with LLD_NAND.C as it contains some common functions */
#include "spectraswconfig.h"
#include "lld.h"
#include "lld_nand.h"
#include "lld_cdma.h"
#include "lld_emu.h"
#include "flash.h"
#include "NAND_Regs_4.h"

#define DBG_SNC_PRINTEVERY    1000000

#if CMD_DMA
#define MODE_02             (0x2 << 26)
#define MAX_DESC_PER_CHANNEL     (MAX_DESCS + 2)

#if FLASH_CDMA
static void ResetSyncModule(void);
#endif

/* command is sent. This is global so FTL can check final cmd results */
struct pending_cmd    PendingCMD[MAX_DESCS + MAX_CHANS];

struct cdma_descriptor (*cdma_desc)[MAX_DESC_PER_CHANNEL];
struct memcpy_descriptor (*memcp_desc)[MAX_DESCS];

u16 dcount[MAX_CHANS];


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Data_Cmd
* Inputs:       tag (0-255)
*               cmd code (aligned for hw)
*               data: pointer to source or destination
*               block: block address
*               page: page address
*               count: num pages to transfer
* Outputs:      PASS
* Description:  This function takes the parameters and puts them
*                   into the "pending commands" array.
*               It does not parse or validate the parameters.
*               The array index is same as the tag.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_Data_CMD(u8 tag, u8 CMD, u8 *data,
       u32 block, u16 page, u16 count, u16 flags)
{
       int i;

       debug_boundary_error(block, DeviceInfo.wTotalBlocks, tag);
       debug_boundary_error(count, DeviceInfo.wPagesPerBlock+1, tag);
       debug_boundary_error(tag, 252, 0);

       tag += MAX_CHANS;
       PendingCMD[tag].Tag = tag - MAX_CHANS;
       PendingCMD[tag].CMD = CMD;
       PendingCMD[tag].DataAddr = data;
       PendingCMD[tag].Block = block;
       PendingCMD[tag].Page = page;
       PendingCMD[tag].PageCount = count;
       PendingCMD[tag].DataDestAddr = 0;
       PendingCMD[tag].DataSrcAddr = 0;
       PendingCMD[tag].MemCopyByteCnt = 0;
       PendingCMD[tag].Flags = flags;
       PendingCMD[tag].SBDCmdIndex = g_SBDCmdIndex;

       for (i = 0; i <= MAX_CHANS; i++)
               PendingCMD[tag].ChanSync[i] = 0;

       PendingCMD[tag].Status = 0xB0B;

#if FLASH_CDMA
       switch (CMD) {
       case WRITE_MAIN_SPARE_CMD:
               NAND_Conv_Main_Spare_Data_Log2Phy_Format(data, count);
               break;
       case WRITE_SPARE_CMD:
               NAND_Conv_Spare_Data_Log2Phy_Format(data);
               break;
       default:
               break;
       }
#endif
       return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_MemCopy_CMD
* Inputs:       tag (0-255)
*               dest: pointer to destination
*               src:  pointer to source
*               count: num bytes to transfer
* Outputs:      PASS
* Description:  This function takes the parameters and puts them
*                   into the "pending commands" array.
*               It does not parse or validate the parameters.
*               The array index is same as the tag.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_MemCopy_CMD(u8 tag, u8 *dest, u8 *src,
                       u16 ByteCount, u16 flags)
{
       int i;

       nand_dbg_print(NAND_DBG_DEBUG,
               "CDMA MemC Command called tag=%u\n", tag);

       debug_boundary_error(tag, 252, 0);

       tag += MAX_CHANS;
       PendingCMD[tag].Tag = tag - MAX_CHANS;
       PendingCMD[tag].CMD = MEMCOPY_CMD;
       PendingCMD[tag].DataAddr = 0;
       PendingCMD[tag].Block = 0;
       PendingCMD[tag].Page = 0;
       PendingCMD[tag].PageCount = 0;
       PendingCMD[tag].DataDestAddr = dest;
       PendingCMD[tag].DataSrcAddr = src;
       PendingCMD[tag].MemCopyByteCnt = ByteCount;
       PendingCMD[tag].Flags = flags;
       PendingCMD[tag].SBDCmdIndex = g_SBDCmdIndex;

       for (i = 0; i <= MAX_CHANS; i++)
               PendingCMD[tag].ChanSync[i] = 0;

       PendingCMD[tag].Status = 0xB0B;

       return PASS;
}


#if DEBUG_SYNC || VERBOSE
/* Double check here because CheckSyncPoints also uses it */
static void pcmd_per_ch(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
               u16 tag_count, int *chIndexes)
{
       u32 i, j, chnl;

       for (i = 0; i < MAX_CHANS; i++)
               chIndexes[i] = 0;

       for (i = 0; i < (tag_count + MAX_CHANS); i++) {
               chnl = PendingCMD[i].Block /
                       (DeviceInfo.wTotalBlocks / totalUsedBanks);
               debug_boundary_error(chnl, totalUsedBanks, i);

               p[chnl][chIndexes[chnl]].Tag = PendingCMD[i].Tag;
               p[chnl][chIndexes[chnl]].CMD = PendingCMD[i].CMD;
               p[chnl][chIndexes[chnl]].DataAddr = PendingCMD[i].DataAddr;
               p[chnl][chIndexes[chnl]].Block = PendingCMD[i].Block;
               p[chnl][chIndexes[chnl]].Page = PendingCMD[i].Page;
               p[chnl][chIndexes[chnl]].DataDestAddr =
                               PendingCMD[i].DataDestAddr;
               p[chnl][chIndexes[chnl]].PageCount = PendingCMD[i].PageCount;
               p[chnl][chIndexes[chnl]].DataSrcAddr =
                               PendingCMD[i].DataSrcAddr;
               p[chnl][chIndexes[chnl]].MemCopyByteCnt =
                               PendingCMD[i].MemCopyByteCnt;
               p[chnl][chIndexes[chnl]].ChanSync[0] =
                               PendingCMD[i].ChanSync[0];
               p[chnl][chIndexes[chnl]].Status = PendingCMD[i].Status;
               chIndexes[chnl]++;

               for (j = 1; (j <= MAX_CHANS) && (PendingCMD[i].ChanSync[j]);
                                                               j++) {
                       p[chnl][chIndexes[chnl]].Tag = 0xFF;
                       p[chnl][chIndexes[chnl]].CMD = DUMMY_CMD;
                       p[chnl][chIndexes[chnl]].Block = PendingCMD[i].Block;
                       p[chnl][chIndexes[chnl]].ChanSync[0] =
                                       PendingCMD[i].ChanSync[j];
                       chIndexes[chnl]++;
               }
       }
}
#endif

#if VERBOSE
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     PrintPendingCMDs
* Inputs:       none
* Outputs:      none
* Description:  prints the PendingCMDs array
*               number of elements to print needs manual control
*               to keep it small
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void PrintPendingCMDs(u16 tag_count)
{
       u16  i;
       u16  not_print;

       nand_dbg_print(NAND_DBG_DEBUG, "Printing PendingCMDs Table\n");
       nand_dbg_print(NAND_DBG_DEBUG, "-------------------------------"
               "------------------------------------------|\n");
       nand_dbg_print(NAND_DBG_DEBUG, "           | Cache  |     Flash      "
               "|        MemCopy       |        |    |\n");
       nand_dbg_print(NAND_DBG_DEBUG,
               "Tag Command DataAddr Block Page PgCnt DestAddr SrcAddr  "
               "BCnt ChanSync Stat|\n");

       for (i = 0; i < (tag_count + MAX_CHANS); i++) {
               not_print = 0;

               switch (PendingCMD[i].CMD) {
               case ERASE_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, " ERASE  ");
                       break;
               case WRITE_MAIN_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, " WRITE  ");
                       break;
               case WRITE_MAIN_SPARE_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " WRITE MAIN+SPARE  ");
                       break;
               case READ_MAIN_SPARE_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " WRITE MAIN+SPARE  ");
                       break;
               case READ_MAIN_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, " READ   ");
                       break;
               case MEMCOPY_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, " MemCpy ");
                       break;
               case DUMMY_CMD:
                       nand_dbg_print(NAND_DBG_DEBUG, "%03d",
                                       PendingCMD[i].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, "  DUMMY ");
                       break;
               default:
                       if (i)
                               not_print = 1;
               }

               if (!not_print) {
                       nand_dbg_print(NAND_DBG_DEBUG, " %p",
                                       PendingCMD[i].DataAddr);
                       nand_dbg_print(NAND_DBG_DEBUG, "  %04X",
                                       (unsigned int)PendingCMD[i].Block);
                       nand_dbg_print(NAND_DBG_DEBUG, " %04X",
                                       PendingCMD[i].Page);
                       nand_dbg_print(NAND_DBG_DEBUG, " %04X",
                                       PendingCMD[i].PageCount);
                       nand_dbg_print(NAND_DBG_DEBUG, "  %p",
                                       PendingCMD[i].DataDestAddr);
                       nand_dbg_print(NAND_DBG_DEBUG, " %p",
                                       PendingCMD[i].DataSrcAddr);
                       nand_dbg_print(NAND_DBG_DEBUG, " %04X",
                                       PendingCMD[i].MemCopyByteCnt);
                       nand_dbg_print(NAND_DBG_DEBUG, " %04X",
                                       PendingCMD[i].ChanSync[0]);
                       nand_dbg_print(NAND_DBG_DEBUG, " %04X",
                                       PendingCMD[i].Status);
                       nand_dbg_print(NAND_DBG_DEBUG, "|\n");
               }
       }

       nand_dbg_print(NAND_DBG_DEBUG, " ----------------------------"
               "---------------------------------------------|\n");
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     PrintPendingCMDsPerChannel
* Inputs:       none
* Outputs:      none
* Description:  prints the PendingCMDs array on a per channel basis
*               number of elements to print needs manual control
*               to keep it small
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void PrintPendingCMDsPerChannel(u16 tag_count)
{
       u16 i, chnl;
       u16 not_print = 0;
       struct pending_cmd p_cmd_ch[MAX_CHANS][MAX_CHANS + MAX_DESCS];
       int chIndexes[MAX_CHANS], maxChIndexes;

       pcmd_per_ch(p_cmd_ch, tag_count, chIndexes);
       nand_dbg_print(NAND_DBG_DEBUG,
               "Printing PendingCMDsPerChannel Table\n");

       for (i = 0; i < MAX_CHANS; i++)
               nand_dbg_print(NAND_DBG_DEBUG,
               " -------------------------------------|");
       nand_dbg_print(NAND_DBG_DEBUG, "\n");

       for (i = 0; i < MAX_CHANS; i++)
               nand_dbg_print(NAND_DBG_DEBUG,
                       " Ch%1d                                  |", i);
       nand_dbg_print(NAND_DBG_DEBUG, "\n");

       maxChIndexes = 0;
       for (i = 0; i < MAX_CHANS; i++) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Tag Command  FromAddr   DestAddr  Sync|");
               if (maxChIndexes < chIndexes[i])
                       maxChIndexes = chIndexes[i];
       }
       nand_dbg_print(NAND_DBG_DEBUG, "\n");

       for (i = 0; i <= maxChIndexes; i++) {
               for (chnl = 0; chnl < MAX_CHANS; chnl++) {
                       not_print = 0;
                       if (chIndexes[chnl] > i) {
                               switch (p_cmd_ch[chnl][i].CMD) {
                               case ERASE_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "%03d",
                                               p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  ERASE ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "         ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "   %04X:0000",
                                               (unsigned int)
                                               p_cmd_ch[chnl][i].Block);
                                       break;
                               case WRITE_MAIN_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "%03d",
                                               p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  WR_MN ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %p",
                                               p_cmd_ch[chnl][i].DataAddr);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %04X",
                                               (unsigned int)
                                               p_cmd_ch[chnl][i].Block);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               ":%04X",
                                               p_cmd_ch[chnl][i].Page);
                                       break;
                               case WRITE_MAIN_SPARE_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "%03d",
                                               p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               " WR_M+S ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %p",
                                               p_cmd_ch[chnl][i].DataAddr);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %04X",
                                               (unsigned int)
                                               p_cmd_ch[chnl][i].Block);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               ":%04X",
                                               p_cmd_ch[chnl][i].Page);
                                       break;
                               case READ_MAIN_SPARE_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "%03d",
                                               p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               " RD_M+S ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %04X",
                                               (unsigned int)
                                               p_cmd_ch[chnl][i].Block);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               ":%04X",
                                               p_cmd_ch[chnl][i].Page);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "  %p",
                                               p_cmd_ch[chnl][i].DataAddr);
                                       break;
                               case READ_MAIN_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "%03d",
                                               p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "   READ ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               " %04X",
                                               (unsigned int)
                                               p_cmd_ch[chnl][i].Block);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               ":%04X",
                                               p_cmd_ch[chnl][i].Page);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "   %p",
                                               p_cmd_ch[chnl][i].DataAddr);
                                       break;
                               case MEMCOPY_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "%03d",
                                       p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " MemCpy ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "  %p",
                                       p_cmd_ch[chnl][i].DataSrcAddr);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "  %p",
                                       p_cmd_ch[chnl][i].DataDestAddr);
                                       break;
                               case DUMMY_CMD:
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "%03d", p_cmd_ch[chnl][i].Tag);
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "  DUMMY ");
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       "            %04X:0000",
                                       (unsigned int)
                                       p_cmd_ch[chnl][i].Block);
                                       break;
                               default:
                                       not_print = 1;
                               }
                       } else {
                               not_print = 1;
                       }

                       if (!not_print)
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "  %04X|",
                               p_cmd_ch[chnl][i].ChanSync[0]);
                       else
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "                                      |");

                       if (chnl == MAX_CHANS - 1)
                               nand_dbg_print(NAND_DBG_DEBUG, "\n");
               }
       }

       nand_dbg_print(NAND_DBG_DEBUG, " ----------------------------"
               "---------------------------------------------|\n");
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     PrintCDMA_Descriptors
* Inputs:       none
* Outputs:      none
* Description:  prints the CDMA_Descriptors array
*               number of elements to print needs manual control
*               to keep it small
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void PrintCDMA_Descriptors(void)
{
       u16 i;
       struct cdma_descriptor *pch[MAX_CHANS];
       struct cdma_descriptor *pchTotal = NULL;
       struct memcpy_descriptor *mcpyPtr;

       char str[MAX_CHANS * 50 + 2];
       char *strp;

       for (i = 0; i < MAX_CHANS; i++) {
               pch[i] = &(cdma_desc[i][0]);
               pchTotal += (u32)pch[i];
       }

       nand_dbg_print(NAND_DBG_DEBUG,
               " Printing CDMA_Descriptors Table \n");
       nand_dbg_print(NAND_DBG_DEBUG,
               "----------------------------------------------------"
               "----------------------------------------------------"
               "----------------------------------------------------"
               "-----------------\n");
       nand_dbg_print(NAND_DBG_DEBUG,
               " CMD | FromAddr |   ToAddr | Siz | Channel | CMD | "
               "FromAddr |   ToAddr | Siz | Channel | CMD | FromAddr |   "
               "ToAddr | Siz | Channel | CMD | FromAddr |   ToAddr "
               "| Siz | Channel\n");

       while (pchTotal) {
               pchTotal = NULL;
               for (i = 0; i < MAX_CHANS; i++) {
                       strp = &str[i * (5 + 22 + 6 + 11)];
                       if (pch[i]) {
                               switch ((pch[i]->CommandType) >> 8) {
                               case 0x21:
                                       sprintf(strp, " FWr ");
                                       strp += 5;
                                       sprintf(strp, " 0x%04x%04x",
                                               (unsigned)pch[i]->MemAddrHi,
                                               (u16)pch[i]->MemAddrLo);
                                       strp += 11;
                                       sprintf(strp, " 0x%04x%04x",
                                               (unsigned)
                                               pch[i]->FlashPointerHi,
                                               (u16)
                                               pch[i]->FlashPointerLo);
                                       strp += 11;
                                       break;
                               case 0x20:
                                       if ((pch[i]->CommandFlags >> 10)) {
                                               sprintf(strp, " Mcp ");
                                               strp += 5;
                                               mcpyPtr =
                                               (struct memcpy_descriptor *)
                                       ((pch[i]->MemCopyPointerHi << 16) |
                                       pch[i]->MemCopyPointerLo);
                                               sprintf(strp, " 0x%04x%04x",
                                               (unsigned)mcpyPtr->SrcAddrHi,
                                               (u16)mcpyPtr->SrcAddrLo);
                                               strp += 11;
                                               sprintf(strp, " 0x%04x%04x",
                                               (unsigned)mcpyPtr->DestAddrHi,
                                               (u16)mcpyPtr->DestAddrLo);
                                               strp += 11;
                                       } else {
                                               sprintf(strp, " FRd ");
                                               strp += 5;
                                               sprintf(strp, " 0x%04x%04x",
                                               (unsigned)
                                               pch[i]->FlashPointerHi,
                                               (u16)
                                               pch[i]->FlashPointerLo);
                                               strp += 11;
                                               sprintf(strp, " 0x%04x%04x",
                                               (unsigned)pch[i]->MemAddrHi,
                                               (u16)pch[i]->MemAddrLo);
                                               strp += 11;
                                       }
                                       break;
                               default:
                                       if (pch[i]->CommandType == 1) {
                                               sprintf(strp, " Ers ");
                                               strp += 5;
                                       } else {
                                               sprintf(strp, " INV ");
                                               strp += 5;
                                       }
                                       sprintf(strp,
                                       "                          ");
                                       strp += 22;
                                       break;
                               }

                               sprintf(strp, "  %3d ",
                                       (int)(pch[i]->CommandType & 0xFFF));
                               strp += 6;
                               sprintf(strp, "  0x%04x ||",
                                       (unsigned)pch[i]->Channel);
                               strp += 11;

                               pch[i] = (struct cdma_descriptor *)
                                       ((pch[i]->NxtPointerHi << 16) |
                                       pch[i]->NxtPointerLo);
                               pchTotal += (u32)pch[i];
                       } else {
                               sprintf(strp,
                               "                                       |");
                               strp += 44;
                       }
               }

               sprintf(strp, "\n");
               nand_dbg_print(NAND_DBG_DEBUG, "%s", str);
       }
       nand_dbg_print(NAND_DBG_DEBUG,
               " ---------------------------------------------------"
               "----------------------|\n");
}
#endif

static u32 calc_next_desc_ptr(u16 c, u16 d)
{
       u32 offset, addr;

       offset = sizeof(struct cdma_descriptor) *
               (c * MAX_DESC_PER_CHANNEL + d + 1);
       addr = (unsigned long)cdma_desc + offset;

       return (unsigned long)GLOB_MEMMAP_TOBUS((u32 *)addr);
}

static u32 calc_desc_ptr(u16 c)
{
       u32 offset, addr ;

       offset = sizeof(struct cdma_descriptor) * c * MAX_DESC_PER_CHANNEL;
       addr = (u32)GLOB_MEMMAP_TOBUS((u32 *)cdma_desc) + offset;

       return addr;
}

/* Reset cdma_desc d in channel c to 0 */
static void reset_cdma_desc(u16 c, u16 d)
{
       cdma_desc[c][d].NxtPointerHi = 0;
       cdma_desc[c][d].NxtPointerLo = 0;
       cdma_desc[c][d].FlashPointerHi = 0;
       cdma_desc[c][d].FlashPointerLo = 0;
       cdma_desc[c][d].CommandType = 0;
       cdma_desc[c][d].MemAddrHi = 0;
       cdma_desc[c][d].MemAddrLo = 0;
       cdma_desc[c][d].CommandFlags = 0;
       cdma_desc[c][d].Channel = 0;
       cdma_desc[c][d].Status = 0;
       cdma_desc[c][d].MemCopyPointerHi = 0;
       cdma_desc[c][d].MemCopyPointerLo = 0;
       cdma_desc[c][d].Tag = 0;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_AddDummyDesc
* Inputs:       Channel number
* Outputs:      None
* Description:  This function adds a dummy descriptor at the descriptor
*               location (from dcount structure) in the given channel.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void     CDMA_AddDummyDesc(u16 channel)
{
       u16 c, d;
       u32 *ptr;
       u32 cont;
       unsigned long next_ptr;

       c = channel;
       d = dcount[c];

       debug_boundary_error(d, MAX_DESC_PER_CHANNEL, 0);

       reset_cdma_desc(c, d);

       next_ptr = calc_next_desc_ptr(c, d);
       cdma_desc[c][d].NxtPointerHi = next_ptr >> 16;
       cdma_desc[c][d].NxtPointerLo = next_ptr;

       ptr = (u32 *)(u32)(MODE_10 | (c << 24));
       cdma_desc[c][d].FlashPointerHi = (u32)((u32)ptr >> 16);
       cdma_desc[c][d].FlashPointerLo = (u32)ptr;

       cdma_desc[c][d].CommandType = 0x42;

       cont = 1;
       cdma_desc[c][d].CommandFlags = (0 << 10) | (cont << 9) |
                                               (0 << 8) | 0x40;

       cdma_desc[c][d].Status = 0;
       cdma_desc[c][d].Tag  = 0xFF;

       return;
}


#if FLASH_ESL
#else
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_AddDummyDescAtEnd
* Inputs:       Channel number
* Outputs:      None
* Description:  This function adds a dummy descriptor at the end of the
*               descriptor chain for the given channel.
*               The purpose of these descriptors is to get a single
*               interrupt on cmd dma chain completion using sync.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void CDMA_AddDummyDescAtEnd(u16 channel)
{
       u16 c, d;
       u32 *ptr;
       u32 cont;

       c = channel;
       d = dcount[c];
       debug_boundary_error(d, MAX_DESC_PER_CHANNEL, 0);

       reset_cdma_desc(c, d);

       ptr = (u32 *)(u32)(MODE_10 | (c << 24));
       cdma_desc[c][d].FlashPointerHi = (u32)((u32)ptr >> 16);
       cdma_desc[c][d].FlashPointerLo = (u32)ptr;

       cdma_desc[c][d].CommandType = 0xFFFF;

       cont = 0;
       cdma_desc[c][d].CommandFlags = (0 << 10) | (cont << 9) |
                                               (1 << 8) | 0x40;

       cdma_desc[c][d].Channel = ((1 << 15) | (1 << 14) |
                               (c << CHANNEL_ID_OFFSET) |
                               ((GLOB_valid_banks[3] << 7) |
                               (GLOB_valid_banks[2] << 6) |
                               (GLOB_valid_banks[1] << 5) |
                               (GLOB_valid_banks[0] << 4)) | 0);

       cdma_desc[c][d].Status = 0;
       cdma_desc[c][d].Tag = 0xFF;

       return;
}

u32 CDMA_Memory_Pool_Size(void)
{
       return (sizeof(struct cdma_descriptor) * MAX_CHANS *
               MAX_DESC_PER_CHANNEL) +
               (sizeof(struct memcpy_descriptor) * MAX_CHANS *
               MAX_DESCS) + 6;
}

int CDMA_Mem_Config(u8 *pMem)
{
       ALIGN_DWORD_FWD(pMem);
       cdma_desc = (struct cdma_descriptor (*)[MAX_DESC_PER_CHANNEL])pMem;
       pMem += (sizeof(struct cdma_descriptor) * MAX_CHANS *
               MAX_DESC_PER_CHANNEL);
       ALIGN_DWORD_FWD(pMem);
       memcp_desc = (struct memcpy_descriptor (*)[MAX_DESCS])pMem;

       return PASS;
}

#if FLASH_CDMA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Flash_Init
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  This should be called at power up.
*               It disables interrupts and clears status bits
*               issues flash reset command
*               configures the controller registers
*               It sets the interrupt mask and enables interrupts
*               It pre-builds special descriptors
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_Flash_Init(void)
{
       u16 i, j;
       u16 int_en_mask;
       u16 cdma_int_en_mask;

       NAND_Flash_Reset();

       /* Set the global Enable masks for only those interrupts
        * that are supported */
       cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                       DMA_INTR__DESC_COMP_CHANNEL1 |
                       DMA_INTR__DESC_COMP_CHANNEL2 |
                       DMA_INTR__DESC_COMP_CHANNEL3 |
                       DMA_INTR__MEMCOPY_DESC_COMP);

       int_en_mask = (INTR_STATUS0__ECC_ERR |
               INTR_STATUS0__PROGRAM_FAIL |
               INTR_STATUS0__ERASE_FAIL);

       /* Disable all interrupts */
       iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);
       iowrite32(0, FlashReg + INTR_EN0);
       iowrite32(0, FlashReg + INTR_EN1);
       iowrite32(0, FlashReg + INTR_EN2);
       iowrite32(0, FlashReg + INTR_EN3);

       /* Clear all status bits */
       iowrite32(0xFFFF, FlashReg + INTR_STATUS0);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS1);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS2);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS3);

       iowrite32(0, FlashReg + DMA_INTR_EN);
       iowrite32(0xFFFF, FlashReg + DMA_INTR);

       iowrite32(cdma_int_en_mask, FlashReg + DMA_INTR_EN);

       iowrite32(int_en_mask, FlashReg + INTR_EN0);
       iowrite32(int_en_mask, FlashReg + INTR_EN1);
       iowrite32(int_en_mask, FlashReg + INTR_EN2);
       iowrite32(int_en_mask, FlashReg + INTR_EN3);

       /* Enable global interrupt to host */
       iowrite32(GLOBAL_INT_EN_FLAG, FlashReg + GLOBAL_INT_ENABLE);

       /* clear the pending CMD array */
       for (i = 0; i < (MAX_DESCS + MAX_CHANS); i++) {
               PendingCMD[i].CMD = 0;
               PendingCMD[i].Tag = 0;
               PendingCMD[i].DataAddr = 0;
               PendingCMD[i].Block = 0;
               PendingCMD[i].Page = 0;
               PendingCMD[i].PageCount = 0;
               PendingCMD[i].DataDestAddr = 0;
               PendingCMD[i].DataSrcAddr = 0;
               PendingCMD[i].MemCopyByteCnt = 0;

               for (j = 0; j <= MAX_CHANS; j++)
                       PendingCMD[i].ChanSync[j] = 0;

               PendingCMD[i].Status = 0;
               PendingCMD[i].SBDCmdIndex = 0;
       }

       return PASS;
}

static u16 abort_chnl_helper(u16 ch)
{
       u16 desc;

       for (desc = 0; desc < dcount[ch]; desc++) {
               if ((cdma_desc[ch][desc].Status & CMD_DMA_DESC_COMP) !=
                               CMD_DMA_DESC_COMP) {
                       if (cdma_desc[ch][desc].Tag != 0xFF)
                               PendingCMD[cdma_desc[ch][desc].Tag].Status =
                                                               CMD_PASS;
                       break;
               } else {
                       if (cdma_desc[ch][desc].Tag != 0xFF)
                               PendingCMD[cdma_desc[ch][desc].Tag].Status =
                                                               CMD_PASS;
               }
       }

       return desc;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_AbortChannels
* Inputs:       channel with failed descriptor
* Outputs:      PASS/ FAIL status
* Description:  This function is called to Abort all the other active channels
*               when a channel gets an error.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16  CDMA_AbortChannels(u16 chan)
{
       u16  c, d;
       u16  aborts_comp;
       u16  DescB4Abort[MAX_CHANS];
       u16  status = PASS;
       u32  active_chnl = 0;

       debug_boundary_error(chan, totalUsedBanks, 0);

       /* If status not complete, Abort the channel */
       for (c = 0; c < MAX_CHANS; c++) {
               /* Initialize the descriptor to be aborted */
               DescB4Abort[c] = 0xFF;
               if ((c != chan) && (1 == GLOB_valid_banks[c])) {
                       d = abort_chnl_helper(c);
                       if ((ioread32(FlashReg + CHNL_ACTIVE) & (1 << c)) ==
                                                       (1 << c)) {
                               DescB4Abort[c] = d;
                               aborts_comp = 0;
                               iowrite32(MODE_02 | (0 << 4), FlashMem);
                               iowrite32((0xF << 4) | c, FlashMem + 0x10);
                       }
               }
       }

       /* Check if aborts (of all active channels) are done */
       while (1) {
               aborts_comp = 1;
               for (c = 0; c < MAX_CHANS; c++) {
                       if ((DescB4Abort[c] != 0xFF) && (c != chan)) {
                               if (0 == c)
                                       active_chnl = CHNL_ACTIVE__CHANNEL0;
                               else if (1 == c)
                                       active_chnl = CHNL_ACTIVE__CHANNEL1;
                               else if (2 == c)
                                       active_chnl = CHNL_ACTIVE__CHANNEL2;
                               else if (3 == c)
                                       active_chnl = CHNL_ACTIVE__CHANNEL3;

                               if (!(ioread32(FlashReg + CHNL_ACTIVE) &
                                                       active_chnl))
                                       DescB4Abort[c] = 0xFF;
                               else
                                       aborts_comp = 0;
                       }
               }

               if (1 == aborts_comp)
                       break;
       }

       ResetSyncModule();

       return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_UpdateEventStatus
* Inputs:       none
* Outputs:      none
* Description:  This function update the event status of all the channels
*               when an error condition is reported.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_UpdateEventStatus(void)
{
       u16 i, j, c, d, status = PASS;

       for (c = 0; c < MAX_CHANS; c++) {
               if (!GLOB_valid_banks[c])
                       continue;

               d = dcount[c];
               debug_boundary_error(d, MAX_DESC_PER_CHANNEL, 0);
               for (j = 0; j < d; j++) {
                       /* Check for the descriptor with failure
                       * (not just desc_complete) */
                       if (!(cdma_desc[c][j].Status & CMD_DMA_DESC_FAIL))
                               continue;

                       /* All the previous command's status for this channel
                       * must be good (no errors reported) */
                       for (i = 0; i < j; i++) {
                               if (cdma_desc[c][i].Tag != 0xFF)
                                       PendingCMD[cdma_desc[c][i].Tag].Status
                                                       = CMD_PASS;
                       }

                       status = CDMA_AbortChannels(c);

                       return status;
               }
       }

       return status;
}
#endif


static void cdma_trans(u16 chan)
{
       iowrite32(MODE_10 | (chan << 24), FlashMem);
       iowrite32((1 << 7) | chan, FlashMem + 0x10);

       iowrite32(MODE_10 | (chan << 24) |
               ((0x0FFFF & ((u32)(calc_desc_ptr(chan)) >> 16)) << 8),
               FlashMem);
       iowrite32((1 << 7) | (1 << 4) | 0, FlashMem + 0x10);

       iowrite32(MODE_10 | (chan << 24) |
               ((0x0FFFF & ((u32)(calc_desc_ptr(chan)))) << 8),
               FlashMem);
       iowrite32((1 << 7) | (1 << 5) | 0, FlashMem + 0x10);

       iowrite32(MODE_10 | (chan << 24), FlashMem);
       iowrite32((1 << 7) | (1 << 5) | (1 << 4) | 0, FlashMem + 0x10);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Execute_CMDs (for use with CMD_DMA)
* Inputs:       tag_count:  the number of pending cmds to do
* Outputs:      PASS/FAIL
* Description:  Build the SDMA chain(s) by making one CMD-DMA descriptor
*               for each pending command, start the CDMA engine, and return.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 CDMA_Execute_CMDs(u16 tag_count)
{
       u16 i, j;
       u8 cont;
       u64 flash_add;
       u32 *ptr;
       u32 mapped_addr;
       u16 status = PASS;
       u16 c, d;
       u16 tmp_c;
       unsigned long next_ptr;

       if (tag_count >= MAX_DESCS)
               return FAIL;

       c = 0;
       d = 0;

       for (c = 0; c < MAX_CHANS; c++)
               for (d = 0; d < MAX_DESC_PER_CHANNEL; d++)
                       reset_cdma_desc(c, d);

       debug_boundary_error(totalUsedBanks - 1, MAX_CHANS, 0);

       for (c = 0; c < totalUsedBanks; c++) {
               dcount[c] = 0;
               PendingCMD[c].CMD = DUMMY_CMD;
               PendingCMD[c].SBDCmdIndex = 0xFF;
               PendingCMD[c].Tag = 0xFF;
               PendingCMD[c].Block = c * (DeviceInfo.wTotalBlocks /
                               totalUsedBanks);

               for (i = 0; i <= MAX_CHANS; i++)
                       PendingCMD[c].ChanSync[i] = 0;
       }

       c = 0;

       CDMA_AddSyncPoints(tag_count);
#if DEBUG_SYNC
       CDMA_CheckSyncPoints(tag_count);
#endif

       for (i = 0; i < (tag_count + MAX_CHANS); i++) {
               if ((i >= totalUsedBanks) && (i < MAX_CHANS))
                       continue;

               if (PendingCMD[i].Block >= DeviceInfo.wTotalBlocks) {
                       PendingCMD[i].Status = CMD_NOT_DONE;
                       continue;
               }

               c = 0;
               tmp_c = PendingCMD[i].Block /
                       (DeviceInfo.wTotalBlocks / totalUsedBanks);

               debug_boundary_error(tmp_c, totalUsedBanks, 0);

               if (0 == tmp_c) {
                       c = tmp_c;
               } else {
                       for (j = 1; j < MAX_CHANS; j++) {
                               if (GLOB_valid_banks[j]) {
                                       tmp_c--;
                                       if (0 == tmp_c) {
                                               c = j;
                                               break;
                                       }
                               }
                       }
               }

               if (GLOB_valid_banks[c] == 1) {
                       d = dcount[c];
                       dcount[c]++;
               } else {
                       continue;
               }

               next_ptr = calc_next_desc_ptr(c, d);
               cdma_desc[c][d].NxtPointerHi = next_ptr >> 16;
               cdma_desc[c][d].NxtPointerLo = next_ptr;

               /* Use the Block offset within a bank */
               tmp_c = PendingCMD[i].Block /
                       (DeviceInfo.wTotalBlocks / totalUsedBanks);
               debug_boundary_error(tmp_c, totalUsedBanks, i);
               flash_add = (u64)(PendingCMD[i].Block - tmp_c *
                       (DeviceInfo.wTotalBlocks / totalUsedBanks)) *
                       DeviceInfo.wBlockDataSize +
                       (u64)(PendingCMD[i].Page) * DeviceInfo.wPageDataSize;

#if FLASH_CDMA
               ptr = (u32 *)(MODE_10 | (c << 24) |
                       (u32)GLOB_u64_Div(flash_add,
                               DeviceInfo.wPageDataSize));
               cdma_desc[c][d].FlashPointerHi = (u32)ptr >> 16;
               cdma_desc[c][d].FlashPointerLo = (u32)ptr;
#endif
               /* set continue flag except if last cmd-descriptor */
               cont = 1;

               if ((PendingCMD[i].CMD == WRITE_MAIN_SPARE_CMD) ||
                       (PendingCMD[i].CMD == READ_MAIN_SPARE_CMD)) {
                       /* Descriptor to set Main+Spare Access Mode */
                       cdma_desc[c][d].CommandType   = 0x43;
                       cdma_desc[c][d].CommandFlags  =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       cdma_desc[c][d].MemAddrHi = 0;
                       cdma_desc[c][d].MemAddrLo = 0;

                       cdma_desc[c][d].Channel = 0;
                       cdma_desc[c][d].Status = 0;
                       cdma_desc[c][d].Tag  = i;

                       dcount[c]++;
                       d++;

                       reset_cdma_desc(c, d);

                       next_ptr = calc_next_desc_ptr(c, d);
                       cdma_desc[c][d].NxtPointerHi = next_ptr >> 16;
                       cdma_desc[c][d].NxtPointerLo = next_ptr;

#if FLASH_CDMA
                       cdma_desc[c][d].FlashPointerHi = (u32)ptr >> 16;
                       cdma_desc[c][d].FlashPointerLo = (u32)ptr;
#endif
               }

               switch (PendingCMD[i].CMD) {
               case ERASE_CMD:
                       cdma_desc[c][d].CommandType = 1;
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       cdma_desc[c][d].MemAddrHi = 0;
                       cdma_desc[c][d].MemAddrLo = 0;
                       break;

               case WRITE_MAIN_CMD:
                       cdma_desc[c][d].CommandType =
                               0x2100 | PendingCMD[i].PageCount;
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataAddr);
                       cdma_desc[c][d].MemAddrHi = mapped_addr >> 16;
                       cdma_desc[c][d].MemAddrLo = mapped_addr;
                       break;

               case READ_MAIN_CMD:
                       cdma_desc[c][d].CommandType =
                               0x2000 | (PendingCMD[i].PageCount);
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataAddr);
                       cdma_desc[c][d].MemAddrHi = mapped_addr >> 16;
                       cdma_desc[c][d].MemAddrLo = mapped_addr;
                       break;

               case WRITE_MAIN_SPARE_CMD:
                       cdma_desc[c][d].CommandType =
                               0x2100 | (PendingCMD[i].PageCount);
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataAddr);
                       cdma_desc[c][d].MemAddrHi = mapped_addr >> 16;
                       cdma_desc[c][d].MemAddrLo = mapped_addr;
                       break;

               case READ_MAIN_SPARE_CMD:
                       cdma_desc[c][d].CommandType =
                               0x2000 | (PendingCMD[i].PageCount);
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataAddr);
                       cdma_desc[c][d].MemAddrHi = mapped_addr >> 16;
                       cdma_desc[c][d].MemAddrLo = mapped_addr;
                       break;

               case MEMCOPY_CMD:
                       cdma_desc[c][d].CommandType =
                               0x2000 | (PendingCMD[i].PageCount);
                       cdma_desc[c][d].CommandFlags =
                               (1 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       mapped_addr = (unsigned int)GLOB_MEMMAP_TOBUS
                               ((u32 *)&memcp_desc[c][d]);
                       cdma_desc[c][d].MemCopyPointerHi = mapped_addr >> 16;
                       cdma_desc[c][d].MemCopyPointerLo = mapped_addr;

                       memcp_desc[c][d].NxtPointerHi = 0;
                       memcp_desc[c][d].NxtPointerLo = 0;

                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataSrcAddr);
                       memcp_desc[c][d].SrcAddrHi = mapped_addr >> 16;
                       memcp_desc[c][d].SrcAddrLo = mapped_addr;
                       mapped_addr = (u32)GLOB_MEMMAP_TOBUS
                               ((u32 *)PendingCMD[i].DataDestAddr);
                       memcp_desc[c][d].DestAddrHi = mapped_addr >> 16;
                       memcp_desc[c][d].DestAddrLo = mapped_addr;

                       memcp_desc[c][d].XferSize =
                               PendingCMD[i].MemCopyByteCnt;
                       memcp_desc[c][d].MemCopyFlags =
                               (0 << 15 | 0 << 14 | 27 << 8 | 0x40);
                       memcp_desc[c][d].MemCopyStatus = 0;
                       break;

               case DUMMY_CMD:
               default:
                       cdma_desc[c][d].CommandType = 0XFFFF;
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       cdma_desc[c][d].MemAddrHi = 0;
                       cdma_desc[c][d].MemAddrLo = 0;
                       break;
               }

               cdma_desc[c][d].Channel = PendingCMD[i].ChanSync[0];
               cdma_desc[c][d].Status = 0;
               cdma_desc[c][d].Tag = i;

               for (j = 1; j <= MAX_CHANS; j++) {
                       if (PendingCMD[i].ChanSync[j]) {
                               if (1 == GLOB_valid_banks[c]) {
                                       CDMA_AddDummyDesc(c);
                                       d = dcount[c]++;
                                       cdma_desc[c][d].Channel =
                                               PendingCMD[i].ChanSync[j];
                               }
                       }
               }

               if ((PendingCMD[i].CMD == WRITE_MAIN_SPARE_CMD) ||
                       (PendingCMD[i].CMD == READ_MAIN_SPARE_CMD)) {
                       /* Descriptor to set back Main Area Access Mode */
                       dcount[c]++;
                       d++;
                       debug_boundary_error(d, MAX_DESC_PER_CHANNEL, 0);
                       next_ptr = calc_next_desc_ptr(c, d);
                       cdma_desc[c][d].NxtPointerHi = next_ptr >> 16;
                       cdma_desc[c][d].NxtPointerLo = next_ptr;
#if FLASH_CDMA
                       cdma_desc[c][d].FlashPointerHi = (u32)ptr >> 16;
                       cdma_desc[c][d].FlashPointerLo = (u32)ptr;
#endif
                       cdma_desc[c][d].CommandType = 0x42;
                       cdma_desc[c][d].CommandFlags =
                               (0 << 10) | (cont << 9) | (0 << 8) | 0x40;
                       cdma_desc[c][d].MemAddrHi = 0;
                       cdma_desc[c][d].MemAddrLo = 0;

                       cdma_desc[c][d].Channel = PendingCMD[i].ChanSync[0];
                       cdma_desc[c][d].Status = 0;
                       cdma_desc[c][d].Tag = i;
               }
       }

       for (c = 0; c < MAX_CHANS; c++) {
               if (GLOB_valid_banks[c])
                       CDMA_AddDummyDescAtEnd(c);
       }

#if FLASH_CDMA
       iowrite32(1, FlashReg + DMA_ENABLE);
       /* Wait for DMA to be enabled before issuing the next command */
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       for (c = 0; c < MAX_CHANS; c++) {
               if (!GLOB_valid_banks[c])
                       continue;
               cdma_trans(c);
       }
#endif

       return status;
}


#if FLASH_CDMA
static void ResetSyncModule(void)
{
       u16 c, d;
       u32 *ptr;
       u32 cont;
       unsigned long next_ptr;

       /* Disable all interrupts */
       iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);

       /* Clear all DMA interrupt bits before starting the chains */
       iowrite32(ioread32(FlashReg + DMA_INTR), FlashReg + DMA_INTR);

       for (c = 0; c < MAX_CHANS; c++) {
               for (d = 0; d < MAX_SYNC_POINTS; d++) {
                       reset_cdma_desc(c, d);

                       next_ptr = calc_next_desc_ptr(c, d);
                       cdma_desc[c][d].NxtPointerHi = next_ptr >> 16;
                       cdma_desc[c][d].NxtPointerLo = next_ptr;

                       ptr = (u32 *)(u32)(MODE_10 | (c << 24));
                       cdma_desc[c][d].FlashPointerHi =
                               (u32)((u32)ptr >> 16);
                       cdma_desc[c][d].FlashPointerLo =
                               (u32)ptr;

                       cdma_desc[c][d].CommandType = 0xFFFF;

                       if (d == (MAX_SYNC_POINTS - 1)) {
                               cont = 0;
                               cdma_desc[c][d].CommandFlags = (0 << 10) |
                                       (cont << 9) | (1 << 8) | 0x40;
                       } else {
                               cont = 1;
                               cdma_desc[c][d].CommandFlags = (0 << 10) |
                                       (cont << 9) | (0 << 8) | 0x40;
                       }

                       cdma_desc[c][d].Channel = ((0 << 15) | (1 << 14) |
                               (c << CHANNEL_ID_OFFSET) |
                               (1 << (4 + c)) | d);

                       cdma_desc[c][d].Status = 0;
                       cdma_desc[c][d].Tag = c * MAX_SYNC_POINTS + d;
               }
       }

       for (c = 0; c < MAX_CHANS; c++)
               cdma_trans(c);

       while ((ioread32(FlashReg + DMA_INTR) &
               (DMA_INTR__DESC_COMP_CHANNEL0 |
               DMA_INTR__DESC_COMP_CHANNEL1 |
               DMA_INTR__DESC_COMP_CHANNEL2 |
               DMA_INTR__DESC_COMP_CHANNEL3)) !=
               (DMA_INTR__DESC_COMP_CHANNEL0 |
               DMA_INTR__DESC_COMP_CHANNEL1 |
               DMA_INTR__DESC_COMP_CHANNEL2 |
               DMA_INTR__DESC_COMP_CHANNEL3))
               ;

       iowrite32(ioread32(FlashReg + DMA_INTR), FlashReg + DMA_INTR);
       iowrite32(GLOBAL_INT_EN_FLAG, FlashReg + GLOBAL_INT_ENABLE);
}

int is_cdma_interrupt(void)
{
       u32 ints_b0, ints_b1, ints_b2, ints_b3, ints_cdma;
       u32 int_en_mask;
       u32 cdma_int_en_mask;

       /* Set the global Enable masks for only those interrupts
        * that are supported */
       cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                       DMA_INTR__DESC_COMP_CHANNEL1 |
                       DMA_INTR__DESC_COMP_CHANNEL2 |
                       DMA_INTR__DESC_COMP_CHANNEL3 |
                       DMA_INTR__MEMCOPY_DESC_COMP);

       int_en_mask = (INTR_STATUS0__ECC_ERR |
               INTR_STATUS0__PROGRAM_FAIL |
               INTR_STATUS0__ERASE_FAIL);

       ints_b0 = ioread32(FlashReg + INTR_STATUS0) & int_en_mask;
       ints_b1 = ioread32(FlashReg + INTR_STATUS1) & int_en_mask;
       ints_b2 = ioread32(FlashReg + INTR_STATUS2) & int_en_mask;
       ints_b3 = ioread32(FlashReg + INTR_STATUS3) & int_en_mask;
       ints_cdma = ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask;

       if (ints_b0 || ints_b1 || ints_b2 || ints_b3 || ints_cdma) {
               nand_dbg_print(NAND_DBG_DEBUG, "NAND controller interrupt!\n"
                       "ints_bank0 to ints_bank3: 0x%x, 0x%x, 0x%x, 0x%x\n"
                       "ints_cdma: 0x%x\n",
                       ints_b0, ints_b1, ints_b2, ints_b3, ints_cdma);
               return 1;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG,
                               "Not a NAND controller interrupt!\n");
               return 0;
       }
}

static void update_event_status(void)
{
       u16 i, c, d;

       for (c = 0; c < MAX_CHANS; c++) {
               if (GLOB_valid_banks[c]) {
                       d = dcount[c];
                       debug_boundary_error(d, MAX_DESC_PER_CHANNEL, 0);
                       for (i = 0; i < d; i++) {
                               if (cdma_desc[c][i].Tag != 0xFF)
                                       PendingCMD[cdma_desc[c][i].Tag].Status
                                                       = CMD_PASS;
#if FLASH_CDMA
                               if ((cdma_desc[c][i].CommandType == 0x41) ||
                               (cdma_desc[c][i].CommandType == 0x42) ||
                               (cdma_desc[c][i].CommandType == 0x43))
                                       continue;

                               switch (PendingCMD[cdma_desc[c][i].Tag].CMD) {
                               case READ_MAIN_SPARE_CMD:
                                       Conv_Main_Spare_Data_Phy2Log_Format(
                                               PendingCMD[
                                               cdma_desc[c][i].Tag].DataAddr,
                                               PendingCMD[
                                               cdma_desc[c][i].Tag].
                                               PageCount);
                                       break;
                               case READ_SPARE_CMD:
                                       Conv_Spare_Data_Phy2Log_Format(
                                               PendingCMD[
                                               cdma_desc[c][i].Tag].
                                               DataAddr);
                                       break;
                               default:
                                       break;
                               }
#endif
                       }
               }
       }
}

static u16 do_ecc_for_desc(u16 c, u8 *buf,
                               u16 page)
{
       u16 event = EVENT_NONE;
       u16 err_byte;
       u8 err_sector;
       u8 err_page = 0;
       u8 err_device;
       u16 ecc_correction_info;
       u16 err_address;
       u32 eccSectorSize;
       u8 *err_pos;

       eccSectorSize   = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);

       do {
               if (0 == c)
                       err_page = ioread32(FlashReg + ERR_PAGE_ADDR0);
               else if (1 == c)
                       err_page = ioread32(FlashReg + ERR_PAGE_ADDR1);
               else if (2 == c)
                       err_page = ioread32(FlashReg + ERR_PAGE_ADDR2);
               else if (3 == c)
                       err_page = ioread32(FlashReg + ERR_PAGE_ADDR3);

               err_address = ioread32(FlashReg + ECC_ERROR_ADDRESS);
               err_byte = err_address & ECC_ERROR_ADDRESS__OFFSET;
               err_sector = ((err_address &
                       ECC_ERROR_ADDRESS__SECTOR_NR) >> 12);

               ecc_correction_info = ioread32(FlashReg + ERR_CORRECTION_INFO);
               err_device = ((ecc_correction_info &
                       ERR_CORRECTION_INFO__DEVICE_NR) >> 8);

               if (ecc_correction_info & ERR_CORRECTION_INFO__ERROR_TYPE) {
                       return EVENT_UNCORRECTABLE_DATA_ERROR;
               } else {
                       event = EVENT_CORRECTABLE_DATA_ERROR_FIXED;
                       if (err_byte < eccSectorSize) {
                               err_pos = buf +
                                       (err_page - page) *
                                       DeviceInfo.wPageDataSize +
                                       err_sector * eccSectorSize +
                                       err_byte *
                                       DeviceInfo.wDevicesConnected +
                                       err_device;
                               *err_pos ^= ecc_correction_info &
                                       ERR_CORRECTION_INFO__BYTEMASK;
                       }
               }
       } while (!(ecc_correction_info & ERR_CORRECTION_INFO__LAST_ERR_INFO));

       return event;
}

static u16 process_ecc_int(u16 c,
       u16 *fiqs, u16 *i)
{
       u16 d, j, event;
       u16 ints;
       u16 cdma_int_en_mask;

       event = EVENT_PASS;
       d = dcount[c];

       for (j = 0; j < d; j++) {
               if ((cdma_desc[c][j].Status & CMD_DMA_DESC_COMP) !=
                       CMD_DMA_DESC_COMP)
                       break;
       }

       *i = j; /* Pass the descripter number found here */

       if (j == d)
               return EVENT_UNCORRECTABLE_DATA_ERROR;

       event = do_ecc_for_desc(c, PendingCMD[cdma_desc[c][j].Tag].DataAddr,
                       PendingCMD[cdma_desc[c][j].Tag].Page);

       if (EVENT_UNCORRECTABLE_DATA_ERROR == event) {
               if (cdma_desc[c][j].Tag != 0xFF)
                       PendingCMD[cdma_desc[c][j].Tag].Status = CMD_FAIL;
               CDMA_UpdateEventStatus();

               iowrite32(0, FlashReg + DMA_ENABLE);
               while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;

               iowrite32(fiqs[0], FlashReg + INTR_STATUS0);
               iowrite32(fiqs[1], FlashReg + INTR_STATUS1);
               iowrite32(fiqs[2], FlashReg + INTR_STATUS2);
               iowrite32(fiqs[3], FlashReg + INTR_STATUS3);

               cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                               DMA_INTR__DESC_COMP_CHANNEL1 |
                               DMA_INTR__DESC_COMP_CHANNEL2 |
                               DMA_INTR__DESC_COMP_CHANNEL3 |
                               DMA_INTR__MEMCOPY_DESC_COMP);

               ints = (ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask);
               iowrite32(ints, FlashReg + DMA_INTR);

               return event;
       }

       if (0 == c)
               iowrite32(INTR_STATUS0__ECC_ERR, FlashReg + INTR_STATUS0);
       else if (1 == c)
               iowrite32(INTR_STATUS1__ECC_ERR, FlashReg + INTR_STATUS1);
       else if (2 == c)
               iowrite32(INTR_STATUS2__ECC_ERR, FlashReg + INTR_STATUS2);
       else if (3 == c)
               iowrite32(INTR_STATUS3__ECC_ERR, FlashReg + INTR_STATUS3);

       return event;
}

static void process_prog_fail_int(u16 c,
       u16 *fiqs, u16 *i)
{
       u16 ints;
       u16 cdma_int_en_mask;

       if (cdma_desc[c][*i].Tag != 0xFF)
               PendingCMD[cdma_desc[c][*i].Tag].Status = CMD_FAIL;

       CDMA_UpdateEventStatus();

       iowrite32(0, FlashReg + DMA_ENABLE);
       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(fiqs[0], FlashReg + INTR_STATUS0);
       iowrite32(fiqs[1], FlashReg + INTR_STATUS1);
       iowrite32(fiqs[2], FlashReg + INTR_STATUS2);
       iowrite32(fiqs[3], FlashReg + INTR_STATUS3);

       cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                       DMA_INTR__DESC_COMP_CHANNEL1 |
                       DMA_INTR__DESC_COMP_CHANNEL2 |
                       DMA_INTR__DESC_COMP_CHANNEL3 |
                       DMA_INTR__MEMCOPY_DESC_COMP);

       ints = (ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask);
       iowrite32(ints, FlashReg + DMA_INTR);
}

static void process_erase_fail_int(u16 c,
       u16 *fiqs, u16 *i)
{
       u16 ints;
       u16 cdma_int_en_mask;

       if (cdma_desc[c][*i].Tag != 0xFF)
               PendingCMD[cdma_desc[c][*i].Tag].Status = CMD_FAIL;

       CDMA_UpdateEventStatus();

       iowrite32(0, FlashReg + DMA_ENABLE);
       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(fiqs[0], FlashReg + INTR_STATUS0);
       iowrite32(fiqs[1], FlashReg + INTR_STATUS1);
       iowrite32(fiqs[2], FlashReg + INTR_STATUS2);
       iowrite32(fiqs[3], FlashReg + INTR_STATUS3);

       cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                       DMA_INTR__DESC_COMP_CHANNEL1 |
                       DMA_INTR__DESC_COMP_CHANNEL2 |
                       DMA_INTR__DESC_COMP_CHANNEL3 |
                       DMA_INTR__MEMCOPY_DESC_COMP);

       ints = (ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask);
       iowrite32(ints, FlashReg + DMA_INTR);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Event_Status (for use with CMD_DMA)
* Inputs:       none
* Outputs:      Event_Status code
* Description:  This function is called after an interrupt has happened
*               It reads the HW status register and ...tbd
*               It returns the appropriate event status
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16  CDMA_Event_Status(void)
{
       u16 FIQstatus[MAX_CHANS];
       u16 int_status, event;
       u16 c, i = 0;
       u16 int_en_mask;
       u16 cdma_int_en_mask;

       event = EVENT_PASS;

       /* Set the global Enable masks for only those interrupts
        * that are supported */
       cdma_int_en_mask = (DMA_INTR__DESC_COMP_CHANNEL0 |
                       DMA_INTR__DESC_COMP_CHANNEL1 |
                       DMA_INTR__DESC_COMP_CHANNEL2 |
                       DMA_INTR__DESC_COMP_CHANNEL3 |
                       DMA_INTR__MEMCOPY_DESC_COMP);

       int_en_mask = (INTR_STATUS0__ECC_ERR |
               INTR_STATUS0__PROGRAM_FAIL |
               INTR_STATUS0__ERASE_FAIL);

       FIQstatus[0] = (ioread32(FlashReg + INTR_STATUS0) & int_en_mask);
       FIQstatus[1] = (ioread32(FlashReg + INTR_STATUS1) & int_en_mask);
       FIQstatus[2] = (ioread32(FlashReg + INTR_STATUS2) & int_en_mask);
       FIQstatus[3] = (ioread32(FlashReg + INTR_STATUS3) & int_en_mask);

       int_status = ioread32(FlashReg + DMA_INTR) & cdma_int_en_mask;

       if (int_status) {
               if ((int_status & DMA_INTR__DESC_COMP_CHANNEL0) ||
               (int_status & DMA_INTR__DESC_COMP_CHANNEL1) ||
               (int_status & DMA_INTR__DESC_COMP_CHANNEL2) ||
               (int_status & DMA_INTR__DESC_COMP_CHANNEL3)) {

                       event = EVENT_PASS;
                       update_event_status();
               } else {
                       /* TODO -- What kind of status can be
                       * reported back to FTL in PendindCMD? */
                       event = EVENT_DMA_CMD_FAIL;
               }

               iowrite32(0, FlashReg + DMA_ENABLE);
               while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;

               iowrite32(int_status, FlashReg + DMA_INTR);
       }

       for (c = 0; c < MAX_CHANS; c++) {
               if (FIQstatus[c]) {
                       if ((FIQstatus[c] & INTR_STATUS0__ECC_ERR) &&
                               ioread32(FlashReg + ECC_ENABLE)) {
                               event = process_ecc_int(c, FIQstatus, &i);
                               if (EVENT_UNCORRECTABLE_DATA_ERROR == event)
                                       return event;
                       }

                       if (FIQstatus[c] & INTR_STATUS0__PROGRAM_FAIL) {
                               process_prog_fail_int(c, FIQstatus, &i);
                               return EVENT_PROGRAM_FAILURE;
                       }

                       if (FIQstatus[c] & INTR_STATUS0__ERASE_FAIL) {
                               process_erase_fail_int(c, FIQstatus, &i);
                               return EVENT_ERASE_FAILURE;
                       } else {
                               if (0 == c)
                                       iowrite32(FIQstatus[0],
                                               FlashReg + INTR_STATUS0);
                               else if (1 == c)
                                       iowrite32(FIQstatus[1],
                                               FlashReg + INTR_STATUS1);
                               else if (2 == c)
                                       iowrite32(FIQstatus[2],
                                               FlashReg + INTR_STATUS2);
                               else if (3 == c)
                                       iowrite32(FIQstatus[3],
                                               FlashReg + INTR_STATUS3);
                       }
               }
       }

       return event;
}
#endif

#endif

/****** Sync related functions ********/
#define MAX_SYNC                           14
#define FORCED_ORDERED_SYNC    15
#define SNUS_CHAN_OFFSET           24
#define SNUS_LASTID_MASK           0xFFFFFF

#if DEBUG_SYNC
u32  debug_sync_cnt = 1;
#endif

static u32 isFlashReadCMD(u8 CMD)
{
       switch (CMD) {
       case READ_MAIN_CMD:
       case READ_SPARE_CMD:
       case READ_MAIN_SPARE_CMD:
               return 1;
       default:
               return 0;
       }
}

static u32 isFlashWriteCMD(u8 CMD)
{
       switch (CMD) {
       case WRITE_MAIN_CMD:
       case WRITE_SPARE_CMD:
       case WRITE_MAIN_SPARE_CMD:
               return 1;
       default:
               return 0;
       }
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     generateSyncNum
* Inputs:       sync_usage array, a new sync number in case no reusable one
*               was found. The bit vector of channels taking place in current
*               sync operation, and the earliest cmd id for the new sync op.
* Outputs:      The sync number to be used for the current syncing
* Description:
* Assumption :  A sync point is always used between 2 and only 2 channels.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 generateSyncNum(u32 *sync_usage, u32 *newSyncNum,
                       u32 syncedChans, u32 lastid)
{
       u32 synci, toUseSyncNum = 0;

       /* We try to reuse syncs as much as possible with this algorithm */
       for (synci = 1; synci < *newSyncNum; synci++) {
               if (((sync_usage[synci] >> SNUS_CHAN_OFFSET) == syncedChans)
                       && ((sync_usage[synci] & SNUS_LASTID_MASK)
                       < lastid)) {
                       toUseSyncNum = synci;
                       break;
               }
       }

       if (!toUseSyncNum && (*newSyncNum <= MAX_SYNC))
               toUseSyncNum = (*newSyncNum)++;

/*
    The rest is to find another sync point which has at least
    one channel in common, and then add a sync point to
    the extra channel, and then use it.

    -- This will not result in the sync number being used
    by 3 channels, since the new use will have just the
    syncedChans values. So our assumption still holds valid.

    -- However, adding the new channel is not easy.
    We need to find the id, which is after the last sync number
    that existed between the common channel, and our new
    channel, and before the next sync that will exist
    between the new and common channel!
*/

       return toUseSyncNum;
}


#define getChannelPendingCMD(idx)  (PendingCMD[idx].Block /\
       (DeviceInfo.wTotalBlocks / totalUsedBanks))

#define isOrderedPendingCMD(idx)  ((PendingCMD[idx].Flags &\
       LLD_CMD_FLAG_ORDER_BEFORE_REST) != 0)

#define getSyncFromChannel(c)                   ((c & CHANNEL_SYNC_MASK) >>\
       CHANNEL_SYNC_OFFSET)
#define getIdFromChannel(c)                     ((c & CHANNEL_ID_MASK) >>\
       CHANNEL_ID_OFFSET)
#define getContFromChannel(c)                   ((c & CHANNEL_CONT_MASK) >>\
       CHANNEL_CONT_OFFSET)
#define getIntrFromChannel(c)                   ((c & CHANNEL_INTR_MASK) >>\
       CHANNEL_INTR_OFFSET)
#define getChanFromChannel(c)                   ((c & CHANNEL_DMA_MASK) >>\
       CHANNEL_DMA_OFFSET)

#define putSyncInChannel(c, v)    (c |= ((v << CHANNEL_SYNC_OFFSET) &\
       CHANNEL_SYNC_MASK))
#define putIdInChannel(c, v)        (c |= ((v << CHANNEL_ID_OFFSET) &\
       CHANNEL_ID_MASK))
#define putContInChannel(c, v)    (c |= ((v << CHANNEL_CONT_OFFSET) &\
       CHANNEL_CONT_MASK))
#define putIntrInChannel(c, v)    (c |= ((v << CHANNEL_INTR_OFFSET) &\
       CHANNEL_INTR_MASK))
#define putChanInChannel(c, v)    (c |= ((v << CHANNEL_DMA_OFFSET) &\
       CHANNEL_DMA_MASK))

#define addChanToChannel(c, v)    (c |= ((1 << CHANNEL_DMA_OFFSET) << v))

#define isWithinRange(toChk, Addr, Bytes)    ((toChk >= Addr) &&\
       (toChk < (Addr + Bytes)))

struct add_sync_points_struct {
       u8 *fromAddr, *toAddr;
       u8 CMD;
       u32 idx;
       u32 numSync, numSyncOther;
       u32 chnl, chnlOther;
       u32 newSyncNum, writeOpSyncPlaced;
       u32 indx_last_cmd[MAX_CHANS];
       u32 namb[MAX_CHANS][MAX_CHANS];
       u32 sync_usage[MAX_SYNC + 1];
};

static void process_memcpy(struct add_sync_points_struct *ptr)
{
       int i, stopLoop, within1, within2, condition;
       u8 *data_addr;
       unsigned long offset;

       ptr->fromAddr = PendingCMD[ptr->idx].DataSrcAddr;
       ptr->toAddr = PendingCMD[ptr->idx].DataDestAddr;
       stopLoop = 0;

       for (i = ptr->idx - 1; (i >= MAX_CHANS) && !stopLoop; i--) {
               data_addr = PendingCMD[i].DataAddr;
               offset = PendingCMD[i].PageCount * DeviceInfo.wPageDataSize;
               within1 = isWithinRange(ptr->toAddr, data_addr, offset);
               within2 = isWithinRange(ptr->fromAddr, data_addr, offset);
               condition = (PendingCMD[i].CMD != MEMCOPY_CMD) &&
                       (PendingCMD[i].CMD != ERASE_CMD) &&
                       (within1 || within2);
               if (condition) {
                       stopLoop = 1;
                       PendingCMD[ptr->idx].Block = PendingCMD[i].Block;
                       ptr->chnl = getChannelPendingCMD(ptr->idx);
                       debug_boundary_error(ptr->chnl, totalUsedBanks,
                               ptr->idx);
                       if (isFlashWriteCMD(PendingCMD[i].CMD) && within1) {
                               ptr->CMD = READ_MAIN_CMD;
                               PendingCMD[ptr->idx].DataAddr = ptr->toAddr;
                       }
               }
       }
}

static void check_synced_helper(struct add_sync_points_struct *ptr,
                               int j, int k)
{
       int l;
       unsigned long m, n;

       m = ptr->chnl;
       n = ptr->chnlOther;

       for (l = 0; l < totalUsedBanks; l++) {
               if ((l != m) && (l != n)) {
                       if (ptr->namb[l][n] <= j) {
                               if (ptr->namb[m][l] < ptr->namb[n][l])
                                       ptr->namb[m][l] = ptr->namb[n][l];
                       } else {
                               if (ptr->namb[l][m] < ptr->namb[n][m])
                                       ptr->namb[l][m] = ptr->namb[n][m];
                       }

                       if (ptr->namb[l][m] <= k) {
                               if (ptr->namb[n][l] < ptr->namb[m][l])
                                       ptr->namb[n][l] = ptr->namb[m][l];
                       } else {
                               if (ptr->namb[l][n] < ptr->namb[m][n])
                                       ptr->namb[l][n] = ptr->namb[m][n];
                       }
               }
       }
}

#if DEBUG_SYNC
static void check_synced_debug_sync(struct add_sync_points_struct *ptr,
       unsigned long toUseSyncNum, unsigned long syncedChans, int j, int k)
{
       int m, n;

       if (!(debug_sync_cnt % DBG_SNC_PRINTEVERY)) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "ADDSYNC: Placed Sync point 0x%x "
                       "with chanvectors 0x%x "
                       "betn tags %d & prev(%d)=%d\n",
                       (unsigned)toUseSyncNum,
                       (unsigned)syncedChans,
                       j - MAX_CHANS,
                       ptr->idx - MAX_CHANS,
                       k - MAX_CHANS);
               for (m = 0; m < totalUsedBanks; m++) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "ADDSYNC: ch:%d ->", m);
                       for (n = 0; n < totalUsedBanks; n++)
                               if (255 == PendingCMD[ptr->namb[m][n]].Tag)
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " (ch:%d tag: -1)", n);
                               else
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " (ch:%d tag:%3d)", n,
                                       PendingCMD[ptr->namb[m][n]].Tag);
                       nand_dbg_print(NAND_DBG_DEBUG, "\n");
               }
       }
}
#endif

static void check_synced(struct add_sync_points_struct *ptr, int j, int k)
{
       unsigned long syncedChans, toUseSyncNum;

       for (ptr->numSync = 0;
       (ptr->numSync <= MAX_CHANS) &&
       (PendingCMD[k].ChanSync[ptr->numSync] & CHANNEL_DMA_MASK);
       ptr->numSync++)
               ;

       for (ptr->numSyncOther = 0;
       (ptr->numSyncOther <= MAX_CHANS) &&
       (PendingCMD[j].ChanSync[ptr->numSyncOther] & CHANNEL_DMA_MASK);
       ptr->numSyncOther++)
               ;

       if ((ptr->numSync > MAX_CHANS) ||
               (ptr->numSyncOther > MAX_CHANS)) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "LLD_CDMA: Sync Algorithm failed to place a Sync "
                       "between command tags %d and %d\n",
                       ptr->idx - MAX_CHANS,
                       j - MAX_CHANS);
       } else {
               ptr->writeOpSyncPlaced |= (1 << ptr->chnlOther);
               syncedChans = ((1 << ptr->chnl) | (1 << ptr->chnlOther));
               toUseSyncNum = generateSyncNum(&ptr->sync_usage[0],
                       &ptr->newSyncNum, syncedChans, (j < k ? j : k));
               if (!toUseSyncNum) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "LLD_CDMA: Sync Algorithm ran out of Syncs "
                               "during syncing command tags %d and %d\n",
                               ptr->idx - MAX_CHANS,
                               j - MAX_CHANS);
               } else {
                       putSyncInChannel(
                               PendingCMD[k].ChanSync[ptr->numSync],
                               toUseSyncNum);
                       putContInChannel(
                               PendingCMD[k].ChanSync[ptr->numSync],
                               1);
                       putIdInChannel(
                               PendingCMD[k].ChanSync[ptr->numSync],
                               ptr->chnl);
                       putSyncInChannel(
                               PendingCMD[j].ChanSync[ptr->numSyncOther],
                               toUseSyncNum);
                       putContInChannel(
                               PendingCMD[j].ChanSync[ptr->numSyncOther],
                               1);
                       putIdInChannel(
                               PendingCMD[j].ChanSync[ptr->numSyncOther],
                               ptr->chnlOther);
                       putChanInChannel(
                               PendingCMD[j].ChanSync[ptr->numSyncOther],
                               syncedChans);
                       putChanInChannel(
                               PendingCMD[k].ChanSync[ptr->numSync],
                               syncedChans);

                       ptr->sync_usage[toUseSyncNum] =
                               (syncedChans << SNUS_CHAN_OFFSET) |
                               ((j > k ? j : k) & SNUS_LASTID_MASK);

                       ptr->namb[ptr->chnl][ptr->chnlOther] = j;

                       if (ptr->namb[ptr->chnlOther][ptr->chnl] > k)
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "LLD_CDMA: Sync Algorithm detected "
                               "a possible deadlock in its assignments.\n");
                       else
                               ptr->namb[ptr->chnlOther][ptr->chnl] = k;

                       check_synced_helper(ptr, j, k);

#if DEBUG_SYNC
                       check_synced_debug_sync(ptr, toUseSyncNum,
                                       syncedChans, j, k);
#endif
               }
       }
}

static void process_flash_rw(struct add_sync_points_struct *ptr)
{
       int j, k, stopLoop, within1, within2, condition;
       unsigned long offset;

       ptr->fromAddr = PendingCMD[ptr->idx].DataAddr;
       k = ptr->indx_last_cmd[ptr->chnl];
       offset = PendingCMD[ptr->idx].PageCount * DeviceInfo.wPageDataSize;
       stopLoop = 0;

       for (j = ptr->idx - 1; (j >= MAX_CHANS) && !stopLoop; j--) {
               ptr->chnlOther = getChannelPendingCMD(j);
               debug_boundary_error(ptr->chnlOther, totalUsedBanks, j);
               within1 = isWithinRange(PendingCMD[j].DataDestAddr,
                               ptr->fromAddr, offset);
               within2 = isWithinRange(PendingCMD[j].DataSrcAddr,
                               ptr->fromAddr, offset);
               condition = (ptr->fromAddr == PendingCMD[j].DataAddr) ||
                       ((PendingCMD[j].CMD == MEMCOPY_CMD) &&
                       (within1 || within2));
               if (condition) {
                       if (ptr->namb[ptr->chnl][ptr->chnlOther] >= j) {
                               stopLoop = 1;
                       } else if (ptr->chnlOther == ptr->chnl) {
                               condition = isFlashWriteCMD(ptr->CMD) ||
                                       isFlashReadCMD(PendingCMD[j].CMD) ||
                                       ((PendingCMD[j].CMD == MEMCOPY_CMD)
                                       && within1);
                               if (condition)
                                       stopLoop = 1;
                       } else {
                               condition = isFlashReadCMD(ptr->CMD) ||
                                       isFlashReadCMD(PendingCMD[j].CMD) ||
                                       ((PendingCMD[j].CMD == MEMCOPY_CMD)
                                       && within1);
                               if (condition) {
                                       if (isFlashReadCMD(PendingCMD[j].CMD)
                                               || ((PendingCMD[j].CMD ==
                                               MEMCOPY_CMD) && within1)) {
                                               stopLoop = 1;
                                               if (ptr->writeOpSyncPlaced)
                                                       break;
                                       }
                                       if (ptr->writeOpSyncPlaced &
                                               (1 << ptr->chnlOther))
                                               break;

                                       check_synced(ptr, j, k);
                               }
                       }
               }
       }
}

static void process_force_ordering_helper(struct add_sync_points_struct *ptr,
                               unsigned long *syncNums, int k)
{
       unsigned long syncedChans;
       int l;

       if ((syncNums[ptr->chnlOther] > MAX_CHANS)) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "LLD_CDMA: Sync Algorithm failed "
                       "find previously placed Forced Sync "
                       "at command tag %d, chnl %d\n",
                       (int)k - MAX_CHANS,
                       ptr->chnl);
               } else {
                       syncedChans = getChanFromChannel(
                       PendingCMD[k].ChanSync[syncNums[ptr->chnlOther]]);

                       l = getIntrFromChannel(
                       PendingCMD[k].ChanSync[syncNums[ptr->chnlOther]]);

                       PendingCMD[k].ChanSync[syncNums[ptr->chnlOther]] = 0;

                       putIntrInChannel(
                       PendingCMD[k].ChanSync[syncNums[ptr->chnlOther]], l);

                       putSyncInChannel(
                       PendingCMD[ptr->idx].ChanSync[syncNums[ptr->chnl]],
                       FORCED_ORDERED_SYNC);

                       putContInChannel(
                       PendingCMD[ptr->idx].ChanSync[syncNums[ptr->chnl]],
                       1);

                       putIdInChannel(
                       PendingCMD[ptr->idx].ChanSync[syncNums[ptr->chnl]],
                       ptr->chnl);

                       putChanInChannel(
                       PendingCMD[ptr->idx].ChanSync[syncNums[ptr->chnl]],
                       syncedChans);

                       for (l = 0; l < totalUsedBanks; l++) {
                               if (l != ptr->chnl)
                                       ptr->namb[l][ptr->chnl] = ptr->idx;
                       }
#if DEBUG_SYNC
                       if (!(debug_sync_cnt % DBG_SNC_PRINTEVERY))
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "ADDSYNC: Moved Forced Sync point "
                               "in chnl %d from tag %d to %d\n",
                               ptr->chnl,
                               k - MAX_CHANS,
                               ptr->idx - MAX_CHANS);
#endif
               }
}

static void process_force_ordering(struct add_sync_points_struct *ptr)
{
       unsigned long syncNums[MAX_CHANS];
       unsigned long syncedChans;
       int j, k, l, stopLoop;
#if DEBUG_SYNC
       int m;
#endif

       stopLoop = 0;
       for (k = ptr->idx - 1; (k >= MAX_CHANS); k--) {
               if (ptr->chnl != getChannelPendingCMD(k))
                       k = MAX_CHANS - 1;
               else if (isOrderedPendingCMD(k))
                       break;
       }

       if (k >= MAX_CHANS) {
               for (syncNums[ptr->chnl] = 0;
               (syncNums[ptr->chnl] <= MAX_CHANS)
               && (PendingCMD[ptr->idx].ChanSync[syncNums[ptr->chnl]]
               & CHANNEL_DMA_MASK); syncNums[ptr->chnl]++)
                       ;

               if (syncNums[ptr->chnl] > MAX_CHANS) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "LLD_CDMA: Sync Algorithm failed to place "
                               "a Forced Sync at command tag %d\n",
                               ptr->idx - MAX_CHANS);
               } else {
                       ptr->chnlOther = (ptr->chnl+1) % totalUsedBanks;
                       for (syncNums[ptr->chnlOther] = 0;
                            (syncNums[ptr->chnlOther] <= MAX_CHANS)
                            && (getSyncFromChannel(
                       PendingCMD[k].ChanSync[syncNums[ptr->chnlOther]]) !=
                       FORCED_ORDERED_SYNC);
                            syncNums[ptr->chnlOther]++)
                               ;

                       process_force_ordering_helper(ptr, syncNums, k);
               }
       } else {
               syncedChans = 0;
               for (j = 0; j < totalUsedBanks; j++) {
                       k = ptr->indx_last_cmd[j];
                       for (syncNums[j] = 0;
                       (syncNums[j] <= MAX_CHANS) &&
                               (PendingCMD[k].ChanSync[syncNums[j]] &
                               CHANNEL_DMA_MASK); syncNums[j]++)
                               ;
                       if ((syncNums[j] > MAX_CHANS)) {
                               /* This should never happen! */
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "LLD_CDMA: Sync Algorithm failed to place "
                               "a Forced Sync at command tag %d\n",
                               k - MAX_CHANS);
                               syncNums[0] = MAX_CHANS + 1;
                       }
                       syncedChans |= (1 << j);
               }

               if (syncNums[0] <= MAX_CHANS) {
                       for (j = 0; j < totalUsedBanks; j++) {
                               k = ptr->indx_last_cmd[j];
                               putSyncInChannel(
                                       PendingCMD[k].ChanSync[syncNums[j]],
                                       FORCED_ORDERED_SYNC);
                               putContInChannel(
                                       PendingCMD[k].ChanSync[syncNums[j]],
                                       1);
                               putIdInChannel(
                                       PendingCMD[k].ChanSync[syncNums[j]],
                                       j);
                               putChanInChannel(
                                       PendingCMD[k].ChanSync[syncNums[j]],
                                       syncedChans);
                               for (l = 0; l < totalUsedBanks; l++) {
                                       if (l != j)
                                               ptr->namb[l][j] = k;
                               }
                       }
#if DEBUG_SYNC
                       if (!(debug_sync_cnt % DBG_SNC_PRINTEVERY)) {
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "ADDSYNC: Placed Forced Sync point "
                                       "for tag %d in tags",
                                       ptr->idx - MAX_CHANS);
                               for (m = 0; m < totalUsedBanks; m++) {
                                       if (m != ptr->chnl)
                                               nand_dbg_print(NAND_DBG_DEBUG,
                                               " %d",
                                               (int)ptr->indx_last_cmd[m] -
                                               MAX_CHANS);
                               }
                               nand_dbg_print(NAND_DBG_DEBUG, "\n");
                       }
#endif
               }
       }
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_AddSyncPoints
* Inputs:       tag_count:- Number of commands in PendingCMD list
* Outputs:      NONE
* Description:  This function takes the PendingCMD list, and adds sync
*               points between each entry on it, and any preceding entry
*               in other channels that have conflicts with the Cache Block
*               pointer.
*               The design also takes care of syncing between memcopy
*               and flash read/write operations. However, this function
*               does not sync between 2 memcopy operations that have a conflict
*               in a RAM pointer other than the cache block one. It is the
*               responsibility of the calling function, probablt the
*               application calling spectra, to take care of that.
* Assumptions:  + This function is before the CDMA_Descriptor list is created.
*               + This function takes care of the fact that memcopy accesses
*                 might be just a few bytes within a cache block, and uses a
*                 knowledge of the cache block to check for accesses anywhere
*                 within it. However, it is assumed that we dont have ranges
*                 that overlap one another. Either ranges overlap perfectly, or
*                 the memcopy range is a subset of the flash address range.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void CDMA_AddSyncPoints(u16 tag_count)
{
       struct add_sync_points_struct vars;
       int i, j;

       vars.newSyncNum = 1;
       debug_boundary_error(totalUsedBanks - 1, MAX_CHANS, 0);
       for (i = 0; i < totalUsedBanks; i++) {
               vars.chnl = getChannelPendingCMD(i);
               debug_boundary_error(vars.chnl, totalUsedBanks, i);
               vars.indx_last_cmd[vars.chnl] = i;
               for (j = 0; j < totalUsedBanks; j++)
                       vars.namb[i][j] = 0;
       }

       for (i = 0; i <= MAX_SYNC; i++)
               vars.sync_usage[i] = 0;

       for (vars.idx = MAX_CHANS;
               vars.idx < (tag_count + MAX_CHANS);
               vars.idx++) {

               vars.writeOpSyncPlaced = 0;
               vars.CMD = PendingCMD[vars.idx].CMD;
               vars.chnl = getChannelPendingCMD(vars.idx);
               debug_boundary_error(vars.chnl, totalUsedBanks, vars.idx);

               if (vars.CMD == MEMCOPY_CMD)
                       process_memcpy(&vars);

               if (isFlashReadCMD(vars.CMD) || isFlashWriteCMD(vars.CMD))
                       process_flash_rw(&vars);

               vars.indx_last_cmd[vars.chnl] = vars.idx;

               /* Simple one sync to rule them all approach */
               if (isOrderedPendingCMD(vars.idx))
                       process_force_ordering(&vars);

       }
}

#if DEBUG_SYNC
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_SyncCheck
* Inputs:       tag_count:- Number of commands in PendingCMD list
* Outputs:      NONE
* Description:  This function takes a long time to run!
*               So use only during testing with lld_emu. The job of this fn
*               is to go through the post-synced PendingCMD array, and check
*               for a) buffers getting accessed out of order (which should
*               not happen), and b) deadlocks. i.e. 2 channels waiting on 2
*               different sync points both of which occur on the other channel
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

#include "flash.h"

#define EOLIST(i)   (chis[i] >= chMaxIndexes[i])

static void print_ops(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
               u32 rwop, u32 i, u32 chisi)
{
       if (rwop & 2)
               nand_dbg_print(NAND_DBG_DEBUG,
               "one or more read operations(indx:%d, tag:%d)",
               chisi >> 16, p[i][chisi >> 16].Tag);
       if (rwop & 1)
               nand_dbg_print(NAND_DBG_DEBUG,
               " one or more write operations(indx:%d, tag:%d)",
               chisi & 0xFFFF, p[i][chisi & 0xFFFF].Tag);
}

/* Get sync channel from pending command */
static u8 get_sync_ch_pcmd(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
               int i, int chisi, int *syncNum, int *i2)
{
       u32 syncVal;

       syncVal = p[i][chisi].ChanSync[0];
       if (syncVal) {
               *syncNum = getSyncFromChannel(syncVal);
               *i2 = getChanFromChannel(syncVal) & ~(1 << i);
               if ((*i2 != 1) && (*i2 != 2) && (*i2 != 4) && (*i2 != 8) &&
                               (*syncNum != FORCED_ORDERED_SYNC))
                       nand_dbg_print(NAND_DBG_DEBUG,
                       "SYNCCHECK: ASSERT FAIL: "
                       "second channel of sync(%d) got from sync val of "
                       "(ch:%d, indx:%d, tag:%d) is not a valid one!\n",
                       *i2, i, chisi, p[i][chisi].Tag);
               *i2 = (*i2 == 1) ? 0 : (*i2 == 2 ? 1 : (*i2 == 4 ? 2 :
                       (i != 3 ? 3 : 2)));
       }

       return (syncVal != 0);
}

static u32 check_ordering(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
               u32 ch1, u32 ch1_fromi, u32 ch1_toi,
               u32 ch2, u32 ch2_fromi, u32 ch2_toi)
{
       u32 sync2syncops[2], i, j;
       u32 rwop1, rwop2, lastcmd[2][CACHE_BLOCK_NUMBER];
       u32 chi, ch, chfromi, chtoi;
       u32 allok = 1;

       for (chi = 0; chi < 2; chi++) {
               if (chi) {
                       ch = ch2;
                       chfromi = ch2_fromi;
                       chtoi = ch2_toi;
               } else {
                       ch = ch1;
                       chfromi = ch1_fromi;
                       chtoi = ch1_toi;
               }

               sync2syncops[chi] = 0;

               for (j = 0; j < CACHE_BLOCK_NUMBER; j++)
                       lastcmd[chi][j] = 0;

               for (i = chfromi; i <= chtoi; i++) {
                       for (j = 0; j < CACHE_BLOCK_NUMBER; j++) {
                               if ((isFlashReadCMD(p[ch][i].CMD) &&
                                       (p[ch][i].DataAddr ==
                                       Cache.ItemArray[j].pContent)) ||
                                       ((p[ch][i].CMD == MEMCOPY_CMD) &&
                                       isWithinRange(p[ch][i].DataDestAddr,
                                       Cache.ItemArray[j].pContent,
                                       DeviceInfo.wBlockDataSize)
                                       )) {
                                       sync2syncops[chi] |= (1 << (j << 1));
                                       lastcmd[chi][j] &= 0xFFFF0000;
                                       lastcmd[chi][j] |= (i & 0xFFFF);
                               }
                               if ((isFlashWriteCMD(p[ch][i].CMD) &&
                                       (p[ch][i].DataAddr ==
                                       Cache.ItemArray[j].pContent)) ||
                                       ((p[ch][i].CMD == MEMCOPY_CMD) &&
                                       isWithinRange(p[ch][i].DataSrcAddr,
                                       Cache.ItemArray[j].pContent,
                                       DeviceInfo.wBlockDataSize))) {
                                       sync2syncops[chi] |=
                                               (1 << ((j << 1) + 1));
                                       lastcmd[chi][j] &= 0xFFFF;
                                       lastcmd[chi][j] |=
                                               ((i & 0xFFFF) << 16);
                               }
                       }
               }
       }

       for (j = 0; j < CACHE_BLOCK_NUMBER; j++) {
               rwop1 = (sync2syncops[0] >> (j << 1)) & 3;
               rwop2 = (sync2syncops[1] >> (j << 1)) & 3;
               if (((rwop1 & 1) && rwop2) || ((rwop2 & 1) && rwop1)) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "SYNCCHECK: ORDERING PROBLEM "
                               "in cache buffer %d: Between "
                               "(ch:%d, indx:%d, tag:%d) & "
                               "(ch:%d, indx:%d, tag:%d), "
                               "there has been\n",
                               j, ch1, ch1_fromi,
                               p[ch1][ch1_fromi].Tag,
                               ch1, ch1_toi,
                               p[ch1][ch1_toi].Tag);
                       print_ops(p, rwop1, ch1,
                                       lastcmd[0][j]);
                       nand_dbg_print(NAND_DBG_DEBUG,
                               ".\nWhich are not ordered w.r.t to ");
                       print_ops(p, rwop2, ch2,
                                       lastcmd[1][j]);
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "\nbetween (ch:%d, indx:%d, tag:%d) & "
                               "(ch:%d, indx:%d, tag:%d).\n",
                               ch2, ch2_fromi,
                               p[ch2][ch2_fromi].Tag,
                               ch2, ch2_toi,
                               p[ch2][ch2_toi].Tag);
                       allok = 0;
               }
       }

       return allok;
}

static int lookfor_deadlocks(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
                       int *chis, int *chMaxIndexes)
{
       int i, j, done, ch1, ch2, snum, snum2;

       done = 0;
       for (i = 0; (!done) && (i < totalUsedBanks); i++) {
               if (!EOLIST(i) &&
                       get_sync_ch_pcmd(p, i, chis[i], &snum, &ch1)) {
                       j = 0;
                       ch2 = ch1;
                       ch1 = i;
                       snum2 = snum;
                       snum = 0xFF;
                       while ((snum != snum2) && (j <= totalUsedBanks) &&
                               !EOLIST(ch2) && (ch2 != i) &&
                               ((snum == 0xFF) ||
                               (snum2 != FORCED_ORDERED_SYNC))) {
                               ch1 = ch2;
                               snum = snum2;
                               get_sync_ch_pcmd(p, ch1, chis[ch1],
                                       &snum2, &ch2);
                               j++;
                       }
                       if ((j <= totalUsedBanks) && (snum != snum2)) {
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "SYNCCHECK: DEADLOCK:\n");
                               ch1 = i;
                               snum = 0xFF;
                               get_sync_ch_pcmd(p, ch1, chis[ch1],
                                               &snum2, &ch2);
                               debug_boundary_error(ch2, totalUsedBanks, 0);
                               while (!EOLIST(ch2) && (ch2 != i) &&
                                       ((snum == 0xFF) ||
                                       (snum2 != FORCED_ORDERED_SYNC))) {
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Channel %d, cmdindx %d, "
                                               "tag %d is waiting for "
                                               "sync number %d "
                                               "from channel %d\n",
                                               ch1, chis[ch1],
                                               p[ch1][chis[ch1]].Tag,
                                               snum2, ch2);
                                       ch1 = ch2;
                                       snum = snum2;
                                       get_sync_ch_pcmd(p, ch1, chis[ch1],
                                                       &snum2, &ch2);
                                       debug_boundary_error(ch2,
                                                       totalUsedBanks, 0);
                               }
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "Channel %d, cmdindx %d, tag %d "
                                       "is waiting for sync number %d "
                                       "from channel %d",
                                       ch1, chis[ch1],
                                       p[ch1][chis[ch1]].Tag,
                                       snum2, ch2);
                               if (!EOLIST(ch2))
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       ", which is the initial channel!\n");
                               else if (snum2 != FORCED_ORDERED_SYNC)
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " which does not have that "
                                       "sync number!\n");
                               else
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                       " which is th forced ordered "
                                       "sync number that cannot proceed "
                                       "until all channels reach it!\n");
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "Sync checking is aborting.\n");
                               done = 1;
                       }
                       if (j > totalUsedBanks) {
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "SYNCCHECK: DEADLOCK: "
                                       "Unknown case. "
                                       "Infinite loop in deadlock check. "
                                       "Aborting.\n");
                               done = 1;
                       }
               }
       }

       return done;
}

static void cfo_helper_1(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
       int *chis, int *chMaxIndexes, int (*namb)[MAX_CHANS],
       int i, int ch1, int syncNum)
{
       int k;

       for (k = 0; k < totalUsedBanks; k++) {
               if ((k != i) && (k != ch1)) {
                       if (namb[ch1][k] > namb[i][k]) {
                               if (!check_ordering(p, i, namb[k][i] + 1,
                                       chis[i], k, namb[i][k] + 1,
                                       namb[ch1][k]))
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Above problem occured when "
                                               "analyzing sync %d between "
                                               "(ch:%d, indx:%d, tag:%d) & "
                                               "(ch:%d, indx:%d, tag:%d)\n",
                                               syncNum, i, chis[i],
                                               p[i][chis[i]].Tag,
                                               ch1, chis[ch1],
                                               p[ch1][chis[ch1]].Tag);
                               namb[i][k] = namb[ch1][k];
                       } else if (namb[ch1][k] < namb[i][k]) {
                               if (!check_ordering(p, ch1,
                                       namb[k][ch1] + 1,
                                       chis[ch1], k,
                                       namb[ch1][k] + 1,
                                       namb[i][k]))
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Above problem occured when "
                                               "analyzing sync %d between "
                                               "(ch:%d, indx:%d, tag:%d) & "
                                               "(ch:%d, indx:%d, tag:%d)\n",
                                               syncNum, i, chis[i],
                                               p[i][chis[i]].Tag,
                                               ch1, chis[ch1],
                                               p[ch1][chis[ch1]].Tag);
                               namb[ch1][k] = namb[i][k];
                       }
               }
       }
}

static void cfo_helper_2(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
       int *chis, int *chMaxIndexes, int (*namb)[MAX_CHANS],
       int i, int ch1, u8 *pidxchgd)
{
       int k, m, n;
       int sync_num, ch2;

       for (k = 0; k < totalUsedBanks; k++) {
               if ((k != i) && (k != ch1)) {
                       if (!EOLIST(k) && get_sync_ch_pcmd(p, k,
                                       chis[k], &sync_num, &ch2)) {
                               if (sync_num != FORCED_ORDERED_SYNC)
                                       k = totalUsedBanks + 2;
                       }
               }
       }

       if (k == totalUsedBanks) {
               for (m = 0; m < (totalUsedBanks - 1); m++) {
                       for (n = m + 1; n < totalUsedBanks; n++) {
                               if (!check_ordering(p, m, namb[n][m] + 1,
                                       chis[m], n, namb[m][n] + 1, chis[n]))
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Above problem occured when "
                                               "analyzing sync %d between "
                                               "(ch:%d, indx:%d, tag:%d) & "
                                               "(ch:%d, indx:%d, tag:%d)\n",
                                               sync_num, m, chis[m],
                                               p[m][chis[m]].Tag,
                                               n, chis[n],
                                               p[n][chis[n]].Tag);
                               namb[n][m] = chis[m];
                               namb[m][n] = chis[n];
                       }
                       chis[m]++;
               }
               chis[m]++;
               *pidxchgd = 1;
       }
}

static int check_for_ording(struct pending_cmd (*p)[MAX_CHANS + MAX_DESCS],
       int *chis, int *chMaxIndexes, int (*namb)[MAX_CHANS])
{
       int i, done, ch1, ch2, syncNum, syncNum2;
       u8 indexchgd;

       indexchgd = 0;
       for (i = 0; (i < totalUsedBanks) && !done && !indexchgd; i++) {
               if (!EOLIST(i) &&
                       get_sync_ch_pcmd(p, i, chis[i], &syncNum, &ch1)) {
                       debug_boundary_error(ch1, totalUsedBanks, 0);
                       if (!EOLIST(ch1) && get_sync_ch_pcmd(p, ch1,
                                       chis[ch1], &syncNum2, &ch2)) {
                               debug_boundary_error(ch2, totalUsedBanks, 0);
                               if ((syncNum == syncNum2) &&
                                       (syncNum != FORCED_ORDERED_SYNC)) {
                                       if (ch2 != i) {
                                               nand_dbg_print(NAND_DBG_DEBUG,
                                               "SYNCCHECK: ILLEGAL CASE: "
                                               "Channel %d, cmdindx %d, "
                                               "tag %d is waiting for "
                                               "sync number %d "
                                               "from channel %d, "
                                               "which is waiting for "
                                               "the same sync number "
                                               "from channel %d. "
                                               "Sync checking is aborting\n",
                                               i, chis[i],
                                               p[i][chis[i]].Tag,
                                               syncNum, ch1, ch2);
                                               done = 1;
                                       } else {
                                               if (!(debug_sync_cnt %
                                                       DBG_SNC_PRINTEVERY)) {
                                                       nand_dbg_print(
                                                       NAND_DBG_DEBUG,
                                                       "SYNCCHECK: "
                                                       "syncnum %d "
                                                       "betn Ch %d, "
                                                       "cmdindx %d, "
                                                       "tag %d & Ch %d, "
                                                       "cmdindx %d, tag %d. "
                                                       "chis="
                                                       "{%d, %d, %d, %d}\n",
                                                       syncNum, i,
                                                       chis[i],
                                                       p[i][chis[i]].Tag,
                                                       ch1, chis[ch1],
                                                       p[ch1][chis[ch1]].Tag,
                                                       chis[0], chis[1],
                                                       chis[2], chis[3]);
                                               }
                                               if (!check_ordering(p, i,
                                                       namb[ch1][i]+1,
                                                       chis[i], ch1,
                                                       namb[i][ch1]+1,
                                                       chis[ch1]))
                                                       nand_dbg_print(
                                                       NAND_DBG_DEBUG,
                                                       "Above problem "
                                                       "occured when "
                                                       "analyzing "
                                                       "sync %d "
                                                       "between "
                                                       "(ch:%d, indx:%d, "
                                                       "tag:%d) & "
                                                       "(ch:%d, indx:%d, "
                                                       "tag:%d)\n",
                                                       syncNum, i, chis[i],
                                                       p[i][chis[i]].Tag,
                                                       ch1, chis[ch1],
                                                       p[ch1][chis[ch1]].Tag);

                                               namb[ch1][i] = chis[i];
                                               namb[i][ch1] = chis[ch1];

                                               cfo_helper_1(p, chis,
                                                       chMaxIndexes,
                                                       namb, i, ch1,
                                                       syncNum);

                                               chis[i]++;
                                               chis[ch1]++;
                                               indexchgd = 1;
                                       }
                               } else if ((syncNum == syncNum2) &&
                                          (syncNum == FORCED_ORDERED_SYNC)) {
                                       cfo_helper_2(p, chis, chMaxIndexes,
                                               namb, i, ch1, &indexchgd);
                               }
                       }
               }
       }

       return done;
}

void CDMA_CheckSyncPoints(u16 tag_count)
{
       struct pending_cmd p_cmd_ch[MAX_CHANS][MAX_CHANS + MAX_DESCS];
       int namb[MAX_CHANS][MAX_CHANS];
       int chMaxIndexes[MAX_CHANS];
       int chis[MAX_CHANS];
       u32 i, j, k, alldone;

       /* Initial Checks */
       if (CACHE_BLOCK_NUMBER > 16) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "SYNCCHECK: INIT FAILED: SyncCheck can only "
                       "work with upto 16 cache blocks \n");
               return;
       }

       /* Initializations */
       for (i = 0; i < totalUsedBanks; i++) {
               chis[i] = 0;
               for (j = 0; j < totalUsedBanks; j++)
                       namb[i][j] = -1;
       }

       pcmd_per_ch(p_cmd_ch, tag_count, chMaxIndexes);

       if (!(debug_sync_cnt % DBG_SNC_PRINTEVERY)) {
               nand_dbg_print(NAND_DBG_DEBUG, "SYNCCHECK: Cache Ptrs:");
               for (j = 0; j < CACHE_BLOCK_NUMBER; j++)
                       nand_dbg_print(NAND_DBG_DEBUG, " %p",
                               Cache.ItemArray[j].pContent);
               nand_dbg_print(NAND_DBG_DEBUG, "\n");
       }

       alldone = 0;
       while (!alldone) {
               for (i = 0; i < totalUsedBanks; i++) {
                       while (!EOLIST(i)) {
                               if (!p_cmd_ch[i][chis[i]].ChanSync[0])
                                       chis[i]++;
                               else
                                       break;
                       }
               }
               alldone = lookfor_deadlocks(p_cmd_ch, chis, chMaxIndexes);
               alldone = check_for_ording(p_cmd_ch, chis, chMaxIndexes,
                                       namb);
               if (!alldone) {
                       alldone = 1;
                       for (i = 0; alldone && (i < totalUsedBanks); i++) {
                               if (!EOLIST(i))
                                       alldone = 0;
                       }
               }
       }

       for (i = 0; i < totalUsedBanks; i++) {
               for (k = i + 1; k < totalUsedBanks; k++) {
                       if (!check_ordering(p_cmd_ch, i, namb[k][i] + 1,
                               chMaxIndexes[i] - 1, k, namb[i][k] + 1,
                               chMaxIndexes[k] - 1))
                               nand_dbg_print(NAND_DBG_DEBUG,
                               "Above problem occured when doing "
                               "end of list checks on channels %d & %d\n",
                               i, k);
               }
       }
}

#endif
#endif

