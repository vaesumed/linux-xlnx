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

#include "flash.h"
#include "ffsdefs.h"
#include "lld.h"
#if CMD_DMA
#include "lld_cdma.h"
#endif

#define NAND_CACHE_INIT_ADDR    0xffffffffffffffffULL

#define BLK_FROM_ADDR(addr)  ((u32)(addr >> DeviceInfo.nBitsInBlockDataSize))
#define PAGE_FROM_ADDR(addr, Block)  ((u16)((addr - (u64)Block * \
       DeviceInfo.wBlockDataSize) >> DeviceInfo.nBitsInPageDataSize))

#define IS_SPARE_BLOCK(blk)     (BAD_BLOCK != (pbt[blk] &\
       BAD_BLOCK) && SPARE_BLOCK == (pbt[blk] & SPARE_BLOCK))

#define IS_DATA_BLOCK(blk)      (0 == (pbt[blk] & BAD_BLOCK))

#define IS_DISCARDED_BLOCK(blk) (BAD_BLOCK != (pbt[blk] &\
       BAD_BLOCK) && DISCARD_BLOCK == (pbt[blk] & DISCARD_BLOCK))

#define IS_BAD_BLOCK(blk)       (BAD_BLOCK == (pbt[blk] & BAD_BLOCK))

#define NUM_MEMPOOL_ALLOCS            (22 + CACHE_BLOCK_NUMBER)

#if DEBUG_BNDRY
void debug_boundary_lineno_error(int chnl, int limit, int no,
                               int lineno, char *filename)
{
       if (chnl >= limit)
               printk(KERN_ERR "Boundary Check Fail value %d >= limit %d, "
               "at  %s:%d. Other info:%d. Aborting...\n",
               chnl, limit, filename, lineno, no);
}
/* static int globalmemsize; */
#endif

static u8 FTL_Cache_If_Hit(u64 dwPageAddr);
static int FTL_Cache_Read(u64 dwPageAddr);
static void FTL_Cache_Read_Page(u8 *pData, u64 dwPageAddr,
                               u8 cache_blk);
static void FTL_Cache_Write_Page(u8 *pData, u64 dwPageAddr,
                                u8 cache_blk, u16 flag);
static int FTL_Cache_Write(void);
static int FTL_Cache_Write_Back(u8 *pData, u64 blk_addr);
static void FTL_Calculate_LRU(void);
static u32 FTL_Get_Block_Index(u32 wBlockNum);

static int FTL_Search_Block_Table_IN_Block(u32 BT_Block,
                                          u8 BT_Tag, u16 *Page);
static int FTL_Read_Block_Table(void);
static int FTL_Write_Block_Table(int wForce);
static int FTL_Write_Block_Table_Data(void);
static int FTL_Check_Block_Table(int wOldTable);
static int FTL_Static_Wear_Leveling(void);
static u32 FTL_Replace_Block_Table(void);
static int FTL_Write_IN_Progress_Block_Table_Page(void);

static u32 FTL_Get_Page_Num(u64 length);
static u64 FTL_Get_Physical_Block_Addr(u64 blk_addr);

static u32 FTL_Replace_OneBlock(u32 wBlockNum,
                                     u32 wReplaceNum);
static u32 FTL_Replace_LWBlock(u32 wBlockNum,
                                    int *pGarbageCollect);
static u32 FTL_Replace_MWBlock(void);
static int FTL_Replace_Block(u64 blk_addr);
static int FTL_Adjust_Relative_Erase_Count(u32 Index_of_MAX);

static int FTL_Flash_Error_Handle(u8 *pData,
       u64 old_page_addr, u64 blk_addr);

struct device_info_tag DeviceInfo;
static u8 *g_pTempBuf;
u8 *g_pBlockTable;
u8 *g_pWearCounter;
u16 *g_pReadCounter;
static u16 g_wBlockTableOffset;
static u32 g_wBlockTableIndex;
static u8 g_cBlockTableStatus;
u32 *g_pBTBlocks;
struct flash_cache_tag Cache;

int g_wNumFreeBlocks;
#if CMD_DMA
   u8 g_SBDCmdIndex = 0;
#endif
static u8 *g_pIPF;
static u8 bt_flag = FIRST_BT_ID;
static u8 bt_block_changed;

#if READBACK_VERIFY
static u8 *g_pCheckBuf;
#endif

static u8 cache_block_to_write;
static u8 last_erased = FIRST_BT_ID;

static u8 *g_pMemPool;
static u8 *g_pMemPoolFree;
static u8 *g_temp_buf;

static int globalMemSize;

static u8 GC_Called;
static u8 BT_GC_Called;

#if CMD_DMA
static u8 FTLCommandCount;  /* Init value is 0 */
u8 *g_pBTDelta;
u8 *g_pBTDelta_Free;
u8 *g_pBTStartingCopy;
u8 *g_pWearCounterCopy;
u16 *g_pReadCounterCopy;
u8 *g_pBlockTableCopies;
u8 *g_pNextBlockTable;
u8 *g_pCopyBackBufferCopies;
u8 *g_pCopyBackBufferStart;

#pragma pack(push, 1)
#pragma pack(1)
struct BTableChangesDelta {
       u8 FTLCommandCount;
       u8 ValidFields;
       u16 g_wBlockTableOffset;
       u32 g_wBlockTableIndex;
       u32 BT_Index;
       u32 BT_Entry_Value;
       u32 WC_Index;
       u8 WC_Entry_Value;
       u32 RC_Index;
       u16 RC_Entry_Value;
};

#pragma pack(pop)

struct BTableChangesDelta *p_BTableChangesDelta;
#endif


#define MARK_BLOCK_AS_BAD(blocknode)      (blocknode |= BAD_BLOCK)
#define MARK_BLK_AS_DISCARD(blk)  (blk = (blk & ~SPARE_BLOCK) | DISCARD_BLOCK)

#define FTL_Get_LBAPBA_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
                                               sizeof(u32))
#define FTL_Get_WearCounter_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
                                               sizeof(u8))
#define FTL_Get_ReadCounter_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
                                               sizeof(u16))
#if SUPPORT_LARGE_BLOCKNUM
#define FTL_Get_LBAPBA_Table_Flash_Size_Bytes() (DeviceInfo.wDataBlockNum *\
                                               sizeof(u8) * 3)
#else
#define FTL_Get_LBAPBA_Table_Flash_Size_Bytes() (DeviceInfo.wDataBlockNum *\
                                               sizeof(u32))
#endif
#define FTL_Get_WearCounter_Table_Flash_Size_Bytes \
       FTL_Get_WearCounter_Table_Mem_Size_Bytes
#define FTL_Get_ReadCounter_Table_Flash_Size_Bytes \
       FTL_Get_ReadCounter_Table_Mem_Size_Bytes

static u32 FTL_Get_Block_Table_Flash_Size_Bytes(void)
{
       u32 byte_num;

       if (DeviceInfo.MLCDevice) {
               byte_num = FTL_Get_LBAPBA_Table_Flash_Size_Bytes() +
                       DeviceInfo.wDataBlockNum * sizeof(u8) +
                       DeviceInfo.wDataBlockNum * sizeof(u16);
       } else {
               byte_num = FTL_Get_LBAPBA_Table_Flash_Size_Bytes() +
                       DeviceInfo.wDataBlockNum * sizeof(u8);
       }

       byte_num += 4 * sizeof(u8);

       return byte_num;
}

static u16  FTL_Get_Block_Table_Flash_Size_Pages(void)
{
       return (u16)FTL_Get_Page_Num(FTL_Get_Block_Table_Flash_Size_Bytes());
}

static int FTL_Copy_Block_Table_To_Flash(u8 *flashBuf, u32 sizeToTx,
                                       u32 sizeTxed)
{
       u32 wBytesCopied, blk_tbl_size, wBytes;
       u32 *pbt = (u32 *)g_pBlockTable;

       blk_tbl_size = FTL_Get_LBAPBA_Table_Flash_Size_Bytes();
       for (wBytes = 0;
       (wBytes < sizeToTx) && ((wBytes + sizeTxed) < blk_tbl_size);
       wBytes++) {
#if SUPPORT_LARGE_BLOCKNUM
               flashBuf[wBytes] = (u8)(pbt[(wBytes + sizeTxed) / 3]
               >> (((wBytes + sizeTxed) % 3) ?
               ((((wBytes + sizeTxed) % 3) == 2) ? 0 : 8) : 16)) & 0xFF;
#else
               flashBuf[wBytes] = (u8)(pbt[(wBytes + sizeTxed) / 2]
               >> (((wBytes + sizeTxed) % 2) ? 0 : 8)) & 0xFF;
#endif
       }

       sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;
       blk_tbl_size = FTL_Get_WearCounter_Table_Flash_Size_Bytes();
       wBytesCopied = wBytes;
       wBytes = ((blk_tbl_size - sizeTxed) > (sizeToTx - wBytesCopied)) ?
               (sizeToTx - wBytesCopied) : (blk_tbl_size - sizeTxed);
       memcpy(flashBuf + wBytesCopied, g_pWearCounter + sizeTxed, wBytes);

       sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;

       if (DeviceInfo.MLCDevice) {
               blk_tbl_size = FTL_Get_ReadCounter_Table_Flash_Size_Bytes();
               wBytesCopied += wBytes;
               for (wBytes = 0; ((wBytes + wBytesCopied) < sizeToTx) &&
                       ((wBytes + sizeTxed) < blk_tbl_size); wBytes++)
                       flashBuf[wBytes + wBytesCopied] =
                       (g_pReadCounter[(wBytes + sizeTxed) / 2] >>
                       (((wBytes + sizeTxed) % 2) ? 0 : 8)) & 0xFF;
       }

       return wBytesCopied + wBytes;
}

static int FTL_Copy_Block_Table_From_Flash(u8 *flashBuf,
                               u32 sizeToTx, u32 sizeTxed)
{
       u32 wBytesCopied, blk_tbl_size, wBytes;
       u32 *pbt = (u32 *)g_pBlockTable;

       blk_tbl_size = FTL_Get_LBAPBA_Table_Flash_Size_Bytes();
       for (wBytes = 0; (wBytes < sizeToTx) &&
               ((wBytes + sizeTxed) < blk_tbl_size); wBytes++) {
#if SUPPORT_LARGE_BLOCKNUM
               if (!((wBytes + sizeTxed) % 3))
                       pbt[(wBytes + sizeTxed) / 3] = 0;
               pbt[(wBytes + sizeTxed) / 3] |=
                       (flashBuf[wBytes] << (((wBytes + sizeTxed) % 3) ?
                       ((((wBytes + sizeTxed) % 3) == 2) ? 0 : 8) : 16));
#else
               if (!((wBytes + sizeTxed) % 2))
                       pbt[(wBytes + sizeTxed) / 2] = 0;
               pbt[(wBytes + sizeTxed) / 2] |=
                       (flashBuf[wBytes] << (((wBytes + sizeTxed) % 2) ?
                       0 : 8));
#endif
       }

       sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;
       blk_tbl_size = FTL_Get_WearCounter_Table_Flash_Size_Bytes();
       wBytesCopied = wBytes;
       wBytes = ((blk_tbl_size - sizeTxed) > (sizeToTx - wBytesCopied)) ?
               (sizeToTx - wBytesCopied) : (blk_tbl_size - sizeTxed);
       memcpy(g_pWearCounter + sizeTxed, flashBuf + wBytesCopied, wBytes);
       sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;

       if (DeviceInfo.MLCDevice) {
               wBytesCopied += wBytes;
               blk_tbl_size = FTL_Get_ReadCounter_Table_Flash_Size_Bytes();
               for (wBytes = 0; ((wBytes + wBytesCopied) < sizeToTx) &&
                       ((wBytes + sizeTxed) < blk_tbl_size); wBytes++) {
                       if (((wBytes + sizeTxed) % 2))
                               g_pReadCounter[(wBytes + sizeTxed) / 2] = 0;
                       g_pReadCounter[(wBytes + sizeTxed) / 2] |=
                               (flashBuf[wBytes] <<
                               (((wBytes + sizeTxed) % 2) ? 0 : 8));
               }
       }

       return wBytesCopied+wBytes;
}

static int FTL_Insert_Block_Table_Signature(u8 *buf, u8 tag)
{
       int i;

       for (i = 0; i < BTSIG_BYTES; i++)
               buf[BTSIG_OFFSET + i] =
               ((tag + (i * BTSIG_DELTA) - FIRST_BT_ID) %
               (1 + LAST_BT_ID-FIRST_BT_ID)) + FIRST_BT_ID;

       return PASS;
}

static int FTL_Extract_Block_Table_Tag(u8 *buf, u8 **tagarray)
{
       static u8 tag[BTSIG_BYTES >> 1];
       int i, j, k, tagi, tagtemp, status;

       *tagarray = (u8 *)tag;
       tagi = 0;

       for (i = 0; i < (BTSIG_BYTES - 1); i++) {
               for (j = i + 1; (j < BTSIG_BYTES) &&
                       (tagi < (BTSIG_BYTES >> 1)); j++) {
                       tagtemp = buf[BTSIG_OFFSET + j] -
                               buf[BTSIG_OFFSET + i];
                       if (tagtemp && !(tagtemp % BTSIG_DELTA)) {
                               tagtemp = (buf[BTSIG_OFFSET + i] +
                                       (1 + LAST_BT_ID - FIRST_BT_ID) -
                                       (i * BTSIG_DELTA)) %
                                       (1 + LAST_BT_ID - FIRST_BT_ID);
                               status = FAIL;
                               for (k = 0; k < tagi; k++) {
                                       if (tagtemp == tag[k])
                                               status = PASS;
                               }

                               if (status == FAIL) {
                                       tag[tagi++] = tagtemp;
                                       i = (j == (i + 1)) ? i + 1 : i;
                                       j = (j == (i + 1)) ? i + 1 : i;
                               }
                       }
               }
       }

       return tagi;
}


static int FTL_Execute_SPL_Recovery(void)
{
       u32 j, block, blks;
       u32 *pbt = (u32 *)g_pBlockTable;
       int ret;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                               __FILE__, __LINE__, __func__);

       blks = DeviceInfo.wSpectraEndBlock - DeviceInfo.wSpectraStartBlock;
       for (j = 0; j <= blks; j++) {
               block = (pbt[j]);
               if (((block & BAD_BLOCK) != BAD_BLOCK) &&
                       ((block & SPARE_BLOCK) == SPARE_BLOCK)) {
#if CMD_DMA
                       ret =  GLOB_LLD_Erase_Block(block & ~BAD_BLOCK,
                       FTLCommandCount, LLD_CMD_FLAG_MODE_POLL);
#else
                       ret =  GLOB_LLD_Erase_Block(block & ~BAD_BLOCK);
#endif
                       if (FAIL == ret) {
                               nand_dbg_print(NAND_DBG_WARN,
                                       "NAND Program fail in %s, Line %d, "
                                       "Function: %s, new Bad Block %d "
                                       "generated!\n",
                                       __FILE__, __LINE__, __func__,
                                       (int)(block & ~BAD_BLOCK));
                               MARK_BLOCK_AS_BAD(pbt[j]);
                       }
               }
       }

       return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_IdentifyDevice
* Inputs:       pointer to identify data structure
* Outputs:      PASS / FAIL
* Description:  the identify data structure is filled in with
*                   information for the block driver.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_IdentifyDevice(struct spectra_indentfy_dev_tag *dev_data)
{
       int status = PASS;
       int bufMem;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                               __FILE__, __LINE__, __func__);

       bufMem = (DeviceInfo.wPageDataSize -
                 ((DeviceInfo.wDataBlockNum *
                   (sizeof(u32) + sizeof(u8)
                    + (DeviceInfo.MLCDevice ? sizeof(u16) : 0))) %
                  DeviceInfo.wPageDataSize)) %
                  DeviceInfo.wPageDataSize;

       dev_data->NumBlocks = DeviceInfo.wTotalBlocks;
       dev_data->PagesPerBlock = DeviceInfo.wPagesPerBlock;
       dev_data->PageDataSize = DeviceInfo.wPageDataSize;
       dev_data->wECCBytesPerSector =
           DeviceInfo.wECCBytesPerSector;
       dev_data->wDataBlockNum = DeviceInfo.wDataBlockNum;

       dev_data->SizeOfGlobalMem =
           (DeviceInfo.wDataBlockNum  * sizeof(u32) * 2) +
           (DeviceInfo.wDataBlockNum * sizeof(u8) + 2) +
           (DeviceInfo.MLCDevice ?
            (DeviceInfo.wDataBlockNum * sizeof(u16)
#if CMD_DMA
             * (1+1+1)
#endif
             ) : 0) + bufMem +
#if (PAGES_PER_CACHE_BLOCK > 0)
       ((CACHE_BLOCK_NUMBER + 1) * PAGES_PER_CACHE_BLOCK *
       DeviceInfo.wPageDataSize * sizeof(u8)) +
#else
       ((CACHE_BLOCK_NUMBER+1) * DeviceInfo.wPagesPerBlock *
       DeviceInfo.wPageDataSize * sizeof(u8)) +
#endif
       (DeviceInfo.wPageSize*sizeof(u8)) +
       (DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize * sizeof(u8))
       +
#if CMD_DMA
       (DeviceInfo.wDataBlockNum * sizeof(u32)) +
       (DeviceInfo.wDataBlockNum * sizeof(u8)) +
       (5 * ((DeviceInfo.wDataBlockNum * sizeof(u32)) +
       (DeviceInfo.wDataBlockNum * sizeof(u8)) +
       (DeviceInfo.wDataBlockNum * sizeof(u16)))) +
       (MAX_DESCS * sizeof(struct BTableChangesDelta)) +
       (10 * DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize) +
#endif
       ((1 + LAST_BT_ID - FIRST_BT_ID) * sizeof(u32)) +
       (DeviceInfo.wDataBlockNum) +
       (DeviceInfo.wPageDataSize * sizeof(u8) * 2) +
       (((DeviceInfo.wPageSize - DeviceInfo.wPageDataSize) *
       sizeof(u8)) * 2) +
       (DeviceInfo.wDataBlockNum) +
#if !CMD_DMA
       (DeviceInfo.wPageDataSize * DeviceInfo.wPagesPerBlock *
       sizeof(u8) * 2) +
#endif
       DeviceInfo.wBlockSize + GLOB_LLD_Memory_Pool_Size() +
       (NUM_MEMPOOL_ALLOCS * sizeof(u8) * 4);

       globalMemSize = dev_data->SizeOfGlobalMem;

       return status;
}
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:        GLOB_FTL_Mem_Config
* Inputs:          pointer to the memory that is allocated
* Outputs:         PASS / FAIL
* Description:     This allows the Block Driver to do the memory allocation
*                  and is used in place of the FTL doing malloc's.  The
*                  Block Driver assigns the length based on data passed
*                  to it in the GLOB_FTL_IdentifyDevice function.
*                  There is sanity checking that the pointers are not NULL
*                  There is no sanity checking for the length. If this
*                  becomes neccessary, an additioanl parameter will
*                  be needed.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Mem_Config(u8 *pMem)
{
       int status = FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       if (pMem != NULL) {
               g_pMemPool = pMem;
               status = GLOB_LLD_Mem_Config(pMem + globalMemSize -
                       GLOB_LLD_Memory_Pool_Size());
       }

       return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Init
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  allocates the memory for cache array,
*               important data structures
*               clears the cache array
*               reads the block table from flash into array
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Init(void)
{
       int i;
       int status = PASS;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

#if (PAGES_PER_CACHE_BLOCK > 0)
       Cache.wCachePageNum = PAGES_PER_CACHE_BLOCK;
#else
       Cache.wCachePageNum = DeviceInfo.wPagesPerBlock;
#endif
       Cache.dwCacheDataSize = (u32)Cache.wCachePageNum *
               DeviceInfo.wPageDataSize;

       g_pMemPoolFree = (u8 *)g_pMemPool;

       g_pBlockTable = (u8 *)g_pMemPoolFree;
       memset(g_pBlockTable, 0, DeviceInfo.wDataBlockNum * sizeof(u32));
       g_pMemPoolFree += DeviceInfo.wDataBlockNum * sizeof(u32);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_pWearCounter = (u8 *)g_pMemPoolFree;
       memset(g_pWearCounter, 0, DeviceInfo.wDataBlockNum * sizeof(u8));
       g_pMemPoolFree += DeviceInfo.wDataBlockNum * sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       if (DeviceInfo.MLCDevice) {
               g_pReadCounter = (u16 *)g_pMemPoolFree;
               g_pMemPoolFree += DeviceInfo.wDataBlockNum * sizeof(u16);
               memset(g_pReadCounter, 0,
                       DeviceInfo.wDataBlockNum * sizeof(u16));
               ALIGN_DWORD_FWD(g_pMemPoolFree);
       }

       for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
               Cache.ItemArray[i].dwAddress = NAND_CACHE_INIT_ADDR;
               Cache.ItemArray[i].bLRUCount = 0;
               Cache.ItemArray[i].bChanged = CLEAR;
               Cache.ItemArray[i].pContent = (u8 *)g_pMemPoolFree;
               g_pMemPoolFree += Cache.dwCacheDataSize * sizeof(u8);
               ALIGN_DWORD_FWD(g_pMemPoolFree);
       }

       g_pIPF = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += DeviceInfo.wPageSize * sizeof(u8);
       memset(g_pIPF, 0, DeviceInfo.wPageSize);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_pTempBuf = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += Cache.dwCacheDataSize * sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_temp_buf = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += DeviceInfo.wPagesPerBlock *
                       DeviceInfo.wPageDataSize * sizeof(u8);
       memset(g_temp_buf, 0xFF,
               DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

#if CMD_DMA
       g_pBTStartingCopy = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wDataBlockNum * sizeof(u32));
       memset(g_pBTStartingCopy, 0, DeviceInfo.wDataBlockNum * sizeof(u32));
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_pWearCounterCopy = (u8 *)g_pMemPoolFree;
       memset(g_pWearCounterCopy, 0, DeviceInfo.wDataBlockNum * sizeof(u8));
       g_pMemPoolFree += DeviceInfo.wDataBlockNum * sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       if (DeviceInfo.MLCDevice) {
               g_pReadCounterCopy = (u16 *)g_pMemPoolFree;
               g_pMemPoolFree += DeviceInfo.wDataBlockNum * sizeof(u16);
               memset(g_pReadCounterCopy, 0,
                       DeviceInfo.wDataBlockNum * sizeof(u16));
               ALIGN_DWORD_FWD(g_pMemPoolFree);
       }

       g_pBlockTableCopies = (u8 *)g_pMemPoolFree;
       g_pNextBlockTable = g_pBlockTableCopies;

       if (DeviceInfo.MLCDevice)
               g_pMemPoolFree += 5 *
                       (DeviceInfo.wDataBlockNum * sizeof(u32) +
                       DeviceInfo.wDataBlockNum * sizeof(u8) +
                       DeviceInfo.wDataBlockNum * sizeof(u16));
       else
               g_pMemPoolFree += 5 *
                       (DeviceInfo.wDataBlockNum * sizeof(u32) +
                       DeviceInfo.wDataBlockNum * sizeof(u8));

       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_pBTDelta = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += (MAX_DESCS * sizeof(struct BTableChangesDelta));
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       FTLCommandCount = 0;
       g_pBTDelta_Free = (u8 *)g_pBTDelta;
       g_pCopyBackBufferCopies = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += 10 * DeviceInfo.wPagesPerBlock *
               DeviceInfo.wPageDataSize;
       ALIGN_DWORD_FWD(g_pMemPoolFree);

       g_pCopyBackBufferStart = g_pCopyBackBufferCopies;
#endif
       g_pBTBlocks = (u32 *)g_pMemPoolFree;
       g_pMemPoolFree += (1 + LAST_BT_ID - FIRST_BT_ID) * sizeof(u32);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       memset(g_pBTBlocks, 0xFF,
               (1 + LAST_BT_ID - FIRST_BT_ID) * sizeof(u32));
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
               globalMemSize, 0);

       status = FTL_Read_Block_Table();

#if CMD_DMA
       FTLCommandCount = 0;
#endif

       return status;
}

#if CMD_DMA
int GLOB_FTL_cdma_int(void)
{
       return GLOB_LLD_is_cdma_int();
}

static void save_blk_table_changes(u16 idx)
{
       u8 ftl_cmd;
       u32 *pbt = (u32 *)g_pBTStartingCopy;

       ftl_cmd = p_BTableChangesDelta->FTLCommandCount;

       while (ftl_cmd <= PendingCMD[idx].Tag) {
               if (p_BTableChangesDelta->ValidFields == 0x01) {
                       g_wBlockTableOffset =
                               p_BTableChangesDelta->g_wBlockTableOffset;
               } else if (p_BTableChangesDelta->ValidFields == 0x0C) {
                       pbt[p_BTableChangesDelta->BT_Index] =
                               p_BTableChangesDelta->BT_Entry_Value;
                       debug_boundary_error(((
                               p_BTableChangesDelta->BT_Index)),
                               DeviceInfo.wDataBlockNum, 0);
               } else if (p_BTableChangesDelta->ValidFields == 0x03) {
                       g_wBlockTableOffset =
                               p_BTableChangesDelta->g_wBlockTableOffset;
                       g_wBlockTableIndex =
                               p_BTableChangesDelta->g_wBlockTableIndex;
               } else if (p_BTableChangesDelta->ValidFields == 0x30) {
                       g_pWearCounterCopy[p_BTableChangesDelta->WC_Index] =
                               p_BTableChangesDelta->WC_Entry_Value;
               } else if ((DeviceInfo.MLCDevice) &&
                       (p_BTableChangesDelta->ValidFields == 0xC0)) {
                       g_pReadCounterCopy[p_BTableChangesDelta->RC_Index] =
                               p_BTableChangesDelta->RC_Entry_Value;
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "In event status setting read counter "
                               "GLOB_FTLCommandCount %u Count %u Index %u\n",
                               ftl_cmd,
                               p_BTableChangesDelta->RC_Entry_Value,
                               (unsigned int)p_BTableChangesDelta->RC_Index);
               } else {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "This should never occur \n");
               }
               p_BTableChangesDelta += 1;
               ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
       }
}

static void discard_cmds(u16 n)
{
       u32 *pbt = (u32 *)g_pBTStartingCopy;
       u8 ftl_cmd;
       unsigned long k, cn;

       if ((PendingCMD[n].CMD == WRITE_MAIN_CMD) ||
               (PendingCMD[n].CMD == WRITE_MAIN_SPARE_CMD)) {
               for (k = 0; k < DeviceInfo.wDataBlockNum; k++) {
                       if (PendingCMD[n].Block == (pbt[k] & (~BAD_BLOCK)))
                               MARK_BLK_AS_DISCARD(pbt[k]);
               }
       }

       ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
       while (ftl_cmd <= PendingCMD[n].Tag) {
               p_BTableChangesDelta += 1;
               ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
       }

       cn = UNHIT_BLOCK;
       for (k = 0; k < CACHE_BLOCK_NUMBER; k++) {
               if (PendingCMD[n].DataAddr == Cache.ItemArray[k].pContent) {
                       cn = k;
                       break;
               }
       }
       if (cn < UNHIT_BLOCK) {
               Cache.ItemArray[cn].dwAddress = NAND_CACHE_INIT_ADDR;
               Cache.ItemArray[cn].bLRUCount = 0;
               Cache.ItemArray[cn].bChanged  = CLEAR;
       }
}

static void process_cmd_pass(int *first_failed_cmd, u16 idx)
{
       int is_rw_cmd;

       is_rw_cmd = (PendingCMD[idx].CMD == WRITE_MAIN_CMD) ||
               (PendingCMD[idx].CMD == WRITE_MAIN_SPARE_CMD) ||
               (PendingCMD[idx].CMD == READ_MAIN_CMD) ||
               (PendingCMD[idx].CMD == READ_MAIN_SPARE_CMD);

       if (0 == *first_failed_cmd)
               save_blk_table_changes(idx);
       else if (is_rw_cmd)
               discard_cmds(idx);
}

static void process_cmd_fail_abort(int *first_failed_cmd,
                               u16 idx, int event)
{
       u32 *pbt = (u32 *)g_pBTStartingCopy;
       u8 ftl_cmd;
       unsigned long i, k, cn;
       int erase_fail, program_fail;

       if (0 == *first_failed_cmd)
               *first_failed_cmd = PendingCMD[idx].SBDCmdIndex;

       nand_dbg_print(NAND_DBG_DEBUG, "Uncorrectable error has occured "
               "while executing %u Command %u accesing Block %u\n",
               (unsigned int)p_BTableChangesDelta->FTLCommandCount,
               PendingCMD[idx].CMD,
               (unsigned int)PendingCMD[idx].Block);

       ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
       while (ftl_cmd <= PendingCMD[idx].Tag) {
               p_BTableChangesDelta += 1;
               ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
       }

       if ((PendingCMD[idx].CMD == READ_MAIN_CMD) ||
               (PendingCMD[idx].CMD == READ_MAIN_SPARE_CMD)) {
               for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
                       Cache.ItemArray[i].dwAddress = NAND_CACHE_INIT_ADDR;
                       Cache.ItemArray[i].bLRUCount = 0;
                       Cache.ItemArray[i].bChanged  = CLEAR;
               }
       } else if ((PendingCMD[idx].CMD == WRITE_MAIN_CMD) ||
               (PendingCMD[idx].CMD == WRITE_MAIN_SPARE_CMD)) {
               cn = 0;
               for (k = 0; k < DeviceInfo.wDataBlockNum; k++) {
                       if (PendingCMD[idx].Block == (pbt[k] & (~BAD_BLOCK))) {
                               Cache.ItemArray[0].dwAddress = (u64)k *
                                       DeviceInfo.wBlockDataSize;
                               Cache.ItemArray[0].bLRUCount = 0;
                               Cache.ItemArray[0].bChanged  = SET;
                               break;
                       }
               }

               if (k == DeviceInfo.wDataBlockNum)
                       cn = 0;
               else
                       cn = 1;

               for (i = cn; i < CACHE_BLOCK_NUMBER; i++) {
                       Cache.ItemArray[i].dwAddress = NAND_CACHE_INIT_ADDR;
                       Cache.ItemArray[i].bLRUCount = 0;
                       Cache.ItemArray[i].bChanged  = CLEAR;
               }
       }

       erase_fail = (event == EVENT_ERASE_FAILURE) &&
                       (PendingCMD[idx].CMD == ERASE_CMD);

       program_fail = (event == EVENT_PROGRAM_FAILURE) &&
                       ((PendingCMD[idx].CMD == WRITE_MAIN_CMD) ||
                       (PendingCMD[idx].CMD == WRITE_MAIN_SPARE_CMD));

       if (erase_fail || program_fail) {
               for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
                       if (PendingCMD[idx].Block ==
                               (pbt[i] & (~BAD_BLOCK)))
                               MARK_BLOCK_AS_BAD(pbt[i]);
               }
       }
}

static void process_cmd(int *first_failed_cmd, u16 idx, int event)
{
       u8 ftl_cmd;
       int cmd_match = 0;

       if (p_BTableChangesDelta->FTLCommandCount == PendingCMD[idx].Tag)
               cmd_match = 1;

       if (PendingCMD[idx].Status == CMD_PASS) {
               process_cmd_pass(first_failed_cmd, idx);
       } else if ((PendingCMD[idx].Status == CMD_FAIL) ||
                       (PendingCMD[idx].Status == CMD_ABORT)) {
               process_cmd_fail_abort(first_failed_cmd, idx, event);
       } else if ((PendingCMD[idx].Status == CMD_NOT_DONE) &&
                                       PendingCMD[idx].Tag) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       " Command no. %hu is not executed\n",
                       (unsigned int)PendingCMD[idx].Tag);
               ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
               while (ftl_cmd <= PendingCMD[idx].Tag) {
                       p_BTableChangesDelta += 1;
                       ftl_cmd = p_BTableChangesDelta->FTLCommandCount;
               }
       }
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:            GLOB_FTL_Event_Status
* Inputs:       none
* Outputs:      Event Code
* Description: It is called by SBD after hardware interrupt signalling
*               completion of commands chain
*               It does following things
*               get event status from LLD
*               analyze command chain status
*               determine last command executed
*               analyze results
*               rebuild the block table in case of uncorrectable error
*               return event code
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Event_Status(int *first_failed_cmd)
{
       int event_code = PASS;
       u16 i_P;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       *first_failed_cmd = 0;

       event_code = GLOB_LLD_Event_Status();
       nand_dbg_print(NAND_DBG_DEBUG, "Event Code got from lld %d\n",
               event_code);

       switch (event_code) {
       case EVENT_PASS:
               nand_dbg_print(NAND_DBG_DEBUG, "Handling EVENT_PASS\n");
               break;
       case EVENT_CORRECTABLE_DATA_ERROR_FIXED:
               nand_dbg_print(NAND_DBG_DEBUG, "Handling "
                              "EVENT_CORRECTABLE_DATA_ERROR_FIXED");
               return event_code;
       case EVENT_UNCORRECTABLE_DATA_ERROR:
       case EVENT_PROGRAM_FAILURE:
       case EVENT_ERASE_FAILURE:
               nand_dbg_print(NAND_DBG_DEBUG, "Handling Ugly case\n");
               nand_dbg_print(NAND_DBG_DEBUG, "UNCORRECTABLE "
                              "DATA ERROR HAS HAPPENED\n");
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta;
               for (i_P = MAX_CHANS; i_P < (FTLCommandCount + MAX_CHANS);
                               i_P++)
                       process_cmd(first_failed_cmd, i_P, event_code);
               memcpy(g_pBlockTable, g_pBTStartingCopy,
                       DeviceInfo.wDataBlockNum * sizeof(u32));
               memcpy(g_pWearCounter, g_pWearCounterCopy,
                       DeviceInfo.wDataBlockNum * sizeof(u8));
               if (DeviceInfo.MLCDevice)
                       memcpy(g_pReadCounter, g_pReadCounterCopy,
                               DeviceInfo.wDataBlockNum * sizeof(u16));
               FTL_Write_Block_Table(FAIL);
               break;
       default:
               nand_dbg_print(NAND_DBG_DEBUG, "Handling default case\n");
               event_code = FAIL;
               break;
       }

       memcpy(g_pBTStartingCopy, g_pBlockTable,
               DeviceInfo.wDataBlockNum * sizeof(u32));
       memcpy(g_pWearCounterCopy, g_pWearCounter,
               DeviceInfo.wDataBlockNum * sizeof(u8));
       if (DeviceInfo.MLCDevice)
               memcpy(g_pReadCounterCopy, g_pReadCounter,
                       DeviceInfo.wDataBlockNum * sizeof(u16));

       g_pBTDelta_Free = g_pBTDelta;
       FTLCommandCount = 0;
       g_pNextBlockTable = g_pBlockTableCopies;
       g_pCopyBackBufferStart = g_pCopyBackBufferCopies;

       return event_code;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:            GLOB_FTL_Enable_Disable_Interrupts
* Inputs:       enable or disable
* Outputs:      none
* Description: pass thru to LLD
**************************************************************/
void GLOB_FTL_Enable_Disable_Interrupts(u16 int_enable)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       GLOB_LLD_Enable_Disable_Interrupts(int_enable);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Execute_CMDS
* Inputs:       none
* Outputs:      none
* Description:  pass thru to LLD
***************************************************************/
void GLOB_FTL_Execute_CMDS(void)
{
       nand_dbg_print(NAND_DBG_TRACE,
               "GLOB_FTL_Execute_CMDS: FTLCommandCount %u\n",
               (unsigned int)FTLCommandCount);
       g_SBDCmdIndex = 0;
       GLOB_LLD_Execute_CMDs(FTLCommandCount);
}

#endif

#if !CMD_DMA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Read Immediate
* Inputs:         pointer to data
*                     address of data
* Outputs:      PASS / FAIL
* Description:  Reads one page of data into RAM directly from flash without
*       using or disturbing cache.It is assumed this function is called
*       with CMD-DMA disabled.
*****************************************************************/
int GLOB_FTL_Read_Immediate(u8 *read_data, u64 addr)
{
       int wResult = FAIL;
       u32 Block;
       u16 Page;
       u32 phy_blk;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       Block = BLK_FROM_ADDR(addr);
       Page = PAGE_FROM_ADDR(addr, Block);

       if (!IS_SPARE_BLOCK(Block))
               return FAIL;

       phy_blk = pbt[Block];
       wResult = GLOB_LLD_Read_Page_Main(read_data, phy_blk, Page, 1);

       if (DeviceInfo.MLCDevice) {
               g_pReadCounter[phy_blk - DeviceInfo.wSpectraStartBlock]++;
               if (g_pReadCounter[phy_blk - DeviceInfo.wSpectraStartBlock]
                       >= MAX_READ_COUNTER)
                       FTL_Read_Disturbance(phy_blk);
               if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }
       }

       return wResult;
}
#endif

#ifdef SUPPORT_BIG_ENDIAN
/*********************************************************************
* Function:     FTL_Invert_Block_Table
* Inputs:       none
* Outputs:      none
* Description:  Re-format the block table in ram based on BIG_ENDIAN and
*                     LARGE_BLOCKNUM if necessary
**********************************************************************/
static void FTL_Invert_Block_Table(void)
{
       u32 i;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

#ifdef SUPPORT_LARGE_BLOCKNUM
       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               pbt[i] = INVERTUINT32(pbt[i]);
               g_pWearCounter[i] = INVERTUINT32(g_pWearCounter[i]);
       }
#else
       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               pbt[i] = INVERTUINT16(pbt[i]);
               g_pWearCounter[i] = INVERTUINT16(g_pWearCounter[i]);
       }
#endif
}
#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Flash_Init
* Inputs:       none
* Outputs:      PASS=0 / FAIL=0x01 (based on read ID)
* Description:  The flash controller is initialized
*               The flash device is reset
*               Perform a flash READ ID command to confirm that a
*                   valid device is attached and active.
*                   The DeviceInfo structure gets filled in
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flash_Init(void)
{
       int status = FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

#if CMD_DMA
       GLOB_LLD_Flash_Init(LLD_CMD_FLAG_MODE_POLL);
#else
       GLOB_LLD_Flash_Init();
#endif
       status = GLOB_LLD_Read_Device_ID();

       return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Inputs:       none
* Outputs:      PASS=0 / FAIL=0x01 (based on read ID)
* Description:  The flash controller is released
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flash_Release(void)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       return GLOB_LLD_Flash_Release();
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Cache_Release
* Inputs:       none
* Outputs:      none
* Description:  release all allocated memory in GLOB_FTL_Init
*               (allocated in GLOB_FTL_Init)
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void GLOB_FTL_Cache_Release(void)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);
       return;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_If_Hit
* Inputs:       Page Address
* Outputs:      Block number/UNHIT BLOCK
* Description:  Determines if the addressed page is in cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u8 FTL_Cache_If_Hit(u64 page_addr)
{
       u8 i, blk;
       u64 addr;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       blk = UNHIT_BLOCK;
       for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
               addr = Cache.ItemArray[i].dwAddress;
               if ((addr <= page_addr) &&
                       (addr + Cache.dwCacheDataSize > page_addr)) {
                       blk = i;
                       break;
               }
       }

       return blk;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Calculate_LRU
* Inputs:       None
* Outputs:      None
* Description:  Calculate the least recently block in a cache and record its
*               index in bLRU field.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Calculate_LRU(void)
{
       u8 i, bCurrentLRU, bTempCount;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       bCurrentLRU = 0;
       bTempCount = MAX_BYTE_VALUE;

       for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
               if (Cache.ItemArray[i].bLRUCount < bTempCount) {
                       bCurrentLRU = i;
                       bTempCount = Cache.ItemArray[i].bLRUCount;
               }
       }

       Cache.bLRU = bCurrentLRU;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read_Page
* Inputs:       pointer to read buffer,page address and block number in a cache
* Outputs:      None
* Description:  Read the page from the cached block addressed by blocknumber
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Cache_Read_Page(u8 *pData, u64 dwPageAddr,
                                       u8 cache_blk)
{
       u8 *pSrc;
       u64 addr;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       addr = Cache.ItemArray[cache_blk].dwAddress;
       pSrc = Cache.ItemArray[cache_blk].pContent;
       pSrc += (unsigned long)(((dwPageAddr - addr) >>
               DeviceInfo.nBitsInPageDataSize) * DeviceInfo.wPageDataSize);

#if CMD_DMA
       GLOB_LLD_MemCopy_CMD(FTLCommandCount, pData, pSrc,
                       DeviceInfo.wPageDataSize, 0);
       FTLCommandCount++;
#else
       memcpy(pData, pSrc, DeviceInfo.wPageDataSize);
#endif

       if (Cache.ItemArray[cache_blk].bLRUCount < MAX_BYTE_VALUE)
               Cache.ItemArray[cache_blk].bLRUCount++;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read_All
* Inputs:       pointer to read buffer,block address
* Outputs:      PASS=0 / FAIL =1
* Description:  It reads pages in cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Read_All(u8 *pData, u64 blk_addr)
{
       int wResult;
       u32 Block;
       u32 lba = BAD_BLOCK;
       u16 Page;
       u16 PageCount;
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 i;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       wResult = PASS;

       Block = BLK_FROM_ADDR(blk_addr);
       Page = PAGE_FROM_ADDR(blk_addr, Block);

       PageCount = Cache.wCachePageNum;

       nand_dbg_print(NAND_DBG_DEBUG,
               "FTL_Cache_Read_All: Reading Block %u\n",
               (unsigned int)Block);

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               if (Block == (pbt[i] & (~BAD_BLOCK))) {
                       lba = i;
                       if (IS_SPARE_BLOCK(i) || IS_BAD_BLOCK(i) ||
                               IS_DISCARDED_BLOCK(i)) {
                               /* Add by yunpeng -2008.12.3 */
#if CMD_DMA
                               GLOB_LLD_MemCopy_CMD(FTLCommandCount,
                               pData, g_temp_buf,
                               PageCount * DeviceInfo.wPageDataSize, 0);
                               FTLCommandCount++;
#else
                               memset(pData, 0xFF,
                                       PageCount * DeviceInfo.wPageDataSize);
#endif
                               return wResult;
                       } else {
                               continue;
                       }
               }
       }

       if (lba == BAD_BLOCK)
               printk(KERN_ERR "FTL_Cache_Read_All: Block is not found in BT\n");

#if CMD_DMA
       wResult = GLOB_LLD_Read_Page_Main(pData, Block, Page, PageCount,
                                           FTLCommandCount,
                                           LLD_CMD_FLAG_MODE_CDMA);
       if (DeviceInfo.MLCDevice) {
               g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock]++;
               nand_dbg_print(NAND_DBG_DEBUG,
                              "Read Counter modified in FTLCommandCount %u"
                               " Block %u Counter%u\n",
                              FTLCommandCount, (unsigned int)Block,
                              g_pReadCounter[Block -
                              DeviceInfo.wSpectraStartBlock]);

               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
               p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
               p_BTableChangesDelta->RC_Index =
                       Block - DeviceInfo.wSpectraStartBlock;
               p_BTableChangesDelta->RC_Entry_Value =
                       g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock];
               p_BTableChangesDelta->ValidFields = 0xC0;

               FTLCommandCount++;

               if (g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock] >=
                   MAX_READ_COUNTER)
                       FTL_Read_Disturbance(Block);
               if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }
       } else {
               FTLCommandCount++;
       }
#else
       wResult = GLOB_LLD_Read_Page_Main(pData, Block, Page, PageCount);
       if (wResult == FAIL)
               return wResult;

       if (DeviceInfo.MLCDevice) {
               g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock]++;
               if (g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock] >=
                                               MAX_READ_COUNTER)
                       FTL_Read_Disturbance(Block);
               if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }
       }
#endif
       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write_All
* Inputs:       pointer to cache in sys memory
*               address of free block in flash
* Outputs:      PASS=0 / FAIL=1
* Description:  writes all the pages of the block in cache to flash
*
*               NOTE:need to make sure this works ok when cache is limited
*               to a partial block. This is where copy-back would be
*               activated.  This would require knowing which pages in the
*               cached block are clean/dirty.Right now we only know if
*               the whole block is clean/dirty.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Write_All(u8 *pData, u64 blk_addr)
{
       u16 wResult = PASS;
       u32 Block;
       u16 Page;
       u16 PageCount;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       nand_dbg_print(NAND_DBG_DEBUG, "This block %d going to be written "
               "on %d\n", cache_block_to_write,
               (u32)(blk_addr >> DeviceInfo.nBitsInBlockDataSize));

       Block = BLK_FROM_ADDR(blk_addr);
       Page = PAGE_FROM_ADDR(blk_addr, Block);
       PageCount = Cache.wCachePageNum;

#if CMD_DMA
       if (FAIL == GLOB_LLD_Write_Page_Main(pData, Block, Page, PageCount,
                                            FTLCommandCount)) {
               nand_dbg_print(NAND_DBG_WARN,
                       "NAND Program fail in %s, Line %d, "
                       "Function: %s, new Bad Block %d generated! "
                       "Need Bad Block replacing.\n",
                       __FILE__, __LINE__, __func__, Block);
               wResult = FAIL;
       }
       FTLCommandCount++;
#else
       if (FAIL == GLOB_LLD_Write_Page_Main(pData, Block, Page, PageCount)) {
               nand_dbg_print(NAND_DBG_WARN, "NAND Program fail in %s,"
                       " Line %d, Function %s, new Bad Block %d generated!"
                       "Need Bad Block replacing.\n",
                       __FILE__, __LINE__, __func__, Block);
               wResult = FAIL;
       }
#endif
       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Update_Block
* Inputs:       pointer to buffer,page address,block address
* Outputs:      PASS=0 / FAIL=1
* Description:  It updates the cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Update_Block(u8 *pData,
                       u64 old_page_addr, u64 blk_addr)
{
       int i, j;
       u8 *buf = pData;
       int wResult = PASS;
       int wFoundInCache;
       u64 page_addr;
       u64 addr;
       u64 old_blk_addr;
       u16 page_offset;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                               __FILE__, __LINE__, __func__);

       old_blk_addr = (u64)(old_page_addr >>
               DeviceInfo.nBitsInBlockDataSize) * DeviceInfo.wBlockDataSize;
       page_offset = (u16)(GLOB_u64_Remainder(old_page_addr, 2) >>
               DeviceInfo.nBitsInPageDataSize);

       for (i = 0; i < DeviceInfo.wPagesPerBlock; i += Cache.wCachePageNum) {
               page_addr = old_blk_addr + i * DeviceInfo.wPageDataSize;
               if (i != page_offset) {
                       wFoundInCache = FAIL;
                       for (j = 0; j < CACHE_BLOCK_NUMBER; j++) {
                               addr = Cache.ItemArray[j].dwAddress;
                               addr = FTL_Get_Physical_Block_Addr(addr) +
                                       GLOB_u64_Remainder(addr, 2);
                               if ((addr >= page_addr) && addr <
                                       (page_addr + Cache.dwCacheDataSize)) {
                                       wFoundInCache = PASS;
                                       buf = Cache.ItemArray[j].pContent;
                                       Cache.ItemArray[j].bChanged = SET;
                                       break;
                               }
                       }
                       if (FAIL == wFoundInCache) {
                               if (ERR == FTL_Cache_Read_All(g_pTempBuf,
                                       page_addr)) {
                                       wResult = FAIL;
                                       break;
                               }
                               buf = g_pTempBuf;
                       }
               } else {
                       buf = pData;
               }

               if (FAIL == FTL_Cache_Write_All(buf,
                       blk_addr + (page_addr - old_blk_addr))) {
                       wResult = FAIL;
                       break;
               }
       }

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Copy_Block
* Inputs:       source block address
*               Destination block address
* Outputs:      PASS=0 / FAIL=1
* Description:  used only for static wear leveling to move the block
*               containing static data to new blocks(more worn)
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Copy_Block(u64 old_blk_addr, u64 blk_addr)
{
       int i, r1, r2, wResult = PASS;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       for (i = 0; i < DeviceInfo.wPagesPerBlock; i += Cache.wCachePageNum) {
               r1 = FTL_Cache_Read_All(g_pTempBuf, old_blk_addr +
                                       i * DeviceInfo.wPageDataSize);
               r2 = FTL_Cache_Write_All(g_pTempBuf, blk_addr +
                                       i * DeviceInfo.wPageDataSize);
               if ((ERR == r1) || (FAIL == r2)) {
                       wResult = FAIL;
                       break;
               }
       }

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write_Back
* Inputs:       pointer to data cached in sys memory
*               address of free block in flash
* Outputs:      PASS=0 / FAIL=1
* Description:  writes all the pages of Cache Block to flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Write_Back(u8 *pData, u64 blk_addr)
{
       int i, j, iErase;
       u64 old_page_addr, addr, phy_addr;
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 lba;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       old_page_addr = FTL_Get_Physical_Block_Addr(blk_addr) +
               GLOB_u64_Remainder(blk_addr, 2);

       iErase = (FAIL == FTL_Replace_Block(blk_addr)) ? PASS : FAIL;

       pbt[BLK_FROM_ADDR(blk_addr)] &= (~SPARE_BLOCK);

#if CMD_DMA
       p_BTableChangesDelta = (struct BTableChangesDelta *)g_pBTDelta_Free;
       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

       p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
       p_BTableChangesDelta->BT_Index = (u32)(blk_addr >>
               DeviceInfo.nBitsInBlockDataSize);
       p_BTableChangesDelta->BT_Entry_Value =
               pbt[(u32)(blk_addr >> DeviceInfo.nBitsInBlockDataSize)];
       p_BTableChangesDelta->ValidFields = 0x0C;
#endif

       if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
               g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
               FTL_Write_IN_Progress_Block_Table_Page();
       }

       for (i = 0; i < RETRY_TIMES; i++) {
               if (PASS == iErase) {
                       phy_addr = FTL_Get_Physical_Block_Addr(blk_addr);
                       if (FAIL == GLOB_FTL_Block_Erase(phy_addr)) {
                               lba = BLK_FROM_ADDR(blk_addr);
                               MARK_BLOCK_AS_BAD(pbt[lba]);
                               i = RETRY_TIMES;
                               break;
                       }
               }

               for (j = 0; j < CACHE_BLOCK_NUMBER; j++) {
                       addr = Cache.ItemArray[j].dwAddress;
                       if ((addr <= blk_addr) &&
                               ((addr + Cache.dwCacheDataSize) > blk_addr))
                               cache_block_to_write = j;
               }

               phy_addr = FTL_Get_Physical_Block_Addr(blk_addr);
               if (PASS == FTL_Cache_Update_Block(pData,
                                       old_page_addr, phy_addr)) {
                       cache_block_to_write = UNHIT_BLOCK;
                       break;
               } else {
                       iErase = PASS;
               }
       }

       if (i >= RETRY_TIMES) {
               if (ERR == FTL_Flash_Error_Handle(pData,
                                       old_page_addr, blk_addr))
                       return ERR;
               else
                       return FAIL;
       }

       return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write_Page
* Inputs:       Pointer to buffer, page address, cache block number
* Outputs:      PASS=0 / FAIL=1
* Description:  It writes the data in Cache Block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Cache_Write_Page(u8 *pData, u64 page_addr,
                               u8 cache_blk, u16 flag)
{
       u8 *pDest;
       u64 addr;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       addr = Cache.ItemArray[cache_blk].dwAddress;
       pDest = Cache.ItemArray[cache_blk].pContent;

       pDest += (unsigned long)(page_addr - addr);
       Cache.ItemArray[cache_blk].bChanged = SET;
#if CMD_DMA
       GLOB_LLD_MemCopy_CMD(FTLCommandCount, pDest, pData,
                       DeviceInfo.wPageDataSize, flag);
       FTLCommandCount++;
#else
       memcpy(pDest, pData, DeviceInfo.wPageDataSize);
#endif
       if (Cache.ItemArray[cache_blk].bLRUCount < MAX_BYTE_VALUE)
               Cache.ItemArray[cache_blk].bLRUCount++;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  It writes least frequently used Cache block to flash if it
*               has been changed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Write(void)
{
       int i, bResult = PASS;
       u8 bNO, least_count = 0xFF;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       FTL_Calculate_LRU();

       bNO = Cache.bLRU;
       nand_dbg_print(NAND_DBG_DEBUG, "FTL_Cache_Write: "
               "Least used cache block is %d\n", bNO);

       if (SET == Cache.ItemArray[bNO].bChanged) {
               nand_dbg_print(NAND_DBG_DEBUG, "FTL_Cache_Write: Cache"
                       " Block %d containing logical block %d is dirty\n",
                       bNO,
                       (u32)(Cache.ItemArray[bNO].dwAddress >>
                       DeviceInfo.nBitsInBlockDataSize));
               bResult = FTL_Cache_Write_Back(Cache.ItemArray[bNO].pContent,
                               Cache.ItemArray[bNO].dwAddress);
               if (bResult != ERR)
                       Cache.ItemArray[bNO].bChanged = CLEAR;

               least_count = Cache.ItemArray[bNO].bLRUCount;

               for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
                       if (i == bNO)
                               continue;
                       if (Cache.ItemArray[i].bLRUCount > 0)
                               Cache.ItemArray[i].bLRUCount -= least_count;
               }
       }

       return bResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read
* Inputs:       Page address
* Outputs:      PASS=0 / FAIL=1
* Description:  It reads the block from device in Cache Bllock
*               Set the LRU count to 1
*               Mark the Cache Block as clean
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Read(u64 page_addr)
{
       u64 addr;
       u8 bNO = Cache.bLRU;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       addr = (u64)GLOB_u64_Div(page_addr, Cache.dwCacheDataSize)
               * Cache.dwCacheDataSize;
       Cache.ItemArray[bNO].bLRUCount = 1;
       Cache.ItemArray[bNO].dwAddress = addr;
       Cache.ItemArray[bNO].bChanged = CLEAR;

       nand_dbg_print(NAND_DBG_DEBUG, "FTL_Cache_Read: Logical Block %d "
               "is read into cache block no. %d\n",
               (u32)GLOB_u64_Div(Cache.ItemArray[bNO].dwAddress,
                       Cache.dwCacheDataSize),
               bNO);

       return FTL_Cache_Read_All(Cache.ItemArray[bNO].pContent,
               FTL_Get_Physical_Block_Addr(addr) +
               GLOB_u64_Remainder(addr, 2));
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Check_Block_Table
* Inputs:       ?
* Outputs:      PASS=0 / FAIL=1
* Description:  It checks the correctness of each block table entry
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Check_Block_Table(int wOldTable)
{
       u32 i;
       int wResult = PASS;
       u32 blk_idx;
       u32 *pbt = (u32 *)g_pBlockTable;

       u8 *pFlag = g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wDataBlockNum);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
               globalMemSize, 0);

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       if (NULL != pFlag) {
               memset(pFlag, FAIL, DeviceInfo.wDataBlockNum);
               for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
                       blk_idx = (u32)(pbt[i] & (~BAD_BLOCK));

                       /*
                        * 20081006/KBV - Changed to pFlag[i] reference
                        * to avoid buffer overflow
                        */

                       /*
                        * 2008-10-20 Yunpeng Note: This change avoid
                        * buffer overflow, but changed function of
                        * the code, so it should be re-write later
                        */
                       if ((blk_idx > DeviceInfo.wSpectraEndBlock) ||
                               PASS == pFlag[i]) {
                               wResult = FAIL;
                               break;
                       } else {
                               pFlag[i] = PASS;
                       }
               }
               g_pMemPoolFree -= (DeviceInfo.wDataBlockNum);
               ALIGN_DWORD_BWD(g_pMemPoolFree);
       }
       return wResult;
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_Block_Table
* Inputs:       flasg
* Outputs:      0=Block Table was updated. No write done. 1=Block write needs to
* happen. -1 Error
* Description:  It writes the block table
*               Block table always mapped to LBA 0 which inturn mapped
*               to any physical block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Write_Block_Table(int wForce)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       int wSuccess = PASS;
       u32 wTempBlockTableIndex;
       u16 bt_pages, new_bt_offset;
       u8 blockchangeoccured = 0;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

       if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus)
               return 0;

       if (PASS == wForce) {
               g_wBlockTableOffset =
                       (u16)(DeviceInfo.wPagesPerBlock - bt_pages);
#if CMD_DMA
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

               p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
               p_BTableChangesDelta->g_wBlockTableOffset =
                       g_wBlockTableOffset;
               p_BTableChangesDelta->ValidFields = 0x01;
#endif
       }

       nand_dbg_print(NAND_DBG_DEBUG,
               "Inside FTL_Write_Block_Table: block %d Page:%d\n",
               g_wBlockTableIndex, g_wBlockTableOffset);

       do {
               new_bt_offset = g_wBlockTableOffset + bt_pages + 1;
               if ((0 == (new_bt_offset % DeviceInfo.wPagesPerBlock)) ||
                       (new_bt_offset > DeviceInfo.wPagesPerBlock) ||
                       (FAIL == wSuccess)) {
                       wTempBlockTableIndex = FTL_Replace_Block_Table();
                       if (BAD_BLOCK == wTempBlockTableIndex)
                               return ERR;
                       if (!blockchangeoccured) {
                               bt_block_changed = 1;
                               blockchangeoccured = 1;
                       }

                       g_wBlockTableIndex = wTempBlockTableIndex;
                       g_wBlockTableOffset = 0;
                       pbt[BLOCK_TABLE_INDEX] = g_wBlockTableIndex;
#if CMD_DMA
                       p_BTableChangesDelta =
                               (struct BTableChangesDelta *)g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                   FTLCommandCount;
                       p_BTableChangesDelta->g_wBlockTableOffset =
                                   g_wBlockTableOffset;
                       p_BTableChangesDelta->g_wBlockTableIndex =
                                   g_wBlockTableIndex;
                       p_BTableChangesDelta->ValidFields = 0x03;

                       p_BTableChangesDelta =
                               (struct BTableChangesDelta *)g_pBTDelta_Free;
                       g_pBTDelta_Free +=
                               sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                   FTLCommandCount;
                       p_BTableChangesDelta->BT_Index =
                                   BLOCK_TABLE_INDEX;
                       p_BTableChangesDelta->BT_Entry_Value =
                                   pbt[BLOCK_TABLE_INDEX];
                       p_BTableChangesDelta->ValidFields = 0x0C;
#endif
               }

               wSuccess = FTL_Write_Block_Table_Data();
               if (FAIL == wSuccess)
                       MARK_BLOCK_AS_BAD(pbt[BLOCK_TABLE_INDEX]);
       } while (FAIL == wSuccess);

       g_cBlockTableStatus = CURRENT_BLOCK_TABLE;

       return 1;
}

/******************************************************************
* Function:     GLOB_FTL_Flash_Format
* Inputs:       none
* Outputs:      PASS
* Description:  The block table stores bad block info, including MDF+
*               blocks gone bad over the ages. Therefore, if we have a
*               block table in place, then use it to scan for bad blocks
*               If not, then scan for MDF.
*               Now, a block table will only be found if spectra was already
*               being used. For a fresh flash, we'll go thru scanning for
*               MDF. If spectra was being used, then there is a chance that
*               the MDF has been corrupted. Spectra avoids writing to the
*               first 2 bytes of the spare area to all pages in a block. This
*               covers all known flash devices. However, since flash
*               manufacturers have no standard of where the MDF is stored,
*               this cannot guarantee that the MDF is protected for future
*               devices too. The initial scanning for the block table assures
*               this. It is ok even if the block table is outdated, as all
*               we're looking for are bad block markers.
*               Use this when mounting a file system or starting a
*               new flash.
*
*********************************************************************/
static int  FTL_Format_Flash(u8 valid_block_table)
{
       u32 i, j;
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 tempNode;
       int ret;

#if CMD_DMA
       u32 *pbtStartingCopy = (u32 *)g_pBTStartingCopy;
       if (FTLCommandCount)
               return FAIL;
#endif

       if (FAIL == FTL_Check_Block_Table(FAIL))
               valid_block_table = 0;

       if (valid_block_table) {
               u8 switched = 1;
               u32 block, k;

               k = DeviceInfo.wSpectraStartBlock;
               while (switched && (k < DeviceInfo.wSpectraEndBlock)) {
                       switched = 0;
                       k++;
                       for (j = DeviceInfo.wSpectraStartBlock, i = 0;
                       j <= DeviceInfo.wSpectraEndBlock;
                       j++, i++) {
                               block = (pbt[i] & ~BAD_BLOCK) -
                                       DeviceInfo.wSpectraStartBlock;
                               if (block != i) {
                                       switched = 1;
                                       tempNode = pbt[i];
                                       pbt[i] = pbt[block];
                                       pbt[block] = tempNode;
                               }
                       }
               }
               if ((k == DeviceInfo.wSpectraEndBlock) && switched)
                       valid_block_table = 0;
       }

       if (!valid_block_table) {
               memset(g_pBlockTable, 0,
                       DeviceInfo.wDataBlockNum * sizeof(u32));
               memset(g_pWearCounter, 0,
                       DeviceInfo.wDataBlockNum * sizeof(u8));
               if (DeviceInfo.MLCDevice)
                       memset(g_pReadCounter, 0,
                               DeviceInfo.wDataBlockNum * sizeof(u16));
#if CMD_DMA
               memset(g_pBTStartingCopy, 0,
                       DeviceInfo.wDataBlockNum * sizeof(u32));
               memset(g_pWearCounterCopy, 0,
                               DeviceInfo.wDataBlockNum * sizeof(u8));
               if (DeviceInfo.MLCDevice)
                       memset(g_pReadCounterCopy, 0,
                               DeviceInfo.wDataBlockNum * sizeof(u16));
#endif

#if READ_BADBLOCK_INFO
               for (j = DeviceInfo.wSpectraStartBlock, i = 0;
                       j <= DeviceInfo.wSpectraEndBlock;
                       j++, i++) {
                       if (GLOB_LLD_Get_Bad_Block((u32)j))
                               pbt[i] = (u32)(BAD_BLOCK | j);
               }
#endif
       }

       nand_dbg_print(NAND_DBG_WARN, "Erasing all blocks in the NAND\n");

       for (j = DeviceInfo.wSpectraStartBlock, i = 0;
               j <= DeviceInfo.wSpectraEndBlock;
               j++, i++) {
               if ((pbt[i] & BAD_BLOCK) != BAD_BLOCK) {
#if CMD_DMA
                       ret = GLOB_LLD_Erase_Block(j, FTLCommandCount,
                                               LLD_CMD_FLAG_MODE_POLL);
#else
                       ret = GLOB_LLD_Erase_Block(j);
#endif
                       if (FAIL == ret) {
                               pbt[i] = (u32)(j);
                               MARK_BLOCK_AS_BAD(pbt[i]);
                               nand_dbg_print(NAND_DBG_WARN,
                              "NAND Program fail in %s, Line %d, "
                              "Function: %s, new Bad Block %d generated!\n",
                              __FILE__, __LINE__, __func__, (int)j);
                       } else {
                               pbt[i] = (u32)(SPARE_BLOCK | j);
                       }
               }
#if CMD_DMA
               pbtStartingCopy[i] = pbt[i];
#endif
       }

       g_wBlockTableOffset = 0;
       for (i = 0; (i <= (DeviceInfo.wSpectraEndBlock -
                       DeviceInfo.wSpectraStartBlock))
                       && ((pbt[i] & BAD_BLOCK) == BAD_BLOCK); i++)
               ;
       if (i > (DeviceInfo.wSpectraEndBlock - DeviceInfo.wSpectraStartBlock)) {
               printk(KERN_ERR "All blocks bad!\n");
               return FAIL;
       } else {
               g_wBlockTableIndex = pbt[i] & ~BAD_BLOCK;
               if (i != BLOCK_TABLE_INDEX) {
                       tempNode = pbt[i];
                       pbt[i] = pbt[BLOCK_TABLE_INDEX];
                       pbt[BLOCK_TABLE_INDEX] = tempNode;
               }
       }
       pbt[BLOCK_TABLE_INDEX] &= (~SPARE_BLOCK);

#if CMD_DMA
       pbtStartingCopy[BLOCK_TABLE_INDEX] &= (~SPARE_BLOCK);
#endif

       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
       memset(g_pBTBlocks, 0xFF,
                       (1 + LAST_BT_ID - FIRST_BT_ID) * sizeof(u32));
       g_pBTBlocks[FIRST_BT_ID-FIRST_BT_ID] = g_wBlockTableIndex;
       FTL_Write_Block_Table(FAIL);

       for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
               Cache.ItemArray[i].dwAddress = NAND_CACHE_INIT_ADDR;
               Cache.ItemArray[i].bLRUCount = 0;
               Cache.ItemArray[i].bChanged  = CLEAR;
       }

       return PASS;
}

int GLOB_FTL_Flash_Format(void)
{
       return FTL_Format_Flash(1);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Search_Block_Table_IN_Block
* Inputs:       Block Number
*               Pointer to page
* Outputs:      PASS / FAIL
*               Page contatining the block table
* Description:  It searches the block table in the block
*               passed as an argument.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Search_Block_Table_IN_Block(u32 BT_Block,
                                               u8 BT_Tag, u16 *Page)
{
       u16 i, j, k;
       u16 Result = PASS;
       u16 Last_IPF = 0;
       u8   BT_Found = 0;
       u8 *tempbuf, *tagarray;
       u8 *pSpareBuf;
       u8 *pSpareBufBTLastPage;
       u8 bt_flag_last_page = 0xFF;
       u8 search_in_previous_pages = 0;
       u16 bt_pages;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

       tempbuf = g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wPageDataSize*sizeof(u8));
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       pSpareBuf = g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wPageSize - DeviceInfo.wPageDataSize) *
           sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       pSpareBufBTLastPage = g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wPageSize - DeviceInfo.wPageDataSize) *
           sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
               globalMemSize, 0);

       nand_dbg_print(NAND_DBG_DEBUG,
                      "FTL_Search_Block_Table_IN_Block: "
                      "Searching block table in %u block\n",
                      (unsigned int)BT_Block);

       for (i = bt_pages; i < DeviceInfo.wPagesPerBlock;
                               i += (bt_pages + 1)) {
#if CMD_DMA
               nand_dbg_print(NAND_DBG_DEBUG,
                              "Searching last IPF: %d\n", i);
               Result = GLOB_LLD_Read_Page_Main(tempbuf,
                       BT_Block, i, 1, FTLCommandCount,
                       LLD_CMD_FLAG_MODE_POLL);
#else
               nand_dbg_print(NAND_DBG_DEBUG,
                              "Searching last IPF: %d\n", i);
               Result = GLOB_LLD_Read_Page_Main_Polling(tempbuf,
                                                       BT_Block, i, 1);
#endif
               if (0 == memcmp(tempbuf, g_pIPF, DeviceInfo.wPageDataSize)) {
                       if ((i + bt_pages + 1) < DeviceInfo.wPagesPerBlock) {
                               continue;
                       } else {
                               search_in_previous_pages = 1;
                               Last_IPF = i;
                       }
               }

               if (!search_in_previous_pages) {
                       if (i != bt_pages) {
                               i -= (bt_pages + 1);
                               Last_IPF = i;
                       }
               }

               if (0 == Last_IPF)
                       break;

               if (!search_in_previous_pages) {
                       i = i + 1;
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "Reading the spare area of Block %u Page %u",
                               (unsigned int)BT_Block, i);
                       Result = GLOB_LLD_Read_Page_Spare(pSpareBuf,
                                                       BT_Block, i, 1);
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "Reading the spare area of Block %u Page %u",
                               (unsigned int)BT_Block, i + bt_pages - 1);
                       Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
                               BT_Block, i + bt_pages - 1, 1);

                       k = 0;
                       j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
                       if (j) {
                               for (; k < j; k++) {
                                       if (tagarray[k] == BT_Tag)
                                               break;
                               }
                       }

                       if (k < j)
                               bt_flag = tagarray[k];
                       else
                               Result = FAIL;

                       if (Result == PASS) {
                               k = 0;
                               j = FTL_Extract_Block_Table_Tag(
                                       pSpareBufBTLastPage, &tagarray);
                               if (j) {
                                       for (; k < j; k++) {
                                               if (tagarray[k] == BT_Tag)
                                                       break;
                                       }
                               }

                               if (k < j)
                                       bt_flag_last_page = tagarray[k];
                               else
                                       Result = FAIL;

                               if (Result == PASS) {
                                       if (bt_flag == bt_flag_last_page) {
                                               nand_dbg_print(NAND_DBG_DEBUG,
                                                       "Block table is found"
                                                       " in page after IPF "
                                                       "at block %d "
                                                       "page %d\n",
                                                       (int)BT_Block, i);
                                               BT_Found = 1;
                                               *Page  = i;
                                               g_cBlockTableStatus =
                                                       CURRENT_BLOCK_TABLE;
                                               break;
                                       } else {
                                               Result = FAIL;
                                       }
                               }
                       }
               }

               if (search_in_previous_pages)
                       i = i - bt_pages;
               else
                       i = i - (bt_pages + 1);

               Result = PASS;

               nand_dbg_print(NAND_DBG_DEBUG,
                       "Reading the spare area of Block %d Page %d",
                       (int)BT_Block, i);

               Result = GLOB_LLD_Read_Page_Spare(pSpareBuf, BT_Block, i, 1);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Reading the spare area of Block %u Page %u",
                       (unsigned int)BT_Block, i + bt_pages - 1);

               Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
                                       BT_Block, i + bt_pages - 1, 1);

               k = 0;
               j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
               if (j) {
                       for (; k < j; k++) {
                               if (tagarray[k] == BT_Tag)
                                       break;
                       }
               }

               if (k < j)
                       bt_flag = tagarray[k];
               else
                       Result = FAIL;

               if (Result == PASS) {
                       k = 0;
                       j = FTL_Extract_Block_Table_Tag(pSpareBufBTLastPage,
                                               &tagarray);
                       if (j) {
                               for (; k < j; k++) {
                                       if (tagarray[k] == BT_Tag)
                                               break;
                               }
                       }

                       if (k < j) {
                               bt_flag_last_page = tagarray[k];
                       } else {
                               Result = FAIL;
                               break;
                       }

                       if (Result == PASS) {
                               if (bt_flag == bt_flag_last_page) {
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Block table is found "
                                               "in page prior to IPF "
                                               "at block %u page %d\n",
                                               (unsigned int)BT_Block, i);
                                       BT_Found = 1;
                                       *Page  = i;
                                       g_cBlockTableStatus =
                                               IN_PROGRESS_BLOCK_TABLE;
                                       break;
                               } else {
                                       Result = FAIL;
                                       break;
                               }
                       }
               }
       }

       if (Result == FAIL) {
               if ((Last_IPF > bt_pages) && (i < Last_IPF) && (!BT_Found)) {
                       BT_Found = 1;
                       *Page = i - (bt_pages + 1);
               }
               if ((Last_IPF == bt_pages) && (i < Last_IPF) && (!BT_Found))
                       goto func_return;
       }

       if (Last_IPF == 0) {
               i = 0;
               Result = PASS;
               nand_dbg_print(NAND_DBG_DEBUG, "Reading the spare area of "
                       "Block %u Page %u", (unsigned int)BT_Block, i);

               Result = GLOB_LLD_Read_Page_Spare(pSpareBuf, BT_Block, i, 1);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Reading the spare area of Block %u Page %u",
                       (unsigned int)BT_Block, i + bt_pages - 1);
               Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
                                       BT_Block, i + bt_pages - 1, 1);

               k = 0;
               j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
               if (j) {
                       for (; k < j; k++) {
                               if (tagarray[k] == BT_Tag)
                                       break;
                       }
               }

               if (k < j)
                       bt_flag = tagarray[k];
               else
                       Result = FAIL;

               if (Result == PASS) {
                       k = 0;
                       j = FTL_Extract_Block_Table_Tag(pSpareBufBTLastPage,
                                                       &tagarray);
                       if (j) {
                               for (; k < j; k++) {
                                       if (tagarray[k] == BT_Tag)
                                               break;
                               }
                       }

                       if (k < j)
                               bt_flag_last_page = tagarray[k];
                       else
                               Result = FAIL;

                       if (Result == PASS) {
                               if (bt_flag == bt_flag_last_page) {
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "Block table is found "
                                               "in page after IPF at "
                                               "block %u page %u\n",
                                               (unsigned int)BT_Block,
                                               (unsigned int)i);
                                       BT_Found = 1;
                                       *Page  = i;
                                       g_cBlockTableStatus =
                                               CURRENT_BLOCK_TABLE;
                                       goto func_return;
                               } else {
                                       Result = FAIL;
                               }
                       }
               }

               if (Result == FAIL)
                       goto func_return;
       }
func_return:
       g_pMemPoolFree -= ((DeviceInfo.wPageSize - DeviceInfo.wPageDataSize) *
                                                       sizeof(u8));
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       g_pMemPoolFree -= ((DeviceInfo.wPageSize - DeviceInfo.wPageDataSize) *
                                                       sizeof(u8));
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       g_pMemPoolFree -= ((DeviceInfo.wPageDataSize * sizeof(u8)));
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       return Result;
}

u8 *get_blk_table_start_addr(void)
{
       return g_pBlockTable;
}

unsigned long get_blk_table_len(void)
{
       return DeviceInfo.wDataBlockNum * sizeof(u32);
}

u8 *get_wear_leveling_table_start_addr(void)
{
       return g_pWearCounter;
}

unsigned long get_wear_leveling_table_len(void)
{
       return DeviceInfo.wDataBlockNum * sizeof(u8);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Read_Block_Table
* Inputs:       none
* Outputs:      PASS / FAIL
* Description:  read the flash spare area and find a block containing the
*               most recent block table(having largest block_table_counter).
*               Find the last written Block table in this block.
*               Check the correctness of Block Table
*               If CDMA is enabled, this function is called in
*               polling mode.
*               We don't need to store changes in Block table in this
*               function as it is called only at initialization
*
*               Note: Currently this function is called at initialization
*               before any read/erase/write command issued to flash so,
*               there is no need to wait for CDMA list to complete as of now
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Read_Block_Table(void)
{
       int k;
       u16 i;
       int j;
       u8 *tempBuf, *tagarray;
       int wResult = FAIL;
       int status = FAIL;
       u8 block_table_found = 0;
       int search_result;
       u32 Block;
       u16 Page = 0;
       u16 PageCount;
       u16 bt_pages;
       int wBytesCopied = 0, tempvar;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

       tempBuf = g_pMemPoolFree;
       g_pMemPoolFree += DeviceInfo.wPageDataSize * sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
               globalMemSize, 0);

       for (j = DeviceInfo.wSpectraStartBlock;
               j <= (int)DeviceInfo.wSpectraEndBlock;
                       j++) {
               status = GLOB_LLD_Read_Page_Spare(tempBuf, j, 0, 1);
               k = 0;
               i = FTL_Extract_Block_Table_Tag(tempBuf, &tagarray);
               if (i) {
#if CMD_DMA
                       status = GLOB_LLD_Read_Page_Main(tempBuf, j, 0, 1,
                               FTLCommandCount, LLD_CMD_FLAG_MODE_POLL);
#else
                       status  = GLOB_LLD_Read_Page_Main_Polling(tempBuf,
                                                               j, 0, 1);
#endif
                       for (; k < i; k++) {
                               if (tagarray[k] == tempBuf[3])
                                       break;
                       }
               }

               if (k < i)
                       k = tagarray[k];
               else
                       continue;

               nand_dbg_print(NAND_DBG_DEBUG,
                               "Block table is contained in Block %d %d\n",
                                      (unsigned int)j, (unsigned int)k);

               if (g_pBTBlocks[k-FIRST_BT_ID] == BTBLOCK_INVAL) {
                       g_pBTBlocks[k-FIRST_BT_ID] = j;
                       block_table_found = 1;
               } else {
                       printk(KERN_ERR "FTL_Read_Block_Table -"
                               "This should never happens. "
                               "Two block table have same counter %u!\n", k);
               }
       }

       g_pMemPoolFree -= DeviceInfo.wPageDataSize * sizeof(u8);
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       if (block_table_found) {
               if (g_pBTBlocks[FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL &&
               g_pBTBlocks[LAST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL) {
                       j = LAST_BT_ID;
                       while ((j > FIRST_BT_ID) &&
                       (g_pBTBlocks[j - FIRST_BT_ID] != BTBLOCK_INVAL))
                               j--;
                       if (j == FIRST_BT_ID) {
                               j = LAST_BT_ID;
                               last_erased = LAST_BT_ID;
                       } else {
                               last_erased = (u8)j + 1;
                               while ((j > FIRST_BT_ID) && (BTBLOCK_INVAL ==
                                       g_pBTBlocks[j - FIRST_BT_ID]))
                                       j--;
                       }
               } else {
                       j = FIRST_BT_ID;
                       while (g_pBTBlocks[j - FIRST_BT_ID] == BTBLOCK_INVAL)
                               j++;
                       last_erased = (u8)j;
                       while ((j < LAST_BT_ID) && (BTBLOCK_INVAL !=
                               g_pBTBlocks[j - FIRST_BT_ID]))
                               j++;
                       if (g_pBTBlocks[j-FIRST_BT_ID] == BTBLOCK_INVAL)
                               j--;
               }

               if (last_erased > j)
                       j += (1 + LAST_BT_ID - FIRST_BT_ID);

               for (; (j >= last_erased) && (FAIL == wResult); j--) {
                       i = (j - FIRST_BT_ID) %
                               (1 + LAST_BT_ID - FIRST_BT_ID);
                       search_result =
                       FTL_Search_Block_Table_IN_Block(g_pBTBlocks[i],
                                               i + FIRST_BT_ID, &Page);
                       if (g_cBlockTableStatus == IN_PROGRESS_BLOCK_TABLE)
                               block_table_found = 0;

                       while ((search_result == PASS) && (FAIL == wResult)) {
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "FTL_Read_Block_Table:"
                                       "Block: %u Page: %u "
                                       "contains block table\n",
                                       (unsigned int)g_pBTBlocks[i],
                                       (unsigned int)Page);

                               tempBuf = g_pMemPoolFree;
                               g_pMemPoolFree += DeviceInfo.wPageDataSize *
                                               sizeof(u8);
                               ALIGN_DWORD_FWD(g_pMemPoolFree);
                               debug_boundary_error(((int)g_pMemPoolFree -
                                       (int)g_pMemPool) - 1,
                                       globalMemSize, 0);

                               for (k = 0; k < bt_pages; k++) {
                                       Block = g_pBTBlocks[i];
                                       PageCount = 1;
#if CMD_DMA
                                       status  = GLOB_LLD_Read_Page_Main(
                                       tempBuf, Block, Page, PageCount,
                                       FTLCommandCount,
                                       LLD_CMD_FLAG_MODE_POLL);
#else
                                       status  =
                                       GLOB_LLD_Read_Page_Main_Polling(
                                       tempBuf, Block, Page, PageCount);
#endif
                                       tempvar = k ? 0 : 4;

                                       wBytesCopied +=
                                       FTL_Copy_Block_Table_From_Flash(
                                       tempBuf + tempvar,
                                       DeviceInfo.wPageDataSize - tempvar,
                                       wBytesCopied);

                                       Page++;
                               }

                               g_pMemPoolFree -= DeviceInfo.wPageDataSize *
                                                       sizeof(u8);
                               ALIGN_DWORD_BWD(g_pMemPoolFree);

                               wResult = FTL_Check_Block_Table(FAIL);
                               if (FAIL == wResult) {
                                       block_table_found = 0;
                                       if (Page > bt_pages)
                                               Page -= ((bt_pages<<1) + 1);
                                       else
                                               search_result = FAIL;
                               }
                       }
               }
       }

       if (PASS == wResult) {
               if (!block_table_found)
                       FTL_Execute_SPL_Recovery();

               if (g_cBlockTableStatus == IN_PROGRESS_BLOCK_TABLE)
                       g_wBlockTableOffset = (u16)Page + 1;
               else
                       g_wBlockTableOffset = (u16)Page - bt_pages;

               g_wBlockTableIndex = (u32)g_pBTBlocks[i];

#if CMD_DMA
               if (DeviceInfo.MLCDevice)
                       memcpy(g_pBTStartingCopy, g_pBlockTable,
                               DeviceInfo.wDataBlockNum * sizeof(u32)
                               + DeviceInfo.wDataBlockNum * sizeof(u8)
                               + DeviceInfo.wDataBlockNum * sizeof(u16));
               else
                       memcpy(g_pBTStartingCopy, g_pBlockTable,
                               DeviceInfo.wDataBlockNum * sizeof(u32)
                               + DeviceInfo.wDataBlockNum * sizeof(u8));
#endif
       }

       if (FAIL == wResult)
               printk(KERN_ERR "Yunpeng - "
               "Can not find valid spectra block table!\n");

#if CMD_DMA
       GLOB_LLD_Flash_Init(LLD_CMD_FLAG_MODE_CDMA);
#endif

#if AUTO_FORMAT_FLASH
       if (FAIL == wResult) {
               nand_dbg_print(NAND_DBG_DEBUG, "doing auto-format\n");
               wResult = FTL_Format_Flash(0);
       }
#endif

       return wResult;
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Flash_Error_Handle
* Inputs:       Pointer to data
*               Page address
*               Block address
* Outputs:      PASS=0 / FAIL=1
* Description:  It handles any error occured during Spectra operation
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Flash_Error_Handle(u8 *pData, u64 old_page_addr,
                               u64 blk_addr)
{
       u32 i;
       int j;
       u32 tmp_node, blk_node = BLK_FROM_ADDR(blk_addr);
       u64 phy_addr;
       int wErase = FAIL;
       int wResult = FAIL;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       if (ERR == GLOB_FTL_Garbage_Collection())
               return ERR;

       do {
               for (i = DeviceInfo.wSpectraEndBlock -
                       DeviceInfo.wSpectraStartBlock;
                                       i > 0; i--) {
                       if (IS_SPARE_BLOCK(i)) {
                               tmp_node = (u32)(BAD_BLOCK |
                                       pbt[blk_node]);
                               pbt[blk_node] = (u32)(pbt[i] &
                                       (~SPARE_BLOCK));
                               pbt[i] = tmp_node;
#if CMD_DMA
                               p_BTableChangesDelta =
                                   (struct BTableChangesDelta *)
                                   g_pBTDelta_Free;
                               g_pBTDelta_Free +=
                                   sizeof(struct BTableChangesDelta);

                               p_BTableChangesDelta->FTLCommandCount =
                                   FTLCommandCount;
                               p_BTableChangesDelta->BT_Index =
                                   blk_node;
                               p_BTableChangesDelta->BT_Entry_Value =
                                   pbt[blk_node];
                               p_BTableChangesDelta->ValidFields = 0x0C;

                               p_BTableChangesDelta =
                                   (struct BTableChangesDelta *)
                                   g_pBTDelta_Free;
                               g_pBTDelta_Free +=
                                   sizeof(struct BTableChangesDelta);

                               p_BTableChangesDelta->FTLCommandCount =
                                   FTLCommandCount;
                               p_BTableChangesDelta->BT_Index = i;
                               p_BTableChangesDelta->BT_Entry_Value = pbt[i];
                               p_BTableChangesDelta->ValidFields = 0x0C;
#endif
                               wResult = PASS;
                               break;
                       }
               }

               if (FAIL == wResult) {
                       if (FAIL == GLOB_FTL_Garbage_Collection())
                               break;
                       else
                               continue;
               }

               if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }

               phy_addr = FTL_Get_Physical_Block_Addr(blk_addr);

               for (j = 0; j < RETRY_TIMES; j++) {
                       if (PASS == wErase) {
                               if (FAIL == GLOB_FTL_Block_Erase(phy_addr)) {
                                       MARK_BLOCK_AS_BAD(pbt[blk_node]);
                                       break;
                               }
                       }
                       if (PASS == FTL_Cache_Update_Block(pData,
                                                          old_page_addr,
                                                          phy_addr)) {
                               wResult = PASS;
                               break;
                       } else {
                               wResult = FAIL;
                               wErase = PASS;
                       }
               }
       } while (FAIL == wResult);

       FTL_Write_Block_Table(FAIL);

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Page_Num
* Inputs:       Size in bytes
* Outputs:      Size in pages
* Description:  It calculates the pages required for the length passed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Get_Page_Num(u64 length)
{
       return (u32)((length >> DeviceInfo.nBitsInPageDataSize) +
               (GLOB_u64_Remainder(length , 1) > 0 ? 1 : 0));
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Physical_Block_Addr
* Inputs:       Block Address (byte format)
* Outputs:      Physical address of the block.
* Description:  It translates LBA to PBA by returning address stored
*               at the LBA location in the block table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u64 FTL_Get_Physical_Block_Addr(u64 blk_addr)
{
       u32 *pbt;
       u64 physical_addr;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       pbt = (u32 *)g_pBlockTable;
       physical_addr = (u64) DeviceInfo.wBlockDataSize *
               (pbt[BLK_FROM_ADDR(blk_addr)] & (~BAD_BLOCK));

       return physical_addr;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Block_Index
* Inputs:       Physical Block no.
* Outputs:      Logical block no. /BAD_BLOCK
* Description:  It returns the logical block no. for the PBA passed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Get_Block_Index(u32 wBlockNum)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 i;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++)
               if (wBlockNum == (pbt[i] & (~BAD_BLOCK)))
                       return i;

       return BAD_BLOCK;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Wear_Leveling
* Inputs:       none
* Outputs:      PASS=0
* Description:  This is static wear leveling (done by explicit call)
*               do complete static wear leveling
*               do complete garbage collection
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Wear_Leveling(void)
{
       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       FTL_Static_Wear_Leveling();
       GLOB_FTL_Garbage_Collection();

       return PASS;
}

static void find_least_most_worn(u8 *chg,
       u32 *least_idx, u8 *least_cnt,
       u32 *most_idx, u8 *most_cnt)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 idx;
       u8 cnt;
       int i;

       for (i = BLOCK_TABLE_INDEX + 1; i < DeviceInfo.wDataBlockNum; i++) {
               if (IS_BAD_BLOCK(i) || PASS == chg[i])
                       continue;

               idx = (u32) ((~BAD_BLOCK) & pbt[i]);
               cnt = g_pWearCounter[idx - DeviceInfo.wSpectraStartBlock];

               if (IS_SPARE_BLOCK(i)) {
                       if (cnt > *most_cnt) {
                               *most_cnt = cnt;
                               *most_idx = idx;
                       }
               }

               if (IS_DATA_BLOCK(i)) {
                       if (cnt < *least_cnt) {
                               *least_cnt = cnt;
                               *least_idx = idx;
                       }
               }

               if (PASS == chg[*most_idx] || PASS == chg[*least_idx]) {
                       debug_boundary_error(*most_idx,
                               DeviceInfo.wDataBlockNum, 0);
                       debug_boundary_error(*least_idx,
                               DeviceInfo.wDataBlockNum, 0);
                       continue;
               }
       }
}

static int move_blks_for_wear_leveling(u8 *chg,
       u32 *least_idx, u32 *rep_blk_num, int *result)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 rep_blk;
       int j, ret_cp_blk, ret_erase;
       int ret = PASS;

       chg[*least_idx] = PASS;
       debug_boundary_error(*least_idx, DeviceInfo.wDataBlockNum, 0);

       rep_blk = FTL_Replace_MWBlock();
       if (rep_blk != BAD_BLOCK) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "More than two spare blocks exist so do it\n");
               nand_dbg_print(NAND_DBG_DEBUG, "Block Replaced is %d\n",
                               rep_blk);

               chg[rep_blk] = PASS;

               if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }

               for (j = 0; j < RETRY_TIMES; j++) {
                       ret_cp_blk = FTL_Copy_Block((u64)(*least_idx) *
                               DeviceInfo.wBlockDataSize,
                               (u64)rep_blk * DeviceInfo.wBlockDataSize);
                       if (FAIL == ret_cp_blk) {
                               ret_erase = GLOB_FTL_Block_Erase((u64)rep_blk
                                       * DeviceInfo.wBlockDataSize);
                               if (FAIL == ret_erase)
                                       MARK_BLOCK_AS_BAD(pbt[rep_blk]);
                       } else {
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "FTL_Copy_Block == OK\n");
                               break;
                       }
               }

               if (j < RETRY_TIMES) {
                       u32 tmp;
                       u32 old_idx = FTL_Get_Block_Index(*least_idx);
                       u32 rep_idx = FTL_Get_Block_Index(rep_blk);
                       tmp = (u32)(DISCARD_BLOCK | pbt[old_idx]);
                       pbt[old_idx] = (u32)((~SPARE_BLOCK) &
                                                       pbt[rep_idx]);
                       pbt[rep_idx] = tmp;
#if CMD_DMA
                       p_BTableChangesDelta = (struct BTableChangesDelta *)
                                               g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
                       p_BTableChangesDelta->FTLCommandCount =
                                               FTLCommandCount;
                       p_BTableChangesDelta->BT_Index = old_idx;
                       p_BTableChangesDelta->BT_Entry_Value = pbt[old_idx];
                       p_BTableChangesDelta->ValidFields = 0x0C;

                       p_BTableChangesDelta = (struct BTableChangesDelta *)
                                               g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                               FTLCommandCount;
                       p_BTableChangesDelta->BT_Index = rep_idx;
                       p_BTableChangesDelta->BT_Entry_Value = pbt[rep_idx];
                       p_BTableChangesDelta->ValidFields = 0x0C;
#endif
               } else {
                       pbt[FTL_Get_Block_Index(rep_blk)] |= BAD_BLOCK;
#if CMD_DMA
                       p_BTableChangesDelta = (struct BTableChangesDelta *)
                                               g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                               FTLCommandCount;
                       p_BTableChangesDelta->BT_Index =
                                       FTL_Get_Block_Index(rep_blk);
                       p_BTableChangesDelta->BT_Entry_Value =
                                       pbt[FTL_Get_Block_Index(rep_blk)];
                       p_BTableChangesDelta->ValidFields = 0x0C;
#endif
                       *result = FAIL;
                       ret = FAIL;
               }

               if ((*rep_blk_num++) > WEAR_LEVELING_BLOCK_NUM)
                       ret = FAIL;
       } else {
               printk(KERN_ERR "Less than 3 spare blocks exist so quit\n");
               ret = FAIL;
       }

       return ret;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Static_Wear_Leveling
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  This is static wear leveling (done by explicit call)
*               search for most&least used
*               if difference < GATE:
*                   update the block table with exhange
*                   mark block table in flash as IN_PROGRESS
*                   copy flash block
*               the caller should handle GC clean up after calling this function
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Static_Wear_Leveling(void)
{
       u8 most_worn_cnt;
       u8 least_worn_cnt;
       u32 most_worn_idx;
       u32 least_worn_idx;
       int result = PASS;
       int go_on = PASS;
       u32 replaced_blks = 0;
       u8 *chang_flag;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       chang_flag = g_pMemPoolFree;
       g_pMemPoolFree += (DeviceInfo.wDataBlockNum);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
                            globalMemSize, 0);

       if (!chang_flag)
               return FAIL;

       memset(chang_flag, FAIL, DeviceInfo.wDataBlockNum);
       while (go_on == PASS) {
               nand_dbg_print(NAND_DBG_DEBUG,
                       "starting static wear leveling\n");
               most_worn_cnt = 0;
               least_worn_cnt = 0xFF;
               least_worn_idx = BLOCK_TABLE_INDEX;
               most_worn_idx = BLOCK_TABLE_INDEX;

               find_least_most_worn(chang_flag, &least_worn_idx,
                       &least_worn_cnt, &most_worn_idx, &most_worn_cnt);

               nand_dbg_print(NAND_DBG_DEBUG,
                       "Used and least worn is block %u, whos count is %u\n",
                       (unsigned int)least_worn_idx,
                       (unsigned int)least_worn_cnt);

               nand_dbg_print(NAND_DBG_DEBUG,
                       "Free and  most worn is block %u, whos count is %u\n",
                       (unsigned int)most_worn_idx,
                       (unsigned int)most_worn_cnt);

               if ((most_worn_cnt > least_worn_cnt) &&
                       (most_worn_cnt - least_worn_cnt > WEAR_LEVELING_GATE))
                       go_on = move_blks_for_wear_leveling(chang_flag,
                               &least_worn_idx, &replaced_blks, &result);
       }

       g_pMemPoolFree -= (DeviceInfo.wDataBlockNum);
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       return result;
}

#if CMD_DMA
static int do_garbage_collection(u32 discard_cnt)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 pba;
       u8 bt_block_erased = 0;
       int i, cnt, ret = FAIL;
       u64 addr;

       i = 0;
       while ((i < DeviceInfo.wDataBlockNum) && (discard_cnt > 0) &&
                       ((FTLCommandCount + 28) < 256)) {
               if (((pbt[i] & BAD_BLOCK) != BAD_BLOCK) &&
                               (pbt[i] & DISCARD_BLOCK)) {
                       if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
                               g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                               FTL_Write_IN_Progress_Block_Table_Page();
                       }

                       addr = FTL_Get_Physical_Block_Addr((u64)i *
                                               DeviceInfo.wBlockDataSize);
                       pba = BLK_FROM_ADDR(addr);

                       for (cnt = FIRST_BT_ID; cnt <= LAST_BT_ID; cnt++) {
                               if (pba == g_pBTBlocks[cnt - FIRST_BT_ID]) {
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "GC will erase BT block %u\n",
                                               (unsigned int)pba);
                                       discard_cnt--;
                                       i++;
                                       bt_block_erased = 1;
                                       break;
                               }
                       }

                       if (bt_block_erased) {
                               bt_block_erased = 0;
                               continue;
                       }

                       addr = FTL_Get_Physical_Block_Addr((u64)i *
                                               DeviceInfo.wBlockDataSize);

                       if (PASS == GLOB_FTL_Block_Erase(addr)) {
                               pbt[i] &= (u32)(~DISCARD_BLOCK);
                               pbt[i] |= (u32)(SPARE_BLOCK);
                               p_BTableChangesDelta =
                                       (struct BTableChangesDelta *)
                                       g_pBTDelta_Free;
                               g_pBTDelta_Free +=
                                       sizeof(struct BTableChangesDelta);
                               p_BTableChangesDelta->FTLCommandCount =
                                       FTLCommandCount - 1;
                               p_BTableChangesDelta->BT_Index = i;
                               p_BTableChangesDelta->BT_Entry_Value = pbt[i];
                               p_BTableChangesDelta->ValidFields = 0x0C;
                               discard_cnt--;
                               ret = PASS;
                       } else {
                               MARK_BLOCK_AS_BAD(pbt[i]);
                       }
               }

               i++;
       }

       return ret;
}

#else
static int do_garbage_collection(u32 discard_cnt)
{
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 pba;
       u8 bt_block_erased = 0;
       int i, cnt, ret = FAIL;
       u64 addr;

       i = 0;
       while ((i < DeviceInfo.wDataBlockNum) && (discard_cnt > 0)) {
               if (((pbt[i] & BAD_BLOCK) != BAD_BLOCK) &&
                               (pbt[i] & DISCARD_BLOCK)) {
                       if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
                               g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                               FTL_Write_IN_Progress_Block_Table_Page();
                       }

                       addr = FTL_Get_Physical_Block_Addr((u64)i *
                                               DeviceInfo.wBlockDataSize);
                       pba = BLK_FROM_ADDR(addr);

                       for (cnt = FIRST_BT_ID; cnt <= LAST_BT_ID; cnt++) {
                               if (pba == g_pBTBlocks[cnt - FIRST_BT_ID]) {
                                       nand_dbg_print(NAND_DBG_DEBUG,
                                               "GC will erase BT block %d\n",
                                               pba);
                                       discard_cnt--;
                                       i++;
                                       bt_block_erased = 1;
                                       break;
                               }
                       }

                       if (bt_block_erased) {
                               bt_block_erased = 0;
                               continue;
                       }

                       addr = FTL_Get_Physical_Block_Addr((u64)i *
                                               DeviceInfo.wBlockDataSize);

                       if (PASS == GLOB_FTL_Block_Erase(addr)) {
                               pbt[i] &= (u32)(~DISCARD_BLOCK);
                               pbt[i] |= (u32)(SPARE_BLOCK);
                               discard_cnt--;
                               ret = PASS;
                       } else {
                               MARK_BLOCK_AS_BAD(pbt[i]);
                       }
               }

               i++;
       }

       return ret;
}
#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Garbage_Collection
* Inputs:       none
* Outputs:      PASS / FAIL (returns the number of un-erased blocks
* Description:  search the block table for all discarded blocks to erase
*               for each discarded block:
*                   set the flash block to IN_PROGRESS
*                   erase the block
*                   update the block table
*                   write the block table to flash
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Garbage_Collection(void)
{
       u32 i;
       u32 wDiscard = 0;
       int wResult = FAIL;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       if (GC_Called) {
               printk(KERN_ALERT "GLOB_FTL_Garbage_Collection() "
                       "has been re-entered! Exit.\n");
               return PASS;
       }

       GC_Called = 1;

       GLOB_FTL_BT_Garbage_Collection();

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               if (IS_DISCARDED_BLOCK(i))
                       wDiscard++;
       }

       if (wDiscard <= 0) {
               GC_Called = 0;
               return wResult;
       }

       nand_dbg_print(NAND_DBG_DEBUG,
               "Found %d discarded blocks\n", wDiscard);

       FTL_Write_Block_Table(FAIL);

       wResult = do_garbage_collection(wDiscard);

       FTL_Write_Block_Table(FAIL);

       GC_Called = 0;

       return wResult;
}


#if CMD_DMA
static int do_bt_garbage_collection(void)
{
       u32 pba, lba;
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 *pBTBlocksNode = (u32 *)g_pBTBlocks;
       u64 addr;
       int i, ret = FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       if (BT_GC_Called)
               return PASS;

       BT_GC_Called = 1;

       for (i = last_erased; (i <= LAST_BT_ID) &&
               (g_pBTBlocks[((i + 2) % (1 + LAST_BT_ID - FIRST_BT_ID)) +
               FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL) &&
               ((FTLCommandCount + 28)) < 256; i++) {
               pba = pBTBlocksNode[i - FIRST_BT_ID];
               lba = FTL_Get_Block_Index(pba);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "do_bt_garbage_collection: pba %d, lba %d\n",
                       pba, lba);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Block Table Entry: %d", pbt[lba]);

               if (((pbt[lba] & BAD_BLOCK) != BAD_BLOCK) &&
                       (pbt[lba] & DISCARD_BLOCK)) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "do_bt_garbage_collection_cdma: "
                               "Erasing Block tables present in block %d\n",
                               pba);
                       addr = FTL_Get_Physical_Block_Addr((u64)lba *
                                               DeviceInfo.wBlockDataSize);
                       if (PASS == GLOB_FTL_Block_Erase(addr)) {
                               pbt[lba] &= (u32)(~DISCARD_BLOCK);
                               pbt[lba] |= (u32)(SPARE_BLOCK);

                               p_BTableChangesDelta =
                                       (struct BTableChangesDelta *)
                                       g_pBTDelta_Free;
                               g_pBTDelta_Free +=
                                       sizeof(struct BTableChangesDelta);

                               p_BTableChangesDelta->FTLCommandCount =
                                       FTLCommandCount - 1;
                               p_BTableChangesDelta->BT_Index = lba;
                               p_BTableChangesDelta->BT_Entry_Value =
                                                               pbt[lba];

                               p_BTableChangesDelta->ValidFields = 0x0C;

                               ret = PASS;
                               pBTBlocksNode[last_erased - FIRST_BT_ID] =
                                                       BTBLOCK_INVAL;
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "resetting bt entry at index %d "
                                       "value %d\n", i,
                                       pBTBlocksNode[i - FIRST_BT_ID]);
                               if (last_erased == LAST_BT_ID)
                                       last_erased = FIRST_BT_ID;
                               else
                                       last_erased++;
                       } else {
                               MARK_BLOCK_AS_BAD(pbt[lba]);
                       }
               }
       }

       BT_GC_Called = 0;

       return ret;
}

#else
static int do_bt_garbage_collection(void)
{
       u32 pba, lba;
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 *pBTBlocksNode = (u32 *)g_pBTBlocks;
       u64 addr;
       int i, ret = FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       if (BT_GC_Called)
               return PASS;

       BT_GC_Called = 1;

       for (i = last_erased; (i <= LAST_BT_ID) &&
               (g_pBTBlocks[((i + 2) % (1 + LAST_BT_ID - FIRST_BT_ID)) +
               FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL); i++) {
               pba = pBTBlocksNode[i - FIRST_BT_ID];
               lba = FTL_Get_Block_Index(pba);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "do_bt_garbage_collection_cdma: pba %d, lba %d\n",
                       pba, lba);
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Block Table Entry: %d", pbt[lba]);

               if (((pbt[lba] & BAD_BLOCK) != BAD_BLOCK) &&
                       (pbt[lba] & DISCARD_BLOCK)) {
                       nand_dbg_print(NAND_DBG_DEBUG,
                               "do_bt_garbage_collection: "
                               "Erasing Block tables present in block %d\n",
                               pba);
                       addr = FTL_Get_Physical_Block_Addr((u64)lba *
                                               DeviceInfo.wBlockDataSize);
                       if (PASS == GLOB_FTL_Block_Erase(addr)) {
                               pbt[lba] &= (u32)(~DISCARD_BLOCK);
                               pbt[lba] |= (u32)(SPARE_BLOCK);
                               ret = PASS;
                               pBTBlocksNode[last_erased - FIRST_BT_ID] =
                                                       BTBLOCK_INVAL;
                               nand_dbg_print(NAND_DBG_DEBUG,
                                       "resetting bt entry at index %d "
                                       "value %d\n", i,
                                       pBTBlocksNode[i - FIRST_BT_ID]);
                               if (last_erased == LAST_BT_ID)
                                       last_erased = FIRST_BT_ID;
                               else
                                       last_erased++;
                       } else {
                               MARK_BLOCK_AS_BAD(pbt[lba]);
                       }
               }
       }

       BT_GC_Called = 0;

       return ret;
}

#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_BT_Garbage_Collection
* Inputs:       none
* Outputs:      PASS / FAIL (returns the number of un-erased blocks
* Description:  Erases discarded blocks containing Block table
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_BT_Garbage_Collection(void)
{
       return do_bt_garbage_collection();
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_OneBlock
* Inputs:       Block number 1
*               Block number 2
* Outputs:      Replaced Block Number
* Description:  Interchange block table entries at wBlockNum and wReplaceNum
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_OneBlock(u32 blk, u32 rep_blk)
{
       u32 tmp_blk;
       u32 replace_node = BAD_BLOCK;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       if (rep_blk != BAD_BLOCK) {
               if (IS_BAD_BLOCK(blk))
                       tmp_blk = (u32)(pbt[blk]);
               else
                       tmp_blk = (u32)(DISCARD_BLOCK |
                                       (~SPARE_BLOCK & pbt[blk]));
               replace_node = (u32) ((~SPARE_BLOCK) & pbt[rep_blk]);
               pbt[blk] = replace_node;
               pbt[rep_blk] = tmp_blk;

#if CMD_DMA
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

               p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
               p_BTableChangesDelta->BT_Index = blk;
               p_BTableChangesDelta->BT_Entry_Value = pbt[blk];

               p_BTableChangesDelta->ValidFields = 0x0C;

               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

               p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
               p_BTableChangesDelta->BT_Index = rep_blk;
               p_BTableChangesDelta->BT_Entry_Value = pbt[rep_blk];
               p_BTableChangesDelta->ValidFields = 0x0C;
#endif
       }

       return replace_node;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_Block_Table_Data
* Inputs:       Block table size in pages
* Outputs:      PASS=0 / FAIL=1
* Description:  Write block table data in flash
*               If first page and last page
*                  Write data+BT flag
*               else
*                  Write data
*               BT flag is a counter. Its value is incremented for block table
*               write in a new Block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Write_Block_Table_Data(void)
{
       u64 dwBlockTableAddr, pTempAddr;
       u32 Block;
       u16 Page, PageCount;
       u8 *tempBuf;
       int wBytesCopied;
       u16 bt_pages;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       dwBlockTableAddr =
               (u64)((u64)g_wBlockTableIndex * DeviceInfo.wBlockDataSize +
               (u64)g_wBlockTableOffset * DeviceInfo.wPageDataSize);
       pTempAddr = dwBlockTableAddr;

       bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

       nand_dbg_print(NAND_DBG_DEBUG, "FTL_Write_Block_Table_Data: "
                              "page= %d BlockTableIndex= %d "
                              "BlockTableOffset=%d\n", bt_pages,
                              g_wBlockTableIndex, g_wBlockTableOffset);

       Block = BLK_FROM_ADDR(pTempAddr);
       Page = PAGE_FROM_ADDR(pTempAddr, Block);
       PageCount = 1;

       if (bt_block_changed) {
               if (bt_flag == LAST_BT_ID) {
                       bt_flag = FIRST_BT_ID;
                       g_pBTBlocks[bt_flag - FIRST_BT_ID] = Block;
               } else if (bt_flag < LAST_BT_ID) {
                       bt_flag++;
                       g_pBTBlocks[bt_flag - FIRST_BT_ID] = Block;
               }

               if ((bt_flag > (LAST_BT_ID-4)) &&
                       g_pBTBlocks[FIRST_BT_ID - FIRST_BT_ID] !=
                                               BTBLOCK_INVAL) {
                       bt_block_changed = 0;
                       GLOB_FTL_BT_Garbage_Collection();
               }

               bt_block_changed = 0;
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Block Table Counter is %u Block %u\n",
                       bt_flag, (unsigned int)Block);
       }

       tempBuf = g_pMemPoolFree;
       g_pMemPoolFree += (bt_pages > 3) ?
           (FTL_Get_Block_Table_Flash_Size_Bytes() -
            (DeviceInfo.wPageSize << 1)) : DeviceInfo.wPageSize;
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
                            globalMemSize, 0);

       memset(tempBuf, 0, 3);
       tempBuf[3] = bt_flag;
       wBytesCopied = FTL_Copy_Block_Table_To_Flash(tempBuf + 4,
                       DeviceInfo.wPageDataSize - 4, 0);
       memset(&tempBuf[wBytesCopied + 4], 0xff,
               DeviceInfo.wPageSize - (wBytesCopied + 4));
       FTL_Insert_Block_Table_Signature(&tempBuf[DeviceInfo.wPageDataSize],
                                       bt_flag);

#if CMD_DMA
       memcpy(g_pNextBlockTable, tempBuf,
               DeviceInfo.wPageSize * sizeof(u8));
       nand_dbg_print(NAND_DBG_DEBUG, "Writing First Page of Block Table "
               "Block %u Page %u\n", (unsigned int)Block, Page);
       if (FAIL == GLOB_LLD_Write_Page_Main_Spare(g_pNextBlockTable,
               Block, Page, 1, FTLCommandCount,
               LLD_CMD_FLAG_MODE_CDMA | LLD_CMD_FLAG_ORDER_BEFORE_REST)) {
               nand_dbg_print(NAND_DBG_WARN, "NAND Program fail in "
                       "%s, Line %d, Function: %s, "
                       "new Bad Block %d generated!\n",
                       __FILE__, __LINE__, __func__, Block);
               goto func_return;
       }

       FTLCommandCount++;
       g_pNextBlockTable += ((DeviceInfo.wPageSize * sizeof(u8)));
#else
       if (FAIL == GLOB_LLD_Write_Page_Main_Spare(tempBuf, Block, Page, 1)) {
               nand_dbg_print(NAND_DBG_WARN,
                       "NAND Program fail in %s, Line %d, Function: %s, "
                       "new Bad Block %d generated!\n",
                       __FILE__, __LINE__, __func__, Block);
               goto func_return;
       }
#endif

       if (bt_pages > 1) {
               PageCount = bt_pages - 1;
               if (PageCount > 1) {
                       wBytesCopied += FTL_Copy_Block_Table_To_Flash(tempBuf,
                               DeviceInfo.wPageDataSize * (PageCount - 1),
                               wBytesCopied);

#if CMD_DMA
                       memcpy(g_pNextBlockTable, tempBuf,
                               (PageCount - 1) * DeviceInfo.wPageDataSize);
                       if (FAIL == GLOB_LLD_Write_Page_Main(
                               g_pNextBlockTable, Block, Page + 1,
                               PageCount - 1, FTLCommandCount)) {
                               nand_dbg_print(NAND_DBG_WARN,
                                       "NAND Program fail in %s, Line %d, "
                                       "Function: %s, "
                                       "new Bad Block %d generated!\n",
                                       __FILE__, __LINE__, __func__,
                                       (int)Block);
                               goto func_return;
                       }

                       FTLCommandCount++;
                       g_pNextBlockTable += (PageCount - 1) *
                               DeviceInfo.wPageDataSize * sizeof(u8);
#else
                       if (FAIL == GLOB_LLD_Write_Page_Main(tempBuf,
                                       Block, Page + 1, PageCount - 1)) {
                               nand_dbg_print(NAND_DBG_WARN,
                                       "NAND Program fail in %s, Line %d, "
                                       "Function: %s, "
                                       "new Bad Block %d generated!\n",
                                       __FILE__, __LINE__, __func__,
                                       (int)Block);
                               goto func_return;
                       }
#endif
               }

               wBytesCopied = FTL_Copy_Block_Table_To_Flash(tempBuf,
                               DeviceInfo.wPageDataSize, wBytesCopied);
               memset(&tempBuf[wBytesCopied], 0xff,
                       DeviceInfo.wPageSize-wBytesCopied);
               FTL_Insert_Block_Table_Signature(
                       &tempBuf[DeviceInfo.wPageDataSize], bt_flag);
#if CMD_DMA
               memcpy(g_pNextBlockTable, tempBuf,
                               DeviceInfo.wPageSize * sizeof(u8));
               nand_dbg_print(NAND_DBG_DEBUG,
                       "Writing the last Page of Block Table "
                       "Block %u Page %u\n",
                       (unsigned int)Block, Page + bt_pages - 1);
               if (FAIL == GLOB_LLD_Write_Page_Main_Spare(g_pNextBlockTable,
                       Block, Page + bt_pages - 1, 1, FTLCommandCount,
                       LLD_CMD_FLAG_MODE_CDMA |
                       LLD_CMD_FLAG_ORDER_BEFORE_REST)) {
                       nand_dbg_print(NAND_DBG_WARN,
                               "NAND Program fail in %s, Line %d, "
                               "Function: %s, new Bad Block %d generated!\n",
                               __FILE__, __LINE__, __func__, Block);
                       goto func_return;
               }
               FTLCommandCount++;
#else
               if (FAIL == GLOB_LLD_Write_Page_Main_Spare(tempBuf,
                                       Block, Page+bt_pages - 1, 1)) {
                       nand_dbg_print(NAND_DBG_WARN,
                               "NAND Program fail in %s, Line %d, "
                               "Function: %s, "
                               "new Bad Block %d generated!\n",
                               __FILE__, __LINE__, __func__, Block);
                       goto func_return;
               }
#endif
       }

       nand_dbg_print(NAND_DBG_DEBUG, "FTL_Write_Block_Table_Data: done\n");

func_return:
       g_pMemPoolFree -= (bt_pages > 3) ?
               (FTL_Get_Block_Table_Flash_Size_Bytes() -
               (DeviceInfo.wPageSize << 1)) : DeviceInfo.wPageSize;
       ALIGN_DWORD_BWD(g_pMemPoolFree);

       return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_Block_Table
* Inputs:       None
* Outputs:      PASS=0 / FAIL=1
* Description:  Get a new block to write block table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_Block_Table(void)
{
       u32 blk;
       int gc;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       blk = FTL_Replace_LWBlock(BLOCK_TABLE_INDEX, &gc);

       if ((BAD_BLOCK == blk) && (PASS == gc)) {
               GLOB_FTL_Garbage_Collection();
               blk = FTL_Replace_LWBlock(BLOCK_TABLE_INDEX, &gc);
       }
       if (BAD_BLOCK == blk)
               printk(KERN_ERR "%s, %s: There is no spare block. "
                       "It should never happen\n",
                       __FILE__, __func__);

       nand_dbg_print(NAND_DBG_DEBUG, "New Block table Block is %d\n", blk);

       return blk;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_LWBlock
* Inputs:       Block number
*               Pointer to Garbage Collect flag
* Outputs:
* Description:  Determine the least weared block by traversing
*               block table
*               Set Garbage collection to be called if number of spare
*               block is less than Free Block Gate count
*               Change Block table entry to map least worn block for current
*               operation
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_LWBlock(u32 wBlockNum, int *pGarbageCollect)
{
       u32 i;
       u32 *pbt = (u32 *)g_pBlockTable;
       u8 wLeastWornCounter = 0xFF;
       u32 wLeastWornIndex = BAD_BLOCK;
       u32 wSpareBlockNum = 0;
       u32 wDiscardBlockNum = 0;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       if (IS_SPARE_BLOCK(wBlockNum)) {
               *pGarbageCollect = FAIL;
               pbt[wBlockNum] = (u32)(pbt[wBlockNum] & (~SPARE_BLOCK));
#if CMD_DMA
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
               p_BTableChangesDelta->FTLCommandCount =
                                               FTLCommandCount;
               p_BTableChangesDelta->BT_Index = (u32)(wBlockNum);
               p_BTableChangesDelta->BT_Entry_Value = pbt[wBlockNum];
               p_BTableChangesDelta->ValidFields = 0x0C;
#endif
               return pbt[wBlockNum];
       }

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               if (IS_DISCARDED_BLOCK(i))
                       wDiscardBlockNum++;

               if (IS_SPARE_BLOCK(i)) {
                       u32 wPhysicalIndex = (u32)((~BAD_BLOCK) & pbt[i]);
                       if (wPhysicalIndex > DeviceInfo.wSpectraEndBlock)
                               printk(KERN_ERR "FTL_Replace_LWBlock: "
                                       "This should never occur!\n");
                       if (g_pWearCounter[wPhysicalIndex -
                               DeviceInfo.wSpectraStartBlock] <
                               wLeastWornCounter) {
                               wLeastWornCounter =
                                       g_pWearCounter[wPhysicalIndex -
                                       DeviceInfo.wSpectraStartBlock];
                               wLeastWornIndex = i;
                       }
                       wSpareBlockNum++;
               }
       }

       nand_dbg_print(NAND_DBG_WARN,
               "FTL_Replace_LWBlock: Least Worn Counter %d\n",
               (int)wLeastWornCounter);

       if ((wDiscardBlockNum >= NUM_FREE_BLOCKS_GATE) ||
               (wSpareBlockNum <= NUM_FREE_BLOCKS_GATE))
               *pGarbageCollect = PASS;
       else
               *pGarbageCollect = FAIL;

       nand_dbg_print(NAND_DBG_DEBUG,
               "FTL_Replace_LWBlock: Discarded Blocks %u Spare"
               " Blocks %u\n",
               (unsigned int)wDiscardBlockNum,
               (unsigned int)wSpareBlockNum);

       return FTL_Replace_OneBlock(wBlockNum, wLeastWornIndex);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_MWBlock
* Inputs:       None
* Outputs:      most worn spare block no./BAD_BLOCK
* Description:  It finds most worn spare block.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_MWBlock(void)
{
       u32 i;
       u32 *pbt = (u32 *)g_pBlockTable;
       u8 wMostWornCounter = 0;
       u32 wMostWornIndex = BAD_BLOCK;
       u32 wSpareBlockNum = 0;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               if (IS_SPARE_BLOCK(i)) {
                       u32 wPhysicalIndex = (u32)((~SPARE_BLOCK) & pbt[i]);
                       if (g_pWearCounter[wPhysicalIndex -
                           DeviceInfo.wSpectraStartBlock] >
                           wMostWornCounter) {
                               wMostWornCounter =
                                   g_pWearCounter[wPhysicalIndex -
                                   DeviceInfo.wSpectraStartBlock];
                               wMostWornIndex = wPhysicalIndex;
                       }
                       wSpareBlockNum++;
               }
       }

       if (wSpareBlockNum <= 2)
               return BAD_BLOCK;

       return wMostWornIndex;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_Block
* Inputs:       Block Address
* Outputs:      PASS=0 / FAIL=1
* Description:  If block specified by blk_addr parameter is not free,
*               replace it with the least worn block.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Replace_Block(u64 blk_addr)
{
       u32 current_blk = BLK_FROM_ADDR(blk_addr);
       u32 *pbt = (u32 *)g_pBlockTable;
       int wResult = PASS;
       int GarbageCollect = FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       if (IS_SPARE_BLOCK(current_blk)) {
               pbt[current_blk] = (~SPARE_BLOCK) & pbt[current_blk];
#if CMD_DMA
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
               p_BTableChangesDelta->FTLCommandCount =
                       FTLCommandCount;
               p_BTableChangesDelta->BT_Index = current_blk;
               p_BTableChangesDelta->BT_Entry_Value = pbt[current_blk];
               p_BTableChangesDelta->ValidFields = 0x0C ;
#endif
               return wResult;
       }

       FTL_Replace_LWBlock(current_blk, &GarbageCollect);

       if (PASS == GarbageCollect)
               wResult = GLOB_FTL_Garbage_Collection();

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Is_BadBlock
* Inputs:       block number to test
* Outputs:      PASS (block is BAD) / FAIL (block is not bad)
* Description:  test if this block number is flagged as bad
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Is_BadBlock(u32 wBlockNum)
{
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       if (wBlockNum >= DeviceInfo.wSpectraStartBlock
               && BAD_BLOCK == (pbt[wBlockNum] & BAD_BLOCK))
               return PASS;
       else
               return FAIL;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Flush_Cache
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  flush all the cache blocks to flash
*               if a cache block is not dirty, don't do anything with it
*               else, write the block and update the block table
* Note:         This function should be called at shutdown/power down.
*               to write important data into device
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flush_Cache(void)
{
       int i;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       for (i = 0; i < CACHE_BLOCK_NUMBER; i++) {
               if (SET == Cache.ItemArray[i].bChanged) {
                       if (FTL_Cache_Write_Back(Cache.ItemArray[i].pContent,
                                       Cache.ItemArray[i].dwAddress) != ERR)
                               Cache.ItemArray[i].bChanged = CLEAR;
                       else
                               return ERR;
               }
       }

       return FTL_Write_Block_Table(FAIL);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Page_Read
* Inputs:       pointer to data
*               address of data (u64 is LBA * Bytes/Page)
* Outputs:      PASS=0 / FAIL=1
* Description:  reads a page of data into RAM from the cache
*               if the data is not already in cache, read from flash to cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Page_Read(u8 *pData, u64 dwPageAddr)
{
       u8 cache_blk;
       int wResult = PASS;

       nand_dbg_print(NAND_DBG_DEBUG, "GLOB_FTL_Page_Read - "
               "dwPageAddr: %llu\n", dwPageAddr);

#if CMD_DMA
    g_SBDCmdIndex++;
#endif

       cache_blk = FTL_Cache_If_Hit(dwPageAddr);

       if (UNHIT_BLOCK == cache_blk) {
               nand_dbg_print(NAND_DBG_DEBUG,
                              "GLOB_FTL_Page_Read: Cache not hit\n");
               wResult = FTL_Cache_Write();
               if (ERR == FTL_Cache_Read(dwPageAddr))
                       wResult = ERR;
               cache_blk = Cache.bLRU;
       }

       FTL_Cache_Read_Page(pData, dwPageAddr, cache_blk);

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Page_Write
* Inputs:       pointer to data
*               address of data (ADDRESSTYPE is LBA * Bytes/Page)
* Outputs:      PASS=0 / FAIL=1
* Description:  writes a page of data from RAM to the cache
*               if the data is not already in cache, write back the
*               least recently used block and read the addressed block
*               from flash to cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Page_Write(u8 *pData, u64 dwPageAddr)
{
       u8 cache_blk;
       int wResult = PASS;
       u32 *pbt = (u32 *)g_pBlockTable;

       nand_dbg_print(NAND_DBG_DEBUG, "GLOB_FTL_Page_Write - "
               "dwPageAddr: %llu\n", dwPageAddr);

#if CMD_DMA
    g_SBDCmdIndex++;
#endif

       cache_blk = FTL_Cache_If_Hit(dwPageAddr);

       if (UNHIT_BLOCK == cache_blk) {
               wResult = FTL_Cache_Write();
               if (IS_BAD_BLOCK(BLK_FROM_ADDR(dwPageAddr))) {
                       if (FAIL == FTL_Replace_Block(dwPageAddr))
                               return FAIL;
               }
               if (ERR == FTL_Cache_Read(dwPageAddr))
                       wResult = ERR;
               cache_blk = Cache.bLRU;
               FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk, 0);
       } else {
#if CMD_DMA
               FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk,
                               LLD_CMD_FLAG_ORDER_BEFORE_REST);
#else
               FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk, 0);
#endif
       }

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Block_Erase
* Inputs:       address of block to erase (now in byte format, should change to
* block format)
* Outputs:      PASS=0 / FAIL=1
* Description:  erases the specified block
*               increments the erase count
*               If erase count reaches its upper limit,call function to
*               do the ajustment as per the relative erase count values
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Block_Erase(u64 blk_addr)
{
       int status;
       u32 BlkIdx;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       BlkIdx = (u32)(blk_addr >> DeviceInfo.nBitsInBlockDataSize);

       if (BlkIdx < DeviceInfo.wSpectraStartBlock) {
               printk(KERN_ERR "GLOB_FTL_Block_Erase: "
                       "This should never occur\n");
               return FAIL;
       }

#if CMD_DMA
       status = GLOB_LLD_Erase_Block(BlkIdx,
               FTLCommandCount, LLD_CMD_FLAG_MODE_CDMA);
       if (status == FAIL)
               nand_dbg_print(NAND_DBG_WARN,
                              "NAND Program fail in %s, Line %d, "
                              "Function: %s, new Bad Block %d generated!\n",
                              __FILE__, __LINE__, __func__, BlkIdx);
#else
       status = GLOB_LLD_Erase_Block(BlkIdx);
       if (status == FAIL) {
               nand_dbg_print(NAND_DBG_WARN,
                              "NAND Program fail in %s, Line %d, "
                              "Function: %s, new Bad Block %d generated!\n",
                              __FILE__, __LINE__, __func__, BlkIdx);
               return status;
       }
#endif

       if (DeviceInfo.MLCDevice) {
               g_pReadCounter[BlkIdx - DeviceInfo.wSpectraStartBlock] = 0;
               if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
                       g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                       FTL_Write_IN_Progress_Block_Table_Page();
               }
       }

       g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock]++;

#if CMD_DMA
       p_BTableChangesDelta =
               (struct BTableChangesDelta *)g_pBTDelta_Free;
       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
       p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
       p_BTableChangesDelta->WC_Index =
               BlkIdx - DeviceInfo.wSpectraStartBlock;
       p_BTableChangesDelta->WC_Entry_Value =
               g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock];
       p_BTableChangesDelta->ValidFields = 0x30;

       if (DeviceInfo.MLCDevice) {
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
               p_BTableChangesDelta->FTLCommandCount =
                       FTLCommandCount;
               p_BTableChangesDelta->RC_Index =
                       BlkIdx - DeviceInfo.wSpectraStartBlock;
               p_BTableChangesDelta->RC_Entry_Value =
                       g_pReadCounter[BlkIdx -
                               DeviceInfo.wSpectraStartBlock];
               p_BTableChangesDelta->ValidFields = 0xC0;
       }

       FTLCommandCount++;
#endif

       if (g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock] == 0xFE)
               FTL_Adjust_Relative_Erase_Count(BlkIdx);

       return status;
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Adjust_Relative_Erase_Count
* Inputs:       index to block that was just incremented and is at the max
* Outputs:      PASS=0 / FAIL=1
* Description:  If any erase counts at MAX, adjusts erase count of every
*               block by substracting least worn
*               counter from counter value of every entry in wear table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Adjust_Relative_Erase_Count(u32 Index_of_MAX)
{
       u8 wLeastWornCounter = MAX_BYTE_VALUE;
       u8 wWearCounter;
       u32 i, wWearIndex;
       u32 *pbt = (u32 *)g_pBlockTable;
       int wResult = PASS;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
               __FILE__, __LINE__, __func__);

       for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
               if (IS_BAD_BLOCK(i))
                       continue;
               wWearIndex = (u32)(pbt[i] & (~BAD_BLOCK));

               if ((wWearIndex - DeviceInfo.wSpectraStartBlock) < 0)
                       printk(KERN_ERR "FTL_Adjust_Relative_Erase_Count:"
                                       "This should never occur\n");
               wWearCounter = g_pWearCounter[wWearIndex -
                       DeviceInfo.wSpectraStartBlock];
               if (wWearCounter < wLeastWornCounter)
                       wLeastWornCounter = wWearCounter;
       }

       if (wLeastWornCounter == 0) {
               nand_dbg_print(NAND_DBG_WARN,
                       "Adjusting Wear Levelling Counters: Special Case\n");
               g_pWearCounter[Index_of_MAX -
                       DeviceInfo.wSpectraStartBlock]--;
#if CMD_DMA
               p_BTableChangesDelta =
                       (struct BTableChangesDelta *)g_pBTDelta_Free;
               g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
               p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
               p_BTableChangesDelta->WC_Index =
                       Index_of_MAX - DeviceInfo.wSpectraStartBlock;
               p_BTableChangesDelta->WC_Entry_Value =
                       g_pWearCounter[Index_of_MAX -
                               DeviceInfo.wSpectraStartBlock];
               p_BTableChangesDelta->ValidFields = 0x30;
#endif
               FTL_Static_Wear_Leveling();
       } else {
               for (i = 0; i < DeviceInfo.wDataBlockNum; i++)
                       if (!IS_BAD_BLOCK(i)) {
                               wWearIndex = (u32)(pbt[i] & (~BAD_BLOCK));
                               g_pWearCounter[wWearIndex -
                                       DeviceInfo.wSpectraStartBlock] =
                                       (u8)(g_pWearCounter
                                       [wWearIndex -
                                       DeviceInfo.wSpectraStartBlock] -
                                       wLeastWornCounter);
#if CMD_DMA
                               p_BTableChangesDelta =
                               (struct BTableChangesDelta *)g_pBTDelta_Free;
                               g_pBTDelta_Free +=
                                       sizeof(struct BTableChangesDelta);

                               p_BTableChangesDelta->FTLCommandCount =
                                       FTLCommandCount;
                               p_BTableChangesDelta->WC_Index = wWearIndex -
                                       DeviceInfo.wSpectraStartBlock;
                               p_BTableChangesDelta->WC_Entry_Value =
                                       g_pWearCounter[wWearIndex -
                                       DeviceInfo.wSpectraStartBlock];
                               p_BTableChangesDelta->ValidFields = 0x30;
#endif
                       }
       }

       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_IN_Progress_Block_Table_Page
* Inputs:       None
* Outputs:      None
* Description:  It writes in-progress flag page to the page next to
*               block table
***********************************************************************/
static int FTL_Write_IN_Progress_Block_Table_Page(void)
{
       int wResult = PASS;
       u16 bt_pages;
       u16 dwIPFPageAddr;
#if CMD_DMA
#else
       u32 *pbt = (u32 *)g_pBlockTable;
       u32 wTempBlockTableIndex;
#endif

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

       bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

       dwIPFPageAddr = g_wBlockTableOffset + bt_pages;

       nand_dbg_print(NAND_DBG_DEBUG, "Writing IPF at "
                              "Block %d Page %d\n",
                              g_wBlockTableIndex, dwIPFPageAddr);

#if CMD_DMA
       wResult = GLOB_LLD_Write_Page_Main_Spare(g_pIPF,
               g_wBlockTableIndex, dwIPFPageAddr, 1, FTLCommandCount,
               LLD_CMD_FLAG_MODE_CDMA | LLD_CMD_FLAG_ORDER_BEFORE_REST);

       if (wResult == FAIL) {
               nand_dbg_print(NAND_DBG_WARN,
                              "NAND Program fail in %s, Line %d, "
                              "Function: %s, new Bad Block %d generated!\n",
                              __FILE__, __LINE__, __func__,
                              g_wBlockTableIndex);
       }
       g_wBlockTableOffset = dwIPFPageAddr + 1;
       p_BTableChangesDelta = (struct BTableChangesDelta *)g_pBTDelta_Free;
       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
       p_BTableChangesDelta->FTLCommandCount = FTLCommandCount;
       p_BTableChangesDelta->g_wBlockTableOffset = g_wBlockTableOffset;
       p_BTableChangesDelta->ValidFields = 0x01;
       FTLCommandCount++;
#else
       wResult = GLOB_LLD_Write_Page_Main_Spare(g_pIPF,
               g_wBlockTableIndex, dwIPFPageAddr, 1);
       if (wResult == FAIL) {
               nand_dbg_print(NAND_DBG_WARN,
                              "NAND Program fail in %s, Line %d, "
                              "Function: %s, new Bad Block %d generated!\n",
                              __FILE__, __LINE__, __func__,
                              (int)g_wBlockTableIndex);
               MARK_BLOCK_AS_BAD(pbt[BLOCK_TABLE_INDEX]);
               wTempBlockTableIndex = FTL_Replace_Block_Table();
               bt_block_changed = 1;
               if (BAD_BLOCK == wTempBlockTableIndex)
                       return ERR;
               g_wBlockTableIndex = wTempBlockTableIndex;
               g_wBlockTableOffset = 0;
               pbt[BLOCK_TABLE_INDEX] = g_wBlockTableIndex;
               return FAIL;
       }
       g_wBlockTableOffset = dwIPFPageAddr + 1;
#endif
       return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Read_Disturbance
* Inputs:       block address
* Outputs:      PASS=0 / FAIL=1
* Description:  used to handle read disturbance. Data in block that
*               reaches its read limit is moved to new block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Read_Disturbance(u32 blk_addr)
{
       int wResult = FAIL;
       u32 *pbt = (u32 *) g_pBlockTable;
       u32 dwOldBlockAddr = blk_addr;
       u32 wBlockNum;
       u32 i;
       u32 wLeastReadCounter = 0xFFFF;
       u32 wLeastReadIndex = BAD_BLOCK;
       u32 wSpareBlockNum = 0;
       u32 wTempNode;
       u32 wReplacedNode;
       u8 *g_pTempBuf;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                              __FILE__, __LINE__, __func__);

#if CMD_DMA
       g_pTempBuf = (u8 *)g_pCopyBackBufferStart;
       g_pCopyBackBufferStart += DeviceInfo.wPageDataSize *
               DeviceInfo.wPagesPerBlock * sizeof(u8);
#else
       g_pTempBuf = (u8 *)g_pMemPoolFree;
       g_pMemPoolFree += DeviceInfo.wPageDataSize *
               DeviceInfo.wPagesPerBlock * sizeof(u8);
       ALIGN_DWORD_FWD(g_pMemPoolFree);
       debug_boundary_error(((int)g_pMemPoolFree - (int)g_pMemPool) - 1,
                            globalMemSize, 0);
#endif

       wBlockNum = FTL_Get_Block_Index(blk_addr);

       do {
               /* This is a bug.Here 'i' should be logical block number
                * and start from 1 (0 is reserved for block table).
                * Have fixed it.        - Yunpeng 2008. 12. 19
                */
               for (i = 1; i < DeviceInfo.wDataBlockNum; i++) {
                       if (IS_SPARE_BLOCK(i)) {
                               u32 wPhysicalIndex =
                                       (u32)((~SPARE_BLOCK) & pbt[i]);
                               if (g_pReadCounter[wPhysicalIndex -
                                       DeviceInfo.wSpectraStartBlock] <
                                       wLeastReadCounter) {
                                       wLeastReadCounter =
                                               g_pReadCounter[wPhysicalIndex -
                                               DeviceInfo.wSpectraStartBlock];
                                       wLeastReadIndex = i;
                               }
                               wSpareBlockNum++;
                       }
               }

               if (wSpareBlockNum <= NUM_FREE_BLOCKS_GATE) {
                       wResult = GLOB_FTL_Garbage_Collection();
                       if (PASS == wResult)
                               continue;
                       else
                               break;
               } else {
                       wTempNode = (u32)(DISCARD_BLOCK | pbt[wBlockNum]);
                       wReplacedNode = (u32)((~SPARE_BLOCK) &
                                       pbt[wLeastReadIndex]);
#if CMD_DMA
                       pbt[wBlockNum] = wReplacedNode;
                       pbt[wLeastReadIndex] = wTempNode;
                       p_BTableChangesDelta =
                               (struct BTableChangesDelta *)g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                       FTLCommandCount;
                       p_BTableChangesDelta->BT_Index = wBlockNum;
                       p_BTableChangesDelta->BT_Entry_Value = pbt[wBlockNum];
                       p_BTableChangesDelta->ValidFields = 0x0C;

                       p_BTableChangesDelta =
                               (struct BTableChangesDelta *)g_pBTDelta_Free;
                       g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

                       p_BTableChangesDelta->FTLCommandCount =
                                       FTLCommandCount;
                       p_BTableChangesDelta->BT_Index = wLeastReadIndex;
                       p_BTableChangesDelta->BT_Entry_Value =
                                       pbt[wLeastReadIndex];
                       p_BTableChangesDelta->ValidFields = 0x0C;

                       wResult = GLOB_LLD_Read_Page_Main(g_pTempBuf,
                               dwOldBlockAddr, 0, DeviceInfo.wPagesPerBlock,
                               FTLCommandCount, LLD_CMD_FLAG_MODE_CDMA);
                       if (wResult == FAIL)
                               return wResult;

                       FTLCommandCount++;

                       if (wResult != FAIL) {
                               if (FAIL == GLOB_LLD_Write_Page_Main(
                                       g_pTempBuf, pbt[wBlockNum], 0,
                                       DeviceInfo.wPagesPerBlock,
                                       FTLCommandCount)) {
                                       nand_dbg_print(NAND_DBG_WARN,
                                               "NAND Program fail in "
                                               "%s, Line %d, Function: %s, "
                                               "new Bad Block %d "
                                               "generated!\n",
                                               __FILE__, __LINE__, __func__,
                                               (int)pbt[wBlockNum]);
                                       wResult = FAIL;
                                       MARK_BLOCK_AS_BAD(pbt[wBlockNum]);
                               }
                               FTLCommandCount++;
                       }
#else
                       wResult = GLOB_LLD_Read_Page_Main(g_pTempBuf,
                               dwOldBlockAddr, 0, DeviceInfo.wPagesPerBlock);
                       if (wResult == FAIL) {
                               g_pMemPoolFree -= (DeviceInfo.wPageDataSize *
                                       DeviceInfo.wPagesPerBlock *
                                       sizeof(u8));
                               ALIGN_DWORD_BWD(g_pMemPoolFree);
                               return wResult;
                       }

                       if (wResult != FAIL) {
                               /* This is a bug. At this time, pbt[wBlockNum]
                               is still the physical address of
                               discard block, and should not be write.
                               Have fixed it as below.
                                       -- Yunpeng 2008.12.19
                               */
                               wResult = GLOB_LLD_Write_Page_Main(g_pTempBuf,
                                       wReplacedNode, 0,
                                       DeviceInfo.wPagesPerBlock);
                               if (wResult == FAIL) {
                                       nand_dbg_print(NAND_DBG_WARN,
                                               "NAND Program fail in "
                                               "%s, Line %d, Function: %s, "
                                               "new Bad Block %d "
                                               "generated!\n",
                                               __FILE__, __LINE__, __func__,
                                               (int)wReplacedNode);
                                       MARK_BLOCK_AS_BAD(wReplacedNode);
                               } else {
                                       pbt[wBlockNum] = wReplacedNode;
                                       pbt[wLeastReadIndex] = wTempNode;
                               }
                       }

                       if ((wResult == PASS) && (g_cBlockTableStatus !=
                               IN_PROGRESS_BLOCK_TABLE)) {
                               g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
                               FTL_Write_IN_Progress_Block_Table_Page();
                       }
#endif
               }
       } while (wResult != PASS)
       ;

#if CMD_DMA
       /* ... */
#else
       g_pMemPoolFree -= (DeviceInfo.wPageDataSize *
                       DeviceInfo.wPagesPerBlock * sizeof(u8));
       ALIGN_DWORD_BWD(g_pMemPoolFree);
#endif

       return wResult;
}

