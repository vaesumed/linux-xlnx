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

#include "lld.h"
#include "lld_nand.h"

#include "spectraswconfig.h"
#include "flash.h"
#include "ffsdefs.h"

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#if (FLASH_NAND  || FLASH_CDMA)
#include "NAND_Regs_4.h"

#define SPECTRA_NAND_NAME    "nd"

#define CEIL_DIV(X, Y) (((X)%(Y)) ? ((X)/(Y)+1) : ((X)/(Y)))

#define INT_IDLE_STATE                 0
#define INT_READ_PAGE_MAIN    0x01
#define INT_WRITE_PAGE_MAIN    0x02
#define INT_PIPELINE_READ_AHEAD    0x04
#define INT_PIPELINE_WRITE_AHEAD    0x08
#define INT_MULTI_PLANE_READ    0x10
#define INT_MULTI_PLANE_WRITE    0x11

struct mrst_nand_info {
       struct pci_dev *dev;
       u32 state;
       u32 flash_bank;
       u8 *read_data;
       u8 *write_data;
       u32 block;
       u16 page;
       u32 use_dma;
       void __iomem *ioaddr;  /* Mapped address */
       int ret;
       struct completion complete;
};

static struct mrst_nand_info info;

int totalUsedBanks;
u32 GLOB_valid_banks[LLD_MAX_FLASH_BANKS];

/* Ugly hack to fix code that used an 8k bytes or 512bytes array
 * in the < 4kB Linux kernel stack */
/* static byte page_main_spare[MAX_PAGE_MAINSPARE_AREA]; */
static u8 page_spare[MAX_PAGE_SPARE_AREA];
static u8 pReadSpareBuf[MAX_PAGE_SPARE_AREA];

void __iomem *FlashReg;
void __iomem *FlashMem;

u16 conf_parameters[] = {
       0x0000,
       0x0000,
       0x01F4,
       0x01F4,
       0x01F4,
       0x01F4,
       0x0000,
       0x0000,
       0x0001,
       0x0000,
       0x0000,
       0x0000,
       0x0000,
       0x0040,
       0x0001,
       0x000A,
       0x000A,
       0x000A,
       0x0000,
       0x0000,
       0x0005,
       0x0012,
       0x000C
};

u16   NAND_Get_Bad_Block(u32 block)
{
       u32 status = PASS;
       u32 flag_bytes  = 0;
       u32 skip_bytes  = DeviceInfo.wSpareSkipBytes;
       u32 page, i;

       if (ioread32(FlashReg + ECC_ENABLE))
               flag_bytes = DeviceInfo.wNumPageSpareFlag;

       for (page = 0; page < 2; page++) {
               status = NAND_Read_Page_Spare(pReadSpareBuf, block, page, 1);
               if (status != PASS)
                       return READ_ERROR;
               for (i = flag_bytes; i < (flag_bytes + skip_bytes); i++)
                       if (pReadSpareBuf[i] != 0xff)
                               return DEFECTIVE_BLOCK;
       }

       for (page = 1; page < 3; page++) {
               status = NAND_Read_Page_Spare(pReadSpareBuf, block,
                       DeviceInfo.wPagesPerBlock - page , 1);
               if (status != PASS)
                       return READ_ERROR;
               for (i = flag_bytes; i < (flag_bytes + skip_bytes); i++)
                       if (pReadSpareBuf[i] != 0xff)
                               return DEFECTIVE_BLOCK;
       }

       return GOOD_BLOCK;
}


u16 NAND_Flash_Reset(void)
{
       u32 i;
       u32 intr_status_rst_comp[4] = {INTR_STATUS0__RST_COMP,
               INTR_STATUS1__RST_COMP,
               INTR_STATUS2__RST_COMP,
               INTR_STATUS3__RST_COMP};
       u32 intr_status[4] = {INTR_STATUS0, INTR_STATUS1,
               INTR_STATUS2, INTR_STATUS3};
       u32 device_reset_banks[4] = {DEVICE_RESET__BANK0,
               DEVICE_RESET__BANK1,
               DEVICE_RESET__BANK2,
               DEVICE_RESET__BANK3};

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       for (i = 0 ; i < LLD_MAX_FLASH_BANKS; i++)
               iowrite32(intr_status_rst_comp[i], FlashReg + intr_status[i]);

       for (i = 0 ; i < LLD_MAX_FLASH_BANKS; i++) {
               if (!GLOB_valid_banks[i])
                       break;
               iowrite32(device_reset_banks[i], FlashReg + DEVICE_RESET);
               while (!(ioread32(FlashReg + intr_status[i]) &
                       intr_status_rst_comp[i]))
                       ;
       }

       for (i = 0; i < LLD_MAX_FLASH_BANKS; i++)
               iowrite32(intr_status_rst_comp[i], FlashReg + intr_status[i]);

       return PASS;
}

static void NAND_ONFi_Timing_Mode(u16 mode)
{
       u16 Trea[6] = {40, 30, 25, 20, 20, 16};
       u16 Trp[6] = {50, 25, 17, 15, 12, 10};
       u16 Treh[6] = {30, 15, 15, 10, 10, 7};
       u16 Trc[6] = {100, 50, 35, 30, 25, 20};
       u16 Trhoh[6] = {0, 15, 15, 15, 15, 15};
       u16 Trloh[6] = {0, 0, 0, 0, 5, 5};
       u16 Tcea[6] = {100, 45, 30, 25, 25, 25};
       u16 Tadl[6] = {200, 100, 100, 100, 70, 70};
       u16 Trhw[6] = {200, 100, 100, 100, 100, 100};
       u16 Trhz[6] = {200, 100, 100, 100, 100, 100};
       u16 Twhr[6] = {120, 80, 80, 60, 60, 60};
       u16 Tcs[6] = {70, 35, 25, 25, 20, 15};

       u16 TclsRising = 1;
       u16 data_invalid_rhoh, data_invalid_rloh, data_invalid;
       u16 dv_window = 0;
       u16 en_lo, en_hi;
       u16 acc_clks;
       u16 addr_2_data, re_2_we, re_2_re, we_2_re, cs_cnt;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       en_lo = CEIL_DIV(Trp[mode], CLK_X);
       en_hi = CEIL_DIV(Treh[mode], CLK_X);

#if ONFI_BLOOM_TIME
       if ((en_hi * CLK_X) < (Treh[mode] + 2))
               en_hi++;
#endif

       if ((en_lo + en_hi) * CLK_X < Trc[mode])
               en_lo += CEIL_DIV((Trc[mode] - (en_lo + en_hi) * CLK_X), CLK_X);

       if ((en_lo + en_hi) < CLK_MULTI)
               en_lo += CLK_MULTI - en_lo - en_hi;

       while (dv_window < 8) {
               data_invalid_rhoh = en_lo * CLK_X + Trhoh[mode];

               data_invalid_rloh = (en_lo + en_hi) * CLK_X + Trloh[mode];

               data_invalid =
                   data_invalid_rhoh <
                   data_invalid_rloh ? data_invalid_rhoh : data_invalid_rloh;

               dv_window = data_invalid - Trea[mode];

               if (dv_window < 8)
                       en_lo++;
       }

       acc_clks = CEIL_DIV(Trea[mode], CLK_X);

       while (((acc_clks * CLK_X) - Trea[mode]) < 3)
               acc_clks++;

       if ((data_invalid - acc_clks * CLK_X) < 2)
               nand_dbg_print(NAND_DBG_WARN, "%s, Line %d: Warning!\n",
                       __FILE__, __LINE__);

       addr_2_data = CEIL_DIV(Tadl[mode], CLK_X);
       re_2_we = CEIL_DIV(Trhw[mode], CLK_X);
       re_2_re = CEIL_DIV(Trhz[mode], CLK_X);
       we_2_re = CEIL_DIV(Twhr[mode], CLK_X);
       cs_cnt = CEIL_DIV((Tcs[mode] - Trp[mode]), CLK_X);
       if (!TclsRising)
               cs_cnt = CEIL_DIV(Tcs[mode], CLK_X);
       if (cs_cnt == 0)
               cs_cnt = 1;

       if (Tcea[mode]) {
               while (((cs_cnt * CLK_X) + Trea[mode]) < Tcea[mode])
                       cs_cnt++;
       }

       iowrite32(acc_clks, FlashReg + ACC_CLKS);
       iowrite32(re_2_we, FlashReg + RE_2_WE);
       iowrite32(re_2_re, FlashReg + RE_2_RE);
       iowrite32(we_2_re, FlashReg + WE_2_RE);
       iowrite32(addr_2_data, FlashReg + ADDR_2_DATA);
       iowrite32(en_lo, FlashReg + RDWR_EN_LO_CNT);
       iowrite32(en_hi, FlashReg + RDWR_EN_HI_CNT);
       iowrite32(cs_cnt, FlashReg + CS_SETUP_CNT);
}

static void index_addr(u32 address, u32 data)
{
       iowrite32(address, FlashMem);
       iowrite32(data, FlashMem + 0x10);
}

static void index_addr_read_data(u32 address, u32 *pdata)
{
       iowrite32(address, FlashMem);
       *pdata = ioread32(FlashMem + 0x10);
}

static void set_ecc_config(void)
{
       if ((ioread32(FlashReg + ECC_CORRECTION) & ECC_CORRECTION__VALUE)
               == 1) {
               DeviceInfo.wECCBytesPerSector = 4;
               DeviceInfo.wECCBytesPerSector *= DeviceInfo.wDevicesConnected;
               DeviceInfo.wNumPageSpareFlag =
                       DeviceInfo.wPageSpareSize -
                       DeviceInfo.wPageDataSize /
                       (ECC_SECTOR_SIZE * DeviceInfo.wDevicesConnected) *
                       DeviceInfo.wECCBytesPerSector
                       - DeviceInfo.wSpareSkipBytes;
       } else {
               DeviceInfo.wECCBytesPerSector =
                       (ioread32(FlashReg + ECC_CORRECTION) &
                       ECC_CORRECTION__VALUE) * 13 / 8;
               if ((DeviceInfo.wECCBytesPerSector) % 2 == 0)
                       DeviceInfo.wECCBytesPerSector += 2;
               else
                       DeviceInfo.wECCBytesPerSector += 1;

               DeviceInfo.wECCBytesPerSector *= DeviceInfo.wDevicesConnected;
               DeviceInfo.wNumPageSpareFlag = DeviceInfo.wPageSpareSize -
                       DeviceInfo.wPageDataSize /
                       (ECC_SECTOR_SIZE * DeviceInfo.wDevicesConnected) *
                       DeviceInfo.wECCBytesPerSector
                       - DeviceInfo.wSpareSkipBytes;
       }

}

static u16 get_onfi_nand_para(void)
{
       int i;
       u16 blks_lun_l, blks_lun_h, n_of_luns;
       u32 blockperlun, id;

       iowrite32(DEVICE_RESET__BANK0, FlashReg + DEVICE_RESET);

       while (!((ioread32(FlashReg + INTR_STATUS0) &
               INTR_STATUS0__RST_COMP) |
               (ioread32(FlashReg + INTR_STATUS0) &
               INTR_STATUS0__TIME_OUT)))
               ;

       if (ioread32(FlashReg + INTR_STATUS0) & INTR_STATUS0__RST_COMP) {
               iowrite32(DEVICE_RESET__BANK1, FlashReg + DEVICE_RESET);
               while (!((ioread32(FlashReg + INTR_STATUS1) &
                       INTR_STATUS1__RST_COMP) |
                       (ioread32(FlashReg + INTR_STATUS1) &
                       INTR_STATUS1__TIME_OUT)))
                       ;

               if (ioread32(FlashReg + INTR_STATUS1) &
                       INTR_STATUS1__RST_COMP) {
                       iowrite32(DEVICE_RESET__BANK2,
                               FlashReg + DEVICE_RESET);
                       while (!((ioread32(FlashReg + INTR_STATUS2) &
                               INTR_STATUS2__RST_COMP) |
                               (ioread32(FlashReg + INTR_STATUS2) &
                               INTR_STATUS2__TIME_OUT)))
                               ;

                       if (ioread32(FlashReg + INTR_STATUS2) &
                               INTR_STATUS2__RST_COMP) {
                               iowrite32(DEVICE_RESET__BANK3,
                                       FlashReg + DEVICE_RESET);
                               while (!((ioread32(FlashReg + INTR_STATUS3) &
                                       INTR_STATUS3__RST_COMP) |
                                       (ioread32(FlashReg + INTR_STATUS3) &
                                       INTR_STATUS3__TIME_OUT)))
                                       ;
                       } else {
                               printk(KERN_ERR "Getting a time out for bank 2!\n");
                       }
               } else {
                       printk(KERN_ERR "Getting a time out for bank 1!\n");
               }
       }

       iowrite32(INTR_STATUS0__TIME_OUT, FlashReg + INTR_STATUS0);
       iowrite32(INTR_STATUS1__TIME_OUT, FlashReg + INTR_STATUS1);
       iowrite32(INTR_STATUS2__TIME_OUT, FlashReg + INTR_STATUS2);
       iowrite32(INTR_STATUS3__TIME_OUT, FlashReg + INTR_STATUS3);

       DeviceInfo.wONFIDevFeatures =
               ioread32(FlashReg + ONFI_DEVICE_FEATURES);
       DeviceInfo.wONFIOptCommands =
               ioread32(FlashReg + ONFI_OPTIONAL_COMMANDS);
       DeviceInfo.wONFITimingMode =
               ioread32(FlashReg + ONFI_TIMING_MODE);
       DeviceInfo.wONFIPgmCacheTimingMode =
               ioread32(FlashReg + ONFI_PGM_CACHE_TIMING_MODE);

       n_of_luns = ioread32(FlashReg + ONFI_DEVICE_NO_OF_LUNS) &
               ONFI_DEVICE_NO_OF_LUNS__NO_OF_LUNS;
       blks_lun_l = ioread32(FlashReg + ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_L);
       blks_lun_h = ioread32(FlashReg + ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_U);

       blockperlun = (blks_lun_h << 16) | blks_lun_l;

       DeviceInfo.wTotalBlocks = n_of_luns * blockperlun;

       if (!(ioread32(FlashReg + ONFI_TIMING_MODE) &
               ONFI_TIMING_MODE__VALUE))
               return FAIL;

       for (i = 5; i > 0; i--) {
               if (ioread32(FlashReg + ONFI_TIMING_MODE) & (0x01 << i))
                       break;
       }

#if MODE5_WORKAROUND
       if (i == 5)
               i = 4;
#endif

       NAND_ONFi_Timing_Mode(i);

       index_addr(MODE_11 | 0, 0x90);
       index_addr(MODE_11 | 1, 0);

       for (i = 0; i < 3; i++)
               index_addr_read_data(MODE_11 | 2, &id);

       nand_dbg_print(NAND_DBG_DEBUG, "3rd ID: 0x%x\n", id);

       DeviceInfo.MLCDevice = id & 0x0C;

       return PASS;
}

static void get_samsung_nand_para(void)
{
       u8 no_of_planes;
       u32 blk_size;
       u64 plane_size, capacity;
       u32 id_bytes[5];
       int i;

       index_addr((u32)(MODE_11 | 0), 0x90);
       index_addr((u32)(MODE_11 | 1), 0);
       for (i = 0; i < 5; i++)
               index_addr_read_data((u32)(MODE_11 | 2), &id_bytes[i]);

       nand_dbg_print(NAND_DBG_DEBUG,
               "ID bytes: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
               id_bytes[0], id_bytes[1], id_bytes[2],
               id_bytes[3], id_bytes[4]);

       no_of_planes = 1 << ((id_bytes[4] & 0x0c) >> 2);
       plane_size  = (u64)64 << ((id_bytes[4] & 0x70) >> 4);
       blk_size = 64 << ((ioread32(FlashReg + DEVICE_PARAM_1) & 0x30) >> 4);
       capacity = (u64)128 * plane_size * no_of_planes;

       DeviceInfo.wTotalBlocks = (u32)GLOB_u64_Div(capacity, blk_size);
}

static void find_valid_banks(void)
{
       u32 id[LLD_MAX_FLASH_BANKS];
       int i;

       totalUsedBanks = 0;
       for (i = 0; i < LLD_MAX_FLASH_BANKS; i++) {
               index_addr((u32)(MODE_11 | (i << 24) | 0), 0x90);
               index_addr((u32)(MODE_11 | (i << 24) | 1), 0);
               index_addr_read_data((u32)(MODE_11 | (i << 24) | 2), &id[i]);

               nand_dbg_print(NAND_DBG_DEBUG,
                       "Return 1st ID for bank[%d]: %x\n", i, id[i]);

               if (i == 0) {
                       if (id[i] & 0x0ff)
                               GLOB_valid_banks[i] = 1;
               } else {
                       if ((id[i] & 0x0ff) == (id[0] & 0x0ff))
                               GLOB_valid_banks[i] = 1;
               }

               totalUsedBanks += GLOB_valid_banks[i];
       }

       nand_dbg_print(NAND_DBG_DEBUG,
               "totalUsedBanks: %d\n", totalUsedBanks);
}

static void detect_partition_feature(void)
{
       if (ioread32(FlashReg + FEATURES) & FEATURES__PARTITION) {
               if ((ioread32(FlashReg + PERM_SRC_ID_1) &
                       PERM_SRC_ID_1__SRCID) == SPECTRA_PARTITION_ID) {
                       DeviceInfo.wSpectraStartBlock =
                           ((ioread32(FlashReg + MIN_MAX_BANK_1) &
                             MIN_MAX_BANK_1__MIN_VALUE) *
                            DeviceInfo.wTotalBlocks)
                           +
                           (ioread32(FlashReg + MIN_BLK_ADDR_1) &
                           MIN_BLK_ADDR_1__VALUE);

                       DeviceInfo.wSpectraEndBlock =
                           (((ioread32(FlashReg + MIN_MAX_BANK_1) &
                              MIN_MAX_BANK_1__MAX_VALUE) >> 2) *
                            DeviceInfo.wTotalBlocks)
                           +
                           (ioread32(FlashReg + MAX_BLK_ADDR_1) &
                           MAX_BLK_ADDR_1__VALUE);

                       DeviceInfo.wTotalBlocks *= totalUsedBanks;

                       if (DeviceInfo.wSpectraEndBlock >=
                           DeviceInfo.wTotalBlocks) {
                               DeviceInfo.wSpectraEndBlock =
                                   DeviceInfo.wTotalBlocks - 1;
                       }

                       DeviceInfo.wDataBlockNum =
                               DeviceInfo.wSpectraEndBlock -
                               DeviceInfo.wSpectraStartBlock + 1;
               } else {
                       DeviceInfo.wTotalBlocks *= totalUsedBanks;
                       DeviceInfo.wSpectraStartBlock = SPECTRA_START_BLOCK;
                       DeviceInfo.wSpectraEndBlock =
                               DeviceInfo.wTotalBlocks - 1;
                       DeviceInfo.wDataBlockNum =
                               DeviceInfo.wSpectraEndBlock -
                               DeviceInfo.wSpectraStartBlock + 1;
               }
       } else {
               DeviceInfo.wTotalBlocks *= totalUsedBanks;
               DeviceInfo.wSpectraStartBlock = SPECTRA_START_BLOCK;
               DeviceInfo.wSpectraEndBlock = DeviceInfo.wTotalBlocks - 1;
               DeviceInfo.wDataBlockNum =
                       DeviceInfo.wSpectraEndBlock -
                       DeviceInfo.wSpectraStartBlock + 1;
       }
}

static void dump_device_info(void)
{
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceInfo:\n");
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceMaker: 0x%x\n",
               DeviceInfo.wDeviceMaker);
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceType: 0x%x\n",
               DeviceInfo.wDeviceType);
       nand_dbg_print(NAND_DBG_DEBUG, "SpectraStartBlock: %d\n",
               DeviceInfo.wSpectraStartBlock);
       nand_dbg_print(NAND_DBG_DEBUG, "SpectraEndBlock: %d\n",
               DeviceInfo.wSpectraEndBlock);
       nand_dbg_print(NAND_DBG_DEBUG, "TotalBlocks: %d\n",
               DeviceInfo.wTotalBlocks);
       nand_dbg_print(NAND_DBG_DEBUG, "PagesPerBlock: %d\n",
               DeviceInfo.wPagesPerBlock);
       nand_dbg_print(NAND_DBG_DEBUG, "PageSize: %d\n",
               DeviceInfo.wPageSize);
       nand_dbg_print(NAND_DBG_DEBUG, "PageDataSize: %d\n",
               DeviceInfo.wPageDataSize);
       nand_dbg_print(NAND_DBG_DEBUG, "PageSpareSize: %d\n",
               DeviceInfo.wPageSpareSize);
       nand_dbg_print(NAND_DBG_DEBUG, "NumPageSpareFlag: %d\n",
               DeviceInfo.wNumPageSpareFlag);
       nand_dbg_print(NAND_DBG_DEBUG, "ECCBytesPerSector: %d\n",
               DeviceInfo.wECCBytesPerSector);
       nand_dbg_print(NAND_DBG_DEBUG, "BlockSize: %d\n",
               DeviceInfo.wBlockSize);
       nand_dbg_print(NAND_DBG_DEBUG, "BlockDataSize: %d\n",
               DeviceInfo.wBlockDataSize);
       nand_dbg_print(NAND_DBG_DEBUG, "DataBlockNum: %d\n",
               DeviceInfo.wDataBlockNum);
       nand_dbg_print(NAND_DBG_DEBUG, "PlaneNum: %d\n",
               DeviceInfo.bPlaneNum);
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceMainAreaSize: %d\n",
               DeviceInfo.wDeviceMainAreaSize);
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceSpareAreaSize: %d\n",
               DeviceInfo.wDeviceSpareAreaSize);
       nand_dbg_print(NAND_DBG_DEBUG, "DevicesConnected: %d\n",
               DeviceInfo.wDevicesConnected);
       nand_dbg_print(NAND_DBG_DEBUG, "DeviceWidth: %d\n",
               DeviceInfo.wDeviceWidth);
       nand_dbg_print(NAND_DBG_DEBUG, "HWRevision: 0x%x\n",
               DeviceInfo.wHWRevision);
       nand_dbg_print(NAND_DBG_DEBUG, "HWFeatures: 0x%x\n",
               DeviceInfo.wHWFeatures);
       nand_dbg_print(NAND_DBG_DEBUG, "ONFIDevFeatures: 0x%x\n",
               DeviceInfo.wONFIDevFeatures);
       nand_dbg_print(NAND_DBG_DEBUG, "ONFIOptCommands: 0x%x\n",
               DeviceInfo.wONFIOptCommands);
       nand_dbg_print(NAND_DBG_DEBUG, "ONFITimingMode: 0x%x\n",
               DeviceInfo.wONFITimingMode);
       nand_dbg_print(NAND_DBG_DEBUG, "ONFIPgmCacheTimingMode: 0x%x\n",
               DeviceInfo.wONFIPgmCacheTimingMode);
       nand_dbg_print(NAND_DBG_DEBUG, "MLCDevice: %s\n",
               DeviceInfo.MLCDevice ? "Yes" : "No");
       nand_dbg_print(NAND_DBG_DEBUG, "SpareSkipBytes: %d\n",
               DeviceInfo.wSpareSkipBytes);
       nand_dbg_print(NAND_DBG_DEBUG, "BitsInPageNumber: %d\n",
               DeviceInfo.nBitsInPageNumber);
       nand_dbg_print(NAND_DBG_DEBUG, "BitsInPageDataSize: %d\n",
               DeviceInfo.nBitsInPageDataSize);
       nand_dbg_print(NAND_DBG_DEBUG, "BitsInBlockDataSize: %d\n",
               DeviceInfo.nBitsInBlockDataSize);
}

u16 NAND_Read_Device_ID(void)
{
       u16 status = PASS;
       u8 mfg_code, dev_code;
       u8 no_of_planes;
       u32 tmp;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       iowrite32(0x02, FlashReg + SPARE_AREA_SKIP_BYTES);
       iowrite32(0xffff, FlashReg + SPARE_AREA_MARKER);
       DeviceInfo.wDeviceMaker = ioread32(FlashReg + MANUFACTURER_ID);
       DeviceInfo.wDeviceType = (((ioread32(FlashReg + DEVICE_WIDTH) >> 2)
               > 0) ? 16 : 8);
       DeviceInfo.wPagesPerBlock = ioread32(FlashReg + PAGES_PER_BLOCK);
       DeviceInfo.wPageDataSize =
               ioread32(FlashReg + LOGICAL_PAGE_DATA_SIZE);

       /* Note: When using the Micon 4K NAND device, the controller will report
        * Page Spare Size as 216 bytes. But Micron's Spec say it's 218 bytes.
        * And if force set it to 218 bytes, the controller can not work
        * correctly. So just let it be. But keep in mind that this bug may
        * cause
        * other problems in future.       - Yunpeng  2008-10-10
        */
       DeviceInfo.wPageSpareSize =
               ioread32(FlashReg + LOGICAL_PAGE_SPARE_SIZE);

       DeviceInfo.wPageSize =
           DeviceInfo.wPageDataSize + DeviceInfo.wPageSpareSize;
       DeviceInfo.wBlockSize =
           DeviceInfo.wPageSize * DeviceInfo.wPagesPerBlock;
       DeviceInfo.wBlockDataSize =
           DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize;
       DeviceInfo.wHWRevision = ioread32(FlashReg + REVISION);

       DeviceInfo.wDeviceMainAreaSize =
               ioread32(FlashReg + DEVICE_MAIN_AREA_SIZE);
       DeviceInfo.wDeviceSpareAreaSize =
               ioread32(FlashReg + DEVICE_SPARE_AREA_SIZE);

       DeviceInfo.wDeviceWidth = ioread32(FlashReg + DEVICE_WIDTH);
       DeviceInfo.wDevicesConnected = ioread32(FlashReg + DEVICES_CONNECTED);
       DeviceInfo.wHWFeatures = ioread32(FlashReg + FEATURES);

       /* nand_dbg_print(NAND_DBG_DEBUG, "Will disable ECC for now:\n");*/
       /* iowrite32(0, FlashReg + ECC_ENABLE); */

       DeviceInfo.MLCDevice = ioread32(FlashReg + DEVICE_PARAM_0) & 0x0c;
       DeviceInfo.wSpareSkipBytes = ioread32(FlashReg +
                               SPARE_AREA_SKIP_BYTES)
                               * DeviceInfo.wDevicesConnected;

       DeviceInfo.nBitsInPageNumber =
               (u8)GLOB_Calc_Used_Bits(DeviceInfo.wPagesPerBlock);
       DeviceInfo.nBitsInPageDataSize =
               (u8)GLOB_Calc_Used_Bits(DeviceInfo.wPageDataSize);
       DeviceInfo.nBitsInBlockDataSize =
               (u8)GLOB_Calc_Used_Bits(DeviceInfo.wBlockDataSize);

#if SUPPORT_8BITECC
       if ((ioread32(FlashReg + DEVICE_MAIN_AREA_SIZE) < 4096) ||
               (ioread32(FlashReg + DEVICE_SPARE_AREA_SIZE) <= 128))
               iowrite32(8, FlashReg + ECC_CORRECTION);
#endif

       nand_dbg_print(NAND_DBG_DEBUG, "FEATURES register value: 0x%x\n",
               ioread32(FlashReg + FEATURES));
       nand_dbg_print(NAND_DBG_DEBUG, "ECC_CORRECTION register value: 0x%x\n",
               ioread32(FlashReg + ECC_CORRECTION));

       /* Toshiba NAND */
       if ((ioread32(FlashReg + MANUFACTURER_ID) == 0x98) &&
               (ioread32(FlashReg + DEVICE_MAIN_AREA_SIZE) == 4096) &&
               (ioread32(FlashReg + DEVICE_SPARE_AREA_SIZE) == 64)) {
               iowrite32(216, FlashReg + DEVICE_SPARE_AREA_SIZE);
               tmp = ioread32(FlashReg + DEVICES_CONNECTED) *
                       ioread32(FlashReg + DEVICE_SPARE_AREA_SIZE);
               iowrite32(tmp, FlashReg + LOGICAL_PAGE_SPARE_SIZE);
               DeviceInfo.wDeviceSpareAreaSize =
                       ioread32(FlashReg + DEVICE_SPARE_AREA_SIZE);
               DeviceInfo.wPageSpareSize =
                       ioread32(FlashReg + LOGICAL_PAGE_SPARE_SIZE);
#if SUPPORT_15BITECC
               iowrite32(15, FlashReg + ECC_CORRECTION);
#elif SUPPORT_8BITECC
               iowrite32(8, FlashReg + ECC_CORRECTION);
#endif
       }

       set_ecc_config();

       mfg_code = DeviceInfo.wDeviceMaker;
       dev_code = DeviceInfo.wDeviceType;

       if (ioread32(FlashReg + ONFI_DEVICE_NO_OF_LUNS) &
               ONFI_DEVICE_NO_OF_LUNS__ONFI_DEVICE) { /* ONFI 1.0 NAND */
               if (FAIL == get_onfi_nand_para())
                       return FAIL;
       } else if (mfg_code == 0xEC)    { /* Samsung NAND */
               get_samsung_nand_para();
       } else {
#if GLOB_DEVTSBA_ALT_BLK_NFO
       u8 *tsba_ptr = (u8 *)GLOB_DEVTSBA_ALT_BLK_ADD;
       DeviceInfo.wTotalBlocks = (1 << *tsba_ptr);
       if (DeviceInfo.wTotalBlocks < 512)
               DeviceInfo.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
#else
       DeviceInfo.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
#endif
       }

       no_of_planes = ioread32(FlashReg + NUMBER_OF_PLANES) &
               NUMBER_OF_PLANES__VALUE;

       switch (no_of_planes) {
       case 0:
       case 1:
       case 3:
       case 7:
               DeviceInfo.bPlaneNum = no_of_planes + 1;
               break;
       default:
               status = FAIL;
               break;
       }

       find_valid_banks();

       detect_partition_feature();

       dump_device_info();

       return status;
}

u16 NAND_UnlockArrayAll(void)
{
       u64 start_addr, end_addr;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       start_addr = 0;
       end_addr = ((u64)DeviceInfo.wBlockSize *
               (DeviceInfo.wTotalBlocks - 1)) >>
               DeviceInfo.nBitsInPageDataSize;

       index_addr((u32)(MODE_10 | (u32)start_addr), 0x10);
       index_addr((u32)(MODE_10 | (u32)end_addr), 0x11);

       return PASS;
}

void NAND_LLD_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       if (INT_ENABLE)
               iowrite32(1, FlashReg + GLOBAL_INT_ENABLE);
       else
               iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);
}

u16 NAND_Erase_Block(u32 block)
{
       u16 status = PASS;
       u64 flash_add;
       u16 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (block >= DeviceInfo.wTotalBlocks)
               status = FAIL;

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];

               iowrite32(INTR_STATUS0__ERASE_COMP | INTR_STATUS0__ERASE_FAIL,
                       FlashReg + intr_status);

               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)), 1);

               while (!(ioread32(FlashReg + intr_status) &
                       (INTR_STATUS0__ERASE_COMP | INTR_STATUS0__ERASE_FAIL)))
                       ;

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ERASE_FAIL)
                       status = FAIL;

               iowrite32(INTR_STATUS0__ERASE_COMP | INTR_STATUS0__ERASE_FAIL,
                       FlashReg + intr_status);
       }

       return status;
}

static u32 Boundary_Check_Block_Page(u32 block, u16 page,
                                               u16 page_count)
{
       u32 status = PASS;

       if (block >= DeviceInfo.wTotalBlocks)
               status = FAIL;

       if (page + page_count > DeviceInfo.wPagesPerBlock)
               status = FAIL;

       return status;
}

u16 NAND_Read_Page_Spare(u8 *read_data, u32 block, u16 page,
                           u16 page_count)
{
       u32 status = PASS;
       u32 i;
       u64 flash_add;
       u32 PageSpareSize = DeviceInfo.wPageSpareSize;
       u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};

       if (block >= DeviceInfo.wTotalBlocks) {
               printk(KERN_ERR "block too big: %d\n", (int)block);
               status = FAIL;
       }

       if (page >= DeviceInfo.wPagesPerBlock) {
               printk(KERN_ERR "page too big: %d\n", page);
               status = FAIL;
       }

       if (page_count > 1) {
               printk(KERN_ERR "page count too big: %d\n", page_count);
               status = FAIL;
       }

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];
               iowrite32(ioread32(FlashReg + intr_status),
                       FlashReg + intr_status);

               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                       0x41);
               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                       0x2000 | page_count);
               while (!(ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__LOAD_COMP))
                       ;

               iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                       FlashMem);

               for (i = 0; i < (PageSpareSize / 4); i++)
                       *((u32 *)page_spare + i) =
                                       ioread32(FlashMem + 0x10);

               if (ioread32(FlashReg + ECC_ENABLE)) {
                       for (i = 0; i < spareFlagBytes; i++)
                               read_data[i] =
                                       page_spare[PageSpareSize -
                                               spareFlagBytes + i];
                       for (i = 0; i < (PageSpareSize - spareFlagBytes); i++)
                               read_data[spareFlagBytes + i] =
                                                       page_spare[i];
               } else {
                       for (i = 0; i < PageSpareSize; i++)
                               read_data[i] = page_spare[i];
               }

               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);
       }

       return status;
}

u16 NAND_Write_Page_Spare(u8 *write_data, u32 block, u16 page,
                            u16 page_count)
{
       printk(KERN_ERR
              "Error! This function (NAND_Write_Page_Spare) should never"
               " be called!\n");
       return ERR;
}

#if DDMA
/* op value:  0 - DDMA read;  1 - DDMA write */
static void ddma_trans(u8 *data, u64 flash_add,
                       u32 flash_bank, int op, u32 numPages)
{
       /* Map virtual address to bus address for DDMA */
       data = (u8 *)GLOB_MEMMAP_TOBUS((u32 *)data);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               (u16)(2 << 12) | (op << 8) | numPages);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               ((u16)(0x0FFFF & ((u32)data >> 16)) << 8)),
               (u16)(2 << 12) | (2 << 8) | 0);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               ((u16)(0x0FFFF & (u32)data) << 8)),
               (u16)(2 << 12) | (3 << 8) | 0);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (1 << 16) | (0x40 << 8)),
               (u16)(2 << 12) | (4 << 8) | 0);
}

#endif

/* If data in buf are all 0xff, then return 1; otherwise return 0 */
static int check_all_1(u8 *buf)
{
       int i, j, cnt;

       for (i = 0; i < DeviceInfo.wPageDataSize; i++) {
               if (buf[i] != 0xff) {
                       cnt = 0;
                       nand_dbg_print(NAND_DBG_WARN,
                               "the first non-0xff data byte is: %d\n", i);
                       for (j = i; j < DeviceInfo.wPageDataSize; j++) {
                               nand_dbg_print(NAND_DBG_WARN, "0x%x ", buf[j]);
                               cnt++;
                               if (cnt > 8)
                                       break;
                       }
                       nand_dbg_print(NAND_DBG_WARN, "\n");
                       return 0;
               }
       }

       return 1;
}

static int do_ecc_new(unsigned long bank, u8 *buf,
                               u32 block, u16 page)
{
       int status = PASS;
       u16 err_page = 0;
       u16 err_byte;
       u8 err_sect;
       u8 err_dev;
       u16 err_fix_info;
       u16 err_addr;
       u32 ecc_sect_size;
       u8 *err_pos;
       u32 err_page_addr[4] = {ERR_PAGE_ADDR0,
               ERR_PAGE_ADDR1, ERR_PAGE_ADDR2, ERR_PAGE_ADDR3};

       ecc_sect_size = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);

       do {
               err_page = ioread32(FlashReg + err_page_addr[bank]);
               err_addr = ioread32(FlashReg + ECC_ERROR_ADDRESS);
               err_byte = err_addr & ECC_ERROR_ADDRESS__OFFSET;
               err_sect = ((err_addr & ECC_ERROR_ADDRESS__SECTOR_NR) >> 12);
               err_fix_info = ioread32(FlashReg + ERR_CORRECTION_INFO);
               err_dev = ((err_fix_info & ERR_CORRECTION_INFO__DEVICE_NR)
                       >> 8);
               if (err_fix_info & ERR_CORRECTION_INFO__ERROR_TYPE) {
                       nand_dbg_print(NAND_DBG_WARN,
                               "%s, Line %d Uncorrectable ECC error "
                               "when read block %d page %d."
                               "PTN_INTR register: 0x%x "
                               "err_page: %d, err_sect: %d, err_byte: %d, "
                               "err_dev: %d, ecc_sect_size: %d, "
                               "err_fix_info: 0x%x\n",
                               __FILE__, __LINE__, block, page,
                               ioread32(FlashReg + PTN_INTR),
                               err_page, err_sect, err_byte, err_dev,
                               ecc_sect_size, (u32)err_fix_info);

                       if (check_all_1(buf))
                               nand_dbg_print(NAND_DBG_WARN, "%s, Line %d"
                                              "All 0xff!\n",
                                              __FILE__, __LINE__);
                       else
                               nand_dbg_print(NAND_DBG_WARN, "%s, Line %d"
                                              "Not all 0xff!\n",
                                              __FILE__, __LINE__);
                       status = FAIL;
               } else {
                       /* glob_mdelay(200); */ /* Add for test */
                       nand_dbg_print(NAND_DBG_WARN,
                               "%s, Line %d Found ECC error "
                               "when read block %d page %d."
                               "err_page: %d, err_sect: %d, err_byte: %d, "
                               "err_dev: %d, ecc_sect_size: %d, "
                               "err_fix_info: 0x%x\n",
                               __FILE__, __LINE__, block, page,
                               err_page, err_sect, err_byte, err_dev,
                               ecc_sect_size, (u32)err_fix_info);
                       if (err_byte < ecc_sect_size) {
                               err_pos = buf +
                                       (err_page - page) *
                                       DeviceInfo.wPageDataSize +
                                       err_sect * ecc_sect_size +
                                       err_byte *
                                       DeviceInfo.wDevicesConnected +
                                       err_dev;

                               *err_pos ^= err_fix_info &
                                       ERR_CORRECTION_INFO__BYTEMASK;
                       } else {
                               nand_dbg_print(NAND_DBG_WARN,
                                       "!!!Error - Too big err_byte!\n");
                       }
               }
       } while (!(err_fix_info & ERR_CORRECTION_INFO__LAST_ERR_INFO));

       return status;
}

u16 NAND_Read_Page_Main_Polling(u8 *read_data,
               u32 block, u16 page, u16 page_count)
{
       u32 status = PASS;
       u64 flash_add;
       u32 intr_status = 0;
       u32 flash_bank;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);
       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;
       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       intr_status = intr_status_addresses[flash_bank];
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       if (page_count > 1) {
               if (ioread32(FlashReg + MULTIPLANE_OPERATION))
                       status = NAND_Multiplane_Read(read_data,
                                               block, page, page_count);
               else
                       status = NAND_Pipeline_Read_Ahead_Polling(read_data,
                                               block, page, page_count);
               return status;
       }

       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       ddma_trans(read_data, flash_add, flash_bank, 0, 1);

       if (ioread32(FlashReg + ECC_ENABLE)) {
               while (!(ioread32(FlashReg + intr_status) &
                       (INTR_STATUS0__ECC_TRANSACTION_DONE |
                       INTR_STATUS0__ECC_ERR)))
                       ;

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_ERR) {
                       iowrite32(INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
                       status = do_ecc_new(flash_bank, read_data,
                                       block, page);
               }

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_TRANSACTION_DONE &
                       INTR_STATUS0__ECC_ERR)
                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE |
                               INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
               else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_TRANSACTION_DONE)
                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                               FlashReg + intr_status);
               else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_ERR)
                       iowrite32(INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
       } else {
               while (!(ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__DMA_CMD_COMP))
                       ;
               iowrite32(INTR_STATUS0__DMA_CMD_COMP, FlashReg + intr_status);
       }

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);
       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       return status;
}

u16 NAND_Pipeline_Read_Ahead_Polling(u8 *read_data,
                       u32 block, u16 page, u16 page_count)
{
       u32 status = PASS;
       u32 NumPages = page_count;
       u64 flash_add;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
       u32 ecc_done_OR_dma_comp;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);

       if (page_count < 2)
               status = FAIL;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               *DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];
               iowrite32(ioread32(FlashReg + intr_status),
                       FlashReg + intr_status);

               iowrite32(1, FlashReg + DMA_ENABLE);
               while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;

               iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);
               ddma_trans(read_data, flash_add, flash_bank, 0, NumPages);

               ecc_done_OR_dma_comp = 0;
               while (1) {
                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               while (!ioread32(FlashReg + intr_status))
                                       ;

                               if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                                       status = do_ecc_new(flash_bank,
                                               read_data, block, page);
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__DMA_CMD_COMP) {
                                       iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                               FlashReg + intr_status);

                                       if (1 == ecc_done_OR_dma_comp)
                                               break;

                                       ecc_done_OR_dma_comp = 1;
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_TRANSACTION_DONE) {
                                       iowrite32(
                                       INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);

                                       if (1 == ecc_done_OR_dma_comp)
                                               break;

                                       ecc_done_OR_dma_comp = 1;
                               }
                       } else {
                               while (!(ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__DMA_CMD_COMP))
                                       ;

                               iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                       FlashReg + intr_status);
                               break;
                       }

                       iowrite32((~INTR_STATUS0__ECC_ERR) &
                               (~INTR_STATUS0__ECC_TRANSACTION_DONE) &
                               (~INTR_STATUS0__DMA_CMD_COMP),
                               FlashReg + intr_status);

               }

               iowrite32(ioread32(FlashReg + intr_status),
                       FlashReg + intr_status);

               iowrite32(0, FlashReg + DMA_ENABLE);

               while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;
       }
       return status;
}

u16 NAND_Read_Page_Main(u8 *read_data, u32 block, u16 page,
                          u16 page_count)
{
       u32 status = PASS;
       u64 flash_add;
       u32 intr_status = 0;
       u32 flash_bank;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
#if DDMA
       int ret;
#else
       u32 i;
#endif

       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);
       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;
       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       intr_status = intr_status_addresses[flash_bank];
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       if (page_count > 1) {
               if (ioread32(FlashReg + MULTIPLANE_OPERATION))
                       status = NAND_Multiplane_Read(read_data,
                                               block, page, page_count);
               else
                       status = NAND_Pipeline_Read_Ahead(read_data,
                                               block, page, page_count);
               return status;
       }

#if DDMA
       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       /* Fill the mrst_nand_info structure */
       info.state = INT_READ_PAGE_MAIN;
       info.read_data = read_data;
       info.flash_bank = flash_bank;
       info.block = block;
       info.page = page;
       info.ret = PASS;

       ddma_trans(read_data, flash_add, flash_bank, 0, 1);

       iowrite32(1, FlashReg + GLOBAL_INT_ENABLE); /* Enable Interrupt */

       ret = wait_for_completion_timeout(&info.complete, 10 * HZ);
       if (!ret)
               printk(KERN_ERR "Wait for completion timeout "
                       "in %s, Line %d\n", __FILE__, __LINE__);
       status = info.ret;

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);
       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;
#else
       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x42);
       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x2000 | page_count);

       while (!(ioread32(FlashReg + intr_status) & INTR_STATUS0__LOAD_COMP))
               ;

       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), FlashMem);

       for (i = 0; i < DeviceInfo.wPageDataSize / 4; i++)
               *(((u32 *)read_data) + i) = ioread32(FlashMem + 0x10);

       if (ioread32(FlashReg + ECC_ENABLE)) {
               while (!(ioread32(FlashReg + intr_status) &
                       (INTR_STATUS0__ECC_TRANSACTION_DONE |
                       INTR_STATUS0__ECC_ERR)))
                       ;
               if (ioread32(FlashReg + intr_status) & INTR_STATUS0__ECC_ERR) {
                       iowrite32(INTR_STATUS0__ECC_ERR,
                                       FlashReg + intr_status);
                       status = do_ecc_new(flash_bank, read_data,
                                               block, page);
               }

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_TRANSACTION_DONE &
                       INTR_STATUS0__ECC_ERR)
                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE |
                               INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
               else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_TRANSACTION_DONE)
                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                               FlashReg + intr_status);
               else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_ERR)
                       iowrite32(INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
       }

#endif

       return status;
}

void Conv_Spare_Data_Log2Phy_Format(u8 *data)
{
       int i;
       const u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       const u32 PageSpareSize  = DeviceInfo.wPageSpareSize;

       if (ioread32(FlashReg + ECC_ENABLE)) {
               for (i = spareFlagBytes - 1; i >= 0; i++)
                       data[PageSpareSize - spareFlagBytes + i] = data[i];
       }
}

void Conv_Spare_Data_Phy2Log_Format(u8 *data)
{
       int i;
       const u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       const u32 PageSpareSize = DeviceInfo.wPageSpareSize;

       if (ioread32(FlashReg + ECC_ENABLE)) {
               for (i = 0; i < spareFlagBytes; i++)
                       data[i] = data[PageSpareSize - spareFlagBytes + i];
       }
}


void Conv_Main_Spare_Data_Log2Phy_Format(u8 *data, u16 page_count)
{
       const u32 PageSize = DeviceInfo.wPageSize;
       const u32 PageDataSize = DeviceInfo.wPageDataSize;
       const u32 eccBytes = DeviceInfo.wECCBytesPerSector;
       const u32 spareSkipBytes = DeviceInfo.wSpareSkipBytes;
       const u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       u32 eccSectorSize;
       u32 page_offset;
       int i, j;

       eccSectorSize = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);
       if (ioread32(FlashReg + ECC_ENABLE)) {
               while (page_count > 0) {
                       page_offset = (page_count - 1) * PageSize;
                       j = (DeviceInfo.wPageDataSize / eccSectorSize);
                       for (i = spareFlagBytes - 1; i >= 0; i--)
                               data[page_offset +
                                       (eccSectorSize + eccBytes) * j + i] =
                                       data[page_offset + PageDataSize + i];
                       for (j--; j >= 1; j--) {
                               for (i = eccSectorSize - 1; i >= 0; i--)
                                       data[page_offset +
                                       (eccSectorSize + eccBytes) * j + i] =
                                               data[page_offset +
                                               eccSectorSize * j + i];
                       }
                       for (i = (PageSize - spareSkipBytes) - 1;
                               i >= PageDataSize; i--)
                               data[page_offset + i + spareSkipBytes] =
                                       data[page_offset + i];
                       page_count--;
               }
       }
}

void Conv_Main_Spare_Data_Phy2Log_Format(u8 *data, u16 page_count)
{
       const u32 PageSize = DeviceInfo.wPageSize;
       const u32 PageDataSize = DeviceInfo.wPageDataSize;
       const u32 eccBytes = DeviceInfo.wECCBytesPerSector;
       const u32 spareSkipBytes = DeviceInfo.wSpareSkipBytes;
       const u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       u32 eccSectorSize;
       u32 page_offset;
       int i, j;

       eccSectorSize = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);
       if (ioread32(FlashReg + ECC_ENABLE)) {
               while (page_count > 0) {
                       page_offset = (page_count - 1) * PageSize;
                       for (i = PageDataSize;
                               i < PageSize - spareSkipBytes;
                               i++)
                               data[page_offset + i] =
                                       data[page_offset + i +
                                       spareSkipBytes];
                       for (j = 1;
                       j < DeviceInfo.wPageDataSize / eccSectorSize;
                       j++) {
                               for (i = 0; i < eccSectorSize; i++)
                                       data[page_offset +
                                       eccSectorSize * j + i] =
                                               data[page_offset +
                                               (eccSectorSize + eccBytes) * j
                                               + i];
                       }
                       for (i = 0; i < spareFlagBytes; i++)
                               data[page_offset + PageDataSize + i] =
                                       data[page_offset +
                                       (eccSectorSize + eccBytes) * j + i];
                       page_count--;
               }
       }
}

u16 NAND_Multiplane_Read(u8 *read_data, u32 block, u16 page,
                           u16 page_count)
{
       u32 status = PASS;
       u32 NumPages = page_count;
       u64 flash_add;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};

#if DDMA
       u32 ecc_done_OR_dma_comp;
#else
       u32 PageSize = DeviceInfo.wPageDataSize;
       u32 sector_count = 0;
       u32 SectorStart, SectorEnd;
       u32 bSectorsPerPage = 4;
       u32 i, page_num = 0;
       u32 plane = 0;
       u8 *read_data_l = read_data;
#endif

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];
               iowrite32(ioread32(FlashReg + intr_status),
                       FlashReg + intr_status);

               iowrite32(0, FlashReg + TRANSFER_SPARE_REG);
               iowrite32(0x01, FlashReg + MULTIPLANE_OPERATION);
#if DDMA

               iowrite32(1, FlashReg + DMA_ENABLE);
               while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;
               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);
               ddma_trans(read_data, flash_add, flash_bank, 0, NumPages);

               ecc_done_OR_dma_comp = 0;
               while (1) {
                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               while (!ioread32(FlashReg + intr_status))
                                       ;

                               if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                                       status = do_ecc_new(flash_bank,
                                               read_data, block, page);
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__DMA_CMD_COMP) {
                                       iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                               FlashReg + intr_status);

                                       if (1 == ecc_done_OR_dma_comp)
                                               break;

                                       ecc_done_OR_dma_comp = 1;
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_TRANSACTION_DONE) {
                                       iowrite32(
                                       INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);

                                       if (1 == ecc_done_OR_dma_comp)
                                               break;

                                       ecc_done_OR_dma_comp = 1;
                               }
                       } else {
                               while (!(ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__DMA_CMD_COMP))
                                       ;
                               iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                       FlashReg + intr_status);
                               break;
                       }

                       iowrite32((~INTR_STATUS0__ECC_ERR) &
                               (~INTR_STATUS0__ECC_TRANSACTION_DONE) &
                               (~INTR_STATUS0__DMA_CMD_COMP),
                               FlashReg + intr_status);

               }

               iowrite32(ioread32(FlashReg + intr_status),
                       FlashReg + intr_status);

               iowrite32(0, FlashReg + DMA_ENABLE);

               while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
                       ;

               iowrite32(0, FlashReg + MULTIPLANE_OPERATION);

#else

               if (ioread32(FlashReg + ECC_ENABLE))
                       iowrite32(INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);

               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                       0x42);
               index_addr((u32)(MODE_10 | (flash_bank << 24) |
                       (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                       0x2000 | page_count);

               while (NumPages > 0) {
                       if (plane == 0) {
                               iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                                       ((flash_add +
                                       page_num * DeviceInfo.wPageDataSize)
                                       >> DeviceInfo.nBitsInPageDataSize)),
                                       FlashMem);
                               plane = 1;
                       } else {
                               iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                                       ((flash_add +
                                       DeviceInfo.wBlockDataSize +
                                       page_num * DeviceInfo.wPageDataSize)
                                       >> DeviceInfo.nBitsInPageDataSize)),
                                       FlashMem);
                               plane = 0;
                       }

                       for (sector_count = 0; sector_count < bSectorsPerPage;
                            sector_count++) {
                               SectorStart = sector_count *
                                       (DeviceInfo.wPageDataSize /
                                       (4 * bSectorsPerPage));
                               SectorEnd = (sector_count + 1) *
                                       (DeviceInfo.wPageDataSize /
                                       (4 * bSectorsPerPage));

                               for (i = SectorStart; i < SectorEnd; i++)
                                       *(((u32 *)read_data_l) + i) =
                                               ioread32(FlashMem + 0x10);

                               if (ioread32(FlashReg + ECC_ENABLE)) {
                                       if (ioread32(FlashReg + intr_status) &
                                               INTR_STATUS0__ECC_ERR) {
                                               iowrite32(
                                               INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                                               status = do_ecc_new(
                                                       flash_bank,
                                                       read_data,
                                                       block, page);
                                       }
                               }
                       }

                       if (plane == 0)
                               page_num++;

                       read_data_l += PageSize;
                       --NumPages;
               }

               if (ioread32(FlashReg + ECC_ENABLE)) {
                       while (!(ioread32(FlashReg + intr_status) &
                               (INTR_STATUS0__ECC_TRANSACTION_DONE |
                                INTR_STATUS0__ECC_ERR)))
                               ;

                       if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__ECC_ERR) {
                               iowrite32(INTR_STATUS0__ECC_ERR,
                                       FlashReg + intr_status);
                               status = do_ecc_new(flash_bank,
                                       read_data, block, page);
                               while (!(ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_TRANSACTION_DONE))
                                       ;

                               iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);
                       } else if (ioread32(FlashReg + intr_status) &
                                  INTR_STATUS0__ECC_TRANSACTION_DONE) {
                               iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);
                       }
               }

               iowrite32(0, FlashReg + MULTIPLANE_OPERATION);

#endif
       }
       return status;
}

u16 NAND_Pipeline_Read_Ahead(u8 *read_data, u32 block,
                               u16 page, u16 page_count)
{
       u32 status = PASS;
       u32 NumPages = page_count;
       u64 flash_add;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
#if DDMA
       int ret;
#else
       u32 PageSize = DeviceInfo.wPageDataSize;
       u32 sector_count = 0;
       u32 SectorStart, SectorEnd;
       u32 bSectorsPerPage = 4;
       u32 i, page_num = 0;
       u8 *read_data_l = read_data;
#endif
       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);

       if (page_count < 2)
               status = FAIL;

       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               *DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       intr_status = intr_status_addresses[flash_bank];
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

#if DDMA
       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       /* Fill the mrst_nand_info structure */
       info.state = INT_PIPELINE_READ_AHEAD;
       info.read_data = read_data;
       info.flash_bank = flash_bank;
       info.block = block;
       info.page = page;
       info.ret = PASS;

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       ddma_trans(read_data, flash_add, flash_bank, 0, NumPages);

       iowrite32(1, FlashReg + GLOBAL_INT_ENABLE); /* Enable Interrupt */

       ret = wait_for_completion_timeout(&info.complete, 10 * HZ);
       if (!ret)
               printk(KERN_ERR "Wait for completion timeout "
                       "in %s, Line %d\n", __FILE__, __LINE__);

       status = info.ret;

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);

       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

#else
       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x2000 | NumPages);

       while (NumPages > 0) {
               iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                       ((flash_add + page_num * DeviceInfo.wPageDataSize) >>
                               DeviceInfo.nBitsInPageDataSize)), FlashMem);

               for (sector_count = 0; sector_count < bSectorsPerPage;
               sector_count++) {
                       SectorStart = sector_count *
                               (DeviceInfo.wPageDataSize /
                               (4 * bSectorsPerPage));
                       SectorEnd = (sector_count + 1) *
                               (DeviceInfo.wPageDataSize /
                               (4 * bSectorsPerPage));

                       for (i = SectorStart; i < SectorEnd; i++)
                               *(((u32 *)read_data_l) + i) =
                                       ioread32(FlashMem + 0x10);

                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                                       status = do_ecc_new(flash_bank,
                                               read_data, block, page);
                               }
                       }
               }

               read_data_l += PageSize;
               --NumPages;
               page_num++;
       }

       if (ioread32(FlashReg + ECC_ENABLE)) {
               while (!(ioread32(FlashReg + intr_status) &
                       (INTR_STATUS0__ECC_TRANSACTION_DONE |
                       INTR_STATUS0__ECC_ERR)))
                       ;

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_ERR) {
                       iowrite32(INTR_STATUS0__ECC_ERR,
                               FlashReg + intr_status);
                       status = do_ecc_new(flash_bank, read_data,
                               block, page);
                       while (!(ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__ECC_TRANSACTION_DONE))
                               ;

                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                               FlashReg + intr_status);
               } else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__ECC_TRANSACTION_DONE) {
                       iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                               FlashReg + intr_status);
               }
       }
#endif

       return status;
}


#endif
#if FLASH_NAND

u16 NAND_Write_Page_Main(u8 *write_data, u32 block, u16 page,
                           u16 page_count)
{
       u32 status = PASS;
       u64 flash_add;
       u32 intr_status = 0;
       u32 flash_bank;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
#if  DDMA
       int ret;
#else
       u32 i;
#endif

       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);
       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       intr_status = intr_status_addresses[flash_bank];

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       iowrite32(INTR_STATUS0__PROGRAM_COMP |
               INTR_STATUS0__PROGRAM_FAIL, FlashReg + intr_status);

       if (page_count > 1) {
               if (ioread32(FlashReg + MULTIPLANE_OPERATION))
                       status = NAND_Multiplane_Write(write_data,
                               block, page, page_count);
               else
                       status = NAND_Pipeline_Write_Ahead(write_data,
                               block, page, page_count);
               return status;
       }

#if DDMA
       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       /* Fill the mrst_nand_info structure */
       info.state = INT_WRITE_PAGE_MAIN;
       info.write_data = write_data;
       info.flash_bank = flash_bank;
       info.block = block;
       info.page = page;
       info.ret = PASS;

       ddma_trans(write_data, flash_add, flash_bank, 1, 1);

       iowrite32(1, FlashReg + GLOBAL_INT_ENABLE); /* Enable interrupt */

       ret = wait_for_completion_timeout(&info.complete, 10 * HZ);
       if (!ret)
               printk(KERN_ERR "Wait for completion timeout "
                       "in %s, Line %d\n", __FILE__, __LINE__);

       status = info.ret;

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);
       while (ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG)
               ;

#else
       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), FlashMem);

       for (i = 0; i < DeviceInfo.wPageDataSize / 4; i++)
               iowrite32(*(((u32 *)write_data) + i), FlashMem + 0x10);

       while (!(ioread32(FlashReg + intr_status) &
               (INTR_STATUS0__PROGRAM_COMP | INTR_STATUS0__PROGRAM_FAIL)))
               ;

       if (ioread32(FlashReg + intr_status) & INTR_STATUS0__PROGRAM_FAIL)
               status = FAIL;

       iowrite32(INTR_STATUS0__PROGRAM_COMP |
               INTR_STATUS0__PROGRAM_FAIL, FlashReg + intr_status);

#endif

       return status;
}

void NAND_ECC_Ctrl(int enable)
{
       if (enable) {
               nand_dbg_print(NAND_DBG_WARN,
                       "Will enable ECC in %s, Line %d, Function: %s\n",
                       __FILE__, __LINE__, __func__);
               iowrite32(1, FlashReg + ECC_ENABLE);
       } else {
               nand_dbg_print(NAND_DBG_WARN,
                       "Will disable ECC in %s, Line %d, Function: %s\n",
                       __FILE__, __LINE__, __func__);
               iowrite32(0, FlashReg + ECC_ENABLE);
       }
}

u32 NAND_Memory_Pool_Size(void)
{
       return MAX_PAGE_MAINSPARE_AREA;
}

int NAND_Mem_Config(u8 *pMem)
{
       return 0;
}

u16 NAND_Write_Page_Main_Spare(u8 *write_data, u32 block,
                                       u16 page, u16 page_count)
{
       u32 status = PASS;
       u32 i, j, page_num = 0;
       u32 PageSize = DeviceInfo.wPageSize;
       u32 PageDataSize = DeviceInfo.wPageDataSize;
       u32 eccBytes = DeviceInfo.wECCBytesPerSector;
       u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       u32 spareSkipBytes  = DeviceInfo.wSpareSkipBytes;
       u64 flash_add;
       u32 eccSectorSize;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
       u8 *page_main_spare;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       page_main_spare = kmalloc(DeviceInfo.wPageSize, GFP_ATOMIC);
       if (!page_main_spare) {
               printk(KERN_ERR "Failed to kmalloc memory in %s Line %d, exit.\n",
                       __FILE__, __LINE__);
               return FAIL;
       }

       eccSectorSize = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);

       status = Boundary_Check_Block_Page(block, page, page_count);

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];

               iowrite32(1, FlashReg + TRANSFER_SPARE_REG);

               while ((status != FAIL) && (page_count > 0)) {
                       flash_add = (u64)(block %
                       (DeviceInfo.wTotalBlocks / totalUsedBanks)) *
                       DeviceInfo.wBlockDataSize +
                       (u64)page * DeviceInfo.wPageDataSize;

                       iowrite32(ioread32(FlashReg + intr_status),
                               FlashReg + intr_status);

                       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                               (flash_add >>
                               DeviceInfo.nBitsInPageDataSize)),
                               FlashMem);

                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               for (j = 0;
                                    j <
                                    DeviceInfo.wPageDataSize / eccSectorSize;
                                    j++) {
                                       for (i = 0; i < eccSectorSize; i++)
                                               page_main_spare[(eccSectorSize +
                                                                eccBytes) * j +
                                                               i] =
                                                   write_data[eccSectorSize *
                                                              j + i];

                                       for (i = 0; i < eccBytes; i++)
                                               page_main_spare[(eccSectorSize +
                                                                eccBytes) * j +
                                                               eccSectorSize +
                                                               i] =
                                                   write_data[PageDataSize +
                                                              spareFlagBytes +
                                                              eccBytes * j +
                                                              i];
                               }

                               for (i = 0; i < spareFlagBytes; i++)
                                       page_main_spare[(eccSectorSize +
                                                        eccBytes) * j + i] =
                                           write_data[PageDataSize + i];

                               for (i = PageSize - 1; i >= PageDataSize +
                                                       spareSkipBytes; i--)
                                       page_main_spare[i] = page_main_spare[i -
                                                               spareSkipBytes];

                               for (i = PageDataSize; i < PageDataSize +
                                                       spareSkipBytes; i++)
                                       page_main_spare[i] = 0xff;

                               for (i = 0; i < PageSize / 4; i++)
                                       iowrite32(
                                       *((u32 *)page_main_spare + i),
                                       FlashMem + 0x10);
                       } else {

                               for (i = 0; i < PageSize / 4; i++)
                                       iowrite32(*((u32 *)write_data + i),
                                               FlashMem + 0x10);
                       }

                       while (!(ioread32(FlashReg + intr_status) &
                               (INTR_STATUS0__PROGRAM_COMP |
                               INTR_STATUS0__PROGRAM_FAIL)))
                               ;

                       if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__PROGRAM_FAIL)
                               status = FAIL;

                       iowrite32(ioread32(FlashReg + intr_status),
                                       FlashReg + intr_status);

                       page_num++;
                       page_count--;
                       write_data += PageSize;
               }

               iowrite32(0, FlashReg + TRANSFER_SPARE_REG);
       }

       kfree(page_main_spare);
       return status;
}

u16 NAND_Read_Page_Main_Spare(u8 *read_data, u32 block, u16 page,
                                u16 page_count)
{
       u32 status = PASS;
       u32 i, j;
       u64 flash_add = 0;
       u32 PageSize = DeviceInfo.wPageSize;
       u32 PageDataSize = DeviceInfo.wPageDataSize;
       u32 PageSpareSize = DeviceInfo.wPageSpareSize;
       u32 eccBytes = DeviceInfo.wECCBytesPerSector;
       u32 spareFlagBytes = DeviceInfo.wNumPageSpareFlag;
       u32 spareSkipBytes  = DeviceInfo.wSpareSkipBytes;
       u32 eccSectorSize;
       u32 flash_bank;
       u32 intr_status = 0;
       u8 *read_data_l = read_data;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
       u8 *page_main_spare;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       page_main_spare = kmalloc(DeviceInfo.wPageSize, GFP_ATOMIC);
       if (!page_main_spare) {
               printk(KERN_ERR "Failed to kmalloc memory in %s Line %d, exit.\n",
                       __FILE__, __LINE__);
               return FAIL;
       }

       eccSectorSize = ECC_SECTOR_SIZE * (DeviceInfo.wDevicesConnected);

       status = Boundary_Check_Block_Page(block, page, page_count);

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       if (status == PASS) {
               intr_status = intr_status_addresses[flash_bank];

               iowrite32(1, FlashReg + TRANSFER_SPARE_REG);

               iowrite32(ioread32(FlashReg + intr_status),
                               FlashReg + intr_status);

               while ((status != FAIL) && (page_count > 0)) {
                       flash_add = (u64)(block %
                               (DeviceInfo.wTotalBlocks / totalUsedBanks))
                               * DeviceInfo.wBlockDataSize +
                               (u64)page * DeviceInfo.wPageDataSize;

                       index_addr((u32)(MODE_10 | (flash_bank << 24) |
                               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                               0x43);
                       index_addr((u32)(MODE_10 | (flash_bank << 24) |
                               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
                               0x2000 | page_count);

                       while (!(ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__LOAD_COMP))
                               ;

                       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                               (flash_add >>
                               DeviceInfo.nBitsInPageDataSize)),
                               FlashMem);

                       for (i = 0; i < PageSize / 4; i++)
                               *(((u32 *)page_main_spare) + i) =
                                       ioread32(FlashMem + 0x10);

                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               for (i = PageDataSize;  i < PageSize -
                                                       spareSkipBytes; i++)
                                       page_main_spare[i] = page_main_spare[i +
                                                               spareSkipBytes];

                               for (j = 0;
                               j < DeviceInfo.wPageDataSize / eccSectorSize;
                               j++) {

                                       for (i = 0; i < eccSectorSize; i++)
                                               read_data_l[eccSectorSize * j +
                                                           i] =
                                                   page_main_spare[
                                                       (eccSectorSize +
                                                       eccBytes) * j + i];

                                       for (i = 0; i < eccBytes; i++)
                                               read_data_l[PageDataSize +
                                                           spareFlagBytes +
                                                           eccBytes * j + i] =
                                                   page_main_spare[
                                                       (eccSectorSize +
                                                       eccBytes) * j +
                                                       eccSectorSize + i];
                               }

                               for (i = 0; i < spareFlagBytes; i++)
                                       read_data_l[PageDataSize + i] =
                                           page_main_spare[(eccSectorSize +
                                                            eccBytes) * j + i];
                       } else {
                               for (i = 0; i < (PageDataSize + PageSpareSize);
                                    i++)
                                       read_data_l[i] = page_main_spare[i];

                       }

                       if (ioread32(FlashReg + ECC_ENABLE)) {
                               while (!(ioread32(FlashReg + intr_status) &
                                       (INTR_STATUS0__ECC_TRANSACTION_DONE |
                                       INTR_STATUS0__ECC_ERR)))
                                       ;

                               if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                                       status = do_ecc_new(flash_bank,
                                               read_data, block, page);
                               }

                               if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_TRANSACTION_DONE &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR |
                                       INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_TRANSACTION_DONE) {
                                       iowrite32(
                                       INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);
                               } else if (ioread32(FlashReg + intr_status) &
                                       INTR_STATUS0__ECC_ERR) {
                                       iowrite32(INTR_STATUS0__ECC_ERR,
                                               FlashReg + intr_status);
                               }
                       }

                       page++;
                       page_count--;
                       read_data_l += PageSize;
               }
       }

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       kfree(page_main_spare);
       return status;
}

u16 NAND_Pipeline_Write_Ahead(u8 *write_data, u32 block,
                       u16 page, u16 page_count)
{
       u16 status = PASS;
       u32 NumPages = page_count;
       u64 flash_add;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
#if DDMA
       int ret;
#else
       u32 PageSize = DeviceInfo.wPageDataSize;
       u32 i, page_num = 0;
#endif

       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);

       if (page_count < 2)
               status = FAIL;

       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       intr_status = intr_status_addresses[flash_bank];
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

#if DDMA
       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       /* Fill the mrst_nand_info structure */
       info.state = INT_PIPELINE_WRITE_AHEAD;
       info.write_data = write_data;
       info.flash_bank = flash_bank;
       info.block = block;
       info.page = page;
       info.ret = PASS;

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       ddma_trans(write_data, flash_add, flash_bank, 1, NumPages);

       iowrite32(1, FlashReg + GLOBAL_INT_ENABLE); /* Enable interrupt */

       ret = wait_for_completion_timeout(&info.complete, 10 * HZ);
       if (!ret)
               printk(KERN_ERR "Wait for completion timeout "
                       "in %s, Line %d\n", __FILE__, __LINE__);

       status = info.ret;

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);
       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

#else

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x2100 | NumPages);

       while (NumPages > 0) {
               iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                       ((flash_add + page_num * DeviceInfo.wPageDataSize) >>
                       DeviceInfo.nBitsInPageDataSize)), FlashMem);

               for (i = 0; i < DeviceInfo.wPageDataSize / 4; i++)
                       iowrite32(*((u32 *)write_data + i), FlashMem + 0x10);

               while (!(ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__INT_ACT))
                       ;

               iowrite32(INTR_STATUS0__INT_ACT, FlashReg + intr_status);

               write_data += PageSize;
               --NumPages;
               page_num++;
       }

       while (!(ioread32(FlashReg + intr_status) &
               (INTR_STATUS0__PROGRAM_COMP | INTR_STATUS0__PROGRAM_FAIL)))
               ;

       if (ioread32(FlashReg + intr_status) & INTR_STATUS0__PROGRAM_FAIL)
               status = FAIL;

       iowrite32(INTR_STATUS0__PROGRAM_COMP | INTR_STATUS0__PROGRAM_FAIL,
               FlashReg + intr_status);

#endif

       return status;
}

u16 NAND_Multiplane_Write(u8 *write_data, u32 block, u16 page,
                            u16 page_count)
{
       u16 status = PASS;
       u32 NumPages = page_count;
       u64 flash_add;
       u32 flash_bank;
       u32 intr_status = 0;
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
#if DDMA
       u16 status2 = PASS;
       u32 t;
#else
       u32 PageSize = DeviceInfo.wPageDataSize;
       u32 i, page_num = 0;
       u32 plane = 0;
#endif

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       status = Boundary_Check_Block_Page(block, page, page_count);
       if (status != PASS)
               return status;

       flash_add = (u64)(block % (DeviceInfo.wTotalBlocks / totalUsedBanks))
               * DeviceInfo.wBlockDataSize +
               (u64)page * DeviceInfo.wPageDataSize;

       flash_bank = block / (DeviceInfo.wTotalBlocks / totalUsedBanks);

       intr_status = intr_status_addresses[flash_bank];
       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);
       iowrite32(0x01, FlashReg + MULTIPLANE_OPERATION);

#if DDMA

       iowrite32(1, FlashReg + DMA_ENABLE);
       while (!(ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)), 0x42);

       ddma_trans(write_data, flash_add, flash_bank, 1, NumPages);

       while (1) {
               while (!ioread32(FlashReg + intr_status))
                       ;

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__DMA_CMD_COMP) {
                       iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                               FlashReg + intr_status);
                       status = PASS;
                       if (status2 == FAIL)
                               status = FAIL;
                       break;
               } else if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__PROGRAM_FAIL) {
                       status2 = FAIL;
                       status = FAIL;
                       t = ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__PROGRAM_FAIL;
                       iowrite32(t, FlashReg + intr_status);
               } else {
                       iowrite32((~INTR_STATUS0__PROGRAM_FAIL) &
                               (~INTR_STATUS0__DMA_CMD_COMP),
                               FlashReg + intr_status);
               }
       }

       iowrite32(ioread32(FlashReg + intr_status), FlashReg + intr_status);

       iowrite32(0, FlashReg + DMA_ENABLE);

       while ((ioread32(FlashReg + DMA_ENABLE) & DMA_ENABLE__FLAG))
               ;

       iowrite32(0, FlashReg + MULTIPLANE_OPERATION);

#else
       iowrite32(0, FlashReg + TRANSFER_SPARE_REG);

       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x42);
       index_addr((u32)(MODE_10 | (flash_bank << 24) |
               (flash_add >> DeviceInfo.nBitsInPageDataSize)),
               0x2100 | NumPages);

       while (NumPages > 0) {
               if (0 == plane) {
                       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                               ((flash_add +
                               page_num * DeviceInfo.wPageDataSize) >>
                               DeviceInfo.nBitsInPageDataSize)),
                               FlashMem);
                       plane = 1;
               } else {
                       iowrite32((u32)(MODE_01 | (flash_bank << 24) |
                               ((flash_add + DeviceInfo.wBlockDataSize +
                               page_num * DeviceInfo.wPageDataSize) >>
                               DeviceInfo.nBitsInPageDataSize)),
                               FlashMem);
                       plane = 0;
               }

               for (i = 0; i < DeviceInfo.wPageDataSize / 4; i++)
                       iowrite32(*((u32 *)write_data + i),
                               FlashMem + 0x10);

               write_data += PageSize;

               if (0 == plane)
                       page_num++;

               --NumPages;
       }

       while (!(ioread32(FlashReg + intr_status) &
               (INTR_STATUS0__PROGRAM_COMP |
               INTR_STATUS0__PROGRAM_FAIL)))
               ;

       if (ioread32(FlashReg + intr_status) & INTR_STATUS0__PROGRAM_FAIL)
               status = FAIL;

       iowrite32(INTR_STATUS0__PROGRAM_COMP | INTR_STATUS0__PROGRAM_FAIL,
               FlashReg + intr_status);

       iowrite32(0, FlashReg + MULTIPLANE_OPERATION);
#endif

       return status;
}

u16 NAND_LLD_Event_Status(void)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       return PASS;
}

static void handle_nand_int_read(struct mrst_nand_info *dev)
{
       u32 intr_status_addresses[4] = {INTR_STATUS0,
               INTR_STATUS1, INTR_STATUS2, INTR_STATUS3};
       u32 intr_status;
       u32 ecc_done_OR_dma_comp = 0;

       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       dev->ret = PASS;
       intr_status = intr_status_addresses[dev->flash_bank];

       while (1) {
               if (ioread32(FlashReg + ECC_ENABLE)) {
                       if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__ECC_ERR) {
                               iowrite32(INTR_STATUS0__ECC_ERR,
                                       FlashReg + intr_status);
                               dev->ret = do_ecc_new(dev->flash_bank,
                                               dev->read_data,
                                               dev->block, dev->page);
                       } else if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__DMA_CMD_COMP) {
                               iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                       FlashReg + intr_status);
                               if (1 == ecc_done_OR_dma_comp)
                                       break;
                               ecc_done_OR_dma_comp = 1;
                       } else if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__ECC_TRANSACTION_DONE) {
                               iowrite32(INTR_STATUS0__ECC_TRANSACTION_DONE,
                                       FlashReg + intr_status);
                               if (1 == ecc_done_OR_dma_comp)
                                       break;
                               ecc_done_OR_dma_comp = 1;
                       }
               } else {
                       if (ioread32(FlashReg + intr_status) &
                               INTR_STATUS0__DMA_CMD_COMP) {
                               iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                                       FlashReg + intr_status);
                               break;
                       } else {
                               printk(KERN_ERR "Illegal INTS "
                                       "(offset addr 0x%x) value: 0x%x\n",
                                       intr_status,
                                       ioread32(FlashReg + intr_status));
                       }
               }

               iowrite32((~INTR_STATUS0__ECC_ERR) &
               (~INTR_STATUS0__ECC_TRANSACTION_DONE) &
               (~INTR_STATUS0__DMA_CMD_COMP),
               FlashReg + intr_status);
       }
}

static void handle_nand_int_write(struct mrst_nand_info *dev)
{
       u32 intr_status;
       u32 intr[4] = {INTR_STATUS0, INTR_STATUS1,
               INTR_STATUS2, INTR_STATUS3};
       int status = PASS;

       nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       dev->ret = PASS;
       intr_status = intr[dev->flash_bank];

       while (1) {
               while (!ioread32(FlashReg + intr_status))
                       ;

               if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__DMA_CMD_COMP) {
                       iowrite32(INTR_STATUS0__DMA_CMD_COMP,
                               FlashReg + intr_status);
                       if (FAIL == status)
                               dev->ret = FAIL;
                       break;
               } else if (ioread32(FlashReg + intr_status) &
                       INTR_STATUS0__PROGRAM_FAIL) {
                       status = FAIL;
                       iowrite32(INTR_STATUS0__PROGRAM_FAIL,
                               FlashReg + intr_status);
               } else {
                       iowrite32((~INTR_STATUS0__PROGRAM_FAIL) &
                               (~INTR_STATUS0__DMA_CMD_COMP),
                               FlashReg + intr_status);
               }
       }
}

static irqreturn_t ddma_isr(int irq, void *dev_id)
{
       struct mrst_nand_info *dev = dev_id;
       u32 int_mask, ints0, ints1, ints2, ints3, ints_offset;
       u32 intr[4] = {INTR_STATUS0, INTR_STATUS1,
               INTR_STATUS2, INTR_STATUS3};

       int_mask = INTR_STATUS0__DMA_CMD_COMP |
               INTR_STATUS0__ECC_TRANSACTION_DONE |
               INTR_STATUS0__ECC_ERR |
               INTR_STATUS0__PROGRAM_FAIL |
               INTR_STATUS0__ERASE_FAIL;

       ints0 = ioread32(FlashReg + INTR_STATUS0);
       ints1 = ioread32(FlashReg + INTR_STATUS1);
       ints2 = ioread32(FlashReg + INTR_STATUS2);
       ints3 = ioread32(FlashReg + INTR_STATUS3);

       ints_offset = intr[dev->flash_bank];

       nand_dbg_print(NAND_DBG_DEBUG,
               "INTR0: 0x%x, INTR1: 0x%x, INTR2: 0x%x, INTR3: 0x%x, "
               "DMA_INTR: 0x%x, "
               "dev->state: 0x%x, dev->flash_bank: %d\n",
               ints0, ints1, ints2, ints3,
               ioread32(FlashReg + DMA_INTR),
               dev->state, dev->flash_bank);

       if (!(ioread32(FlashReg + ints_offset) & int_mask)) {
               iowrite32(ints0, FlashReg + INTR_STATUS0);
               iowrite32(ints1, FlashReg + INTR_STATUS1);
               iowrite32(ints2, FlashReg + INTR_STATUS2);
               iowrite32(ints3, FlashReg + INTR_STATUS3);
               nand_dbg_print(NAND_DBG_WARN,
                       "ddma_isr: Invalid interrupt for NAND controller. "
                       "Ignore it\n");
               return IRQ_NONE;
       }

       switch (dev->state) {
       case INT_READ_PAGE_MAIN:
       case INT_PIPELINE_READ_AHEAD:
               /* Disable controller interrupts */
               iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);
               handle_nand_int_read(dev);
               break;
       case INT_WRITE_PAGE_MAIN:
       case INT_PIPELINE_WRITE_AHEAD:
               iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);
               handle_nand_int_write(dev);
               break;
       default:
               printk(KERN_ERR "ddma_isr - Illegal state: 0x%x\n",
                       dev->state);
               return IRQ_NONE;
       }

       dev->state = INT_IDLE_STATE;
       complete(&dev->complete);
       return IRQ_HANDLED;
}

static const struct pci_device_id nand_pci_ids[] = {
       {
        .vendor = 0x8086,
        .device = 0x0809,
        .subvendor = PCI_ANY_ID,
        .subdevice = PCI_ANY_ID,
        },
       { /* end: all zeroes */ }
};

static int dump_pci_config_register(struct pci_dev *dev)
{
       int err = 0;
       unsigned int data32;
       u16 data16;
       u8 data8;

       nand_dbg_print(NAND_DBG_DEBUG, "Dump MRST PCI Config Registers:\n");

       err = pci_read_config_word(dev, PCI_VENDOR_ID, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_VENDOR_ID fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_VENDOR_ID: 0x%x\n", data16);
       }

       err = pci_read_config_word(dev, PCI_DEVICE_ID, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_DEVICE_ID fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_DEVICE_ID: 0x%x\n", data16);
       }

       err = pci_read_config_word(dev, PCI_COMMAND, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_COMMAND fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_COMMAND: 0x%x\n", data16);
       }

       err = pci_read_config_word(dev, PCI_STATUS, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_STATUS fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_STATUS: 0x%x\n", data16);
       }

       err = pci_read_config_byte(dev, PCI_CLASS_REVISION, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_CLASS_REVISION fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_CLASS_REVISION: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_CLASS_PROG, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_CLASS_PROG fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_CLASS_PROG: 0x%x\n", data8);
       }

       err = pci_read_config_word(dev, PCI_CLASS_DEVICE, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_CLASS_DEVICE fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_CLASS_DEVICE: 0x%x\n",
                              data16);
       }

       err = pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_CACHE_LINE_SIZE fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_CACHE_LINE_SIZE: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_LATENCY_TIMER, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_LATENCY_TIMER fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_LATENCY_TIMER: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_HEADER_TYPE, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_HEADER_TYPE fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_HEADER_TYPE: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_BIST, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_BIST fail, " "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BIST: 0x%x\n", data8);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_0 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_0: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_1, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_1 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_1: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_2, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_2 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_2: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_3, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_3 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_3: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_4, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_4 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_4: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_BASE_ADDRESS_5, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_BASE_ADDRESS_5 fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_BASE_ADDRESS_5: 0x%x\n",
                              data32);
       }

       err = pci_read_config_dword(dev, PCI_CARDBUS_CIS, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_CARDBUS_CIS fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_CARDBUS_CIS: 0x%x\n",
                              data32);
       }

       err = pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_SUBSYSTEM_VENDOR_ID fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG,
                              "PCI_SUBSYSTEM_VENDOR_ID: 0x%x\n", data16);
       }

       err = pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &data16);
       if (err) {
               printk(KERN_ERR "Read PCI_SUBSYSTEM_ID fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_SUBSYSTEM_ID: 0x%x\n",
                              data16);
       }

       err = pci_read_config_dword(dev, PCI_ROM_ADDRESS, &data32);
       if (err) {
               printk(KERN_ERR "Read PCI_ROM_ADDRESS fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_ROM_ADDRESS: 0x%x\n",
                              data32);
       }

       err = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_INTERRUPT_LINE fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_INTERRUPT_LINE: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_INTERRUPT_PIN fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_INTERRUPT_PIN: 0x%x\n",
                              data8);
       }

       err = pci_read_config_byte(dev, PCI_MIN_GNT, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_MIN_GNT fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_MIN_GNT: 0x%x\n", data8);
       }

       err = pci_read_config_byte(dev, PCI_MAX_LAT, &data8);
       if (err) {
               printk(KERN_ERR "Read PCI_MAX_LAT fail, "
                      "error code: %d\n", err);
               return err;
       } else {
               nand_dbg_print(NAND_DBG_DEBUG, "PCI_MAX_LAT: 0x%x\n", data8);
       }

       return err;
}

static int nand_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
       int ret = -ENODEV;
       unsigned long csr_base;
       unsigned long csr_len;
       struct mrst_nand_info *pndev = &info;

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       ret = pci_enable_device(dev);
       if (ret) {
               printk(KERN_ERR "Spectra: pci_enable_device failed.\n");
               return ret;
       }

       dump_pci_config_register(dev);

       pci_set_master(dev);
       pndev->dev = dev;

       csr_base = pci_resource_start(dev, 0);
       if (!csr_base) {
               printk(KERN_ERR "Spectra: pci_resource_start failed!\n");
               return -ENODEV;
       }

       csr_len = pci_resource_len(dev, 0);
       if (!csr_len) {
               printk(KERN_ERR "Spectra: pci_resource_len failed!\n");
               return -ENODEV;
       }

       ret = pci_request_regions(dev, SPECTRA_NAND_NAME);
       if (ret) {
               printk(KERN_ERR "Spectra: Unable to request "
                      "memory region\n");
               goto failed_req_csr;
       }

       pndev->ioaddr = ioremap_nocache(csr_base, csr_len);
       if (!pndev->ioaddr) {
               printk(KERN_ERR "Spectra: Unable to remap memory region\n");
               ret = -ENOMEM;
               goto failed_remap_csr;
       }
       nand_dbg_print(NAND_DBG_DEBUG, "Spectra: CSR 0x%08lx -> 0x%p (0x%lx)\n",
                      csr_base, pndev->ioaddr, csr_len);

#if DDMA
       init_completion(&pndev->complete);
       nand_dbg_print(NAND_DBG_DEBUG, "Spectra: IRQ %d\n", dev->irq);
       if (request_irq(dev->irq, ddma_isr, IRQF_SHARED,
                       SPECTRA_NAND_NAME, &info)) {
               printk(KERN_ERR "Spectra: Unable to allocate IRQ\n");
               ret = -ENODEV;
               iounmap(pndev->ioaddr);
               goto failed_remap_csr;
       }
#endif

       pci_set_drvdata(dev, pndev);

       return 0;

failed_remap_csr:
       pci_release_regions(dev);
failed_req_csr:

       return ret;
}

static void nand_pci_remove(struct pci_dev *dev)
{
       struct mrst_nand_info *pndev = pci_get_drvdata(dev);

       nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);
#if CMD_DMA
       free_irq(dev->irq, pndev);
#endif
       iounmap(pndev->ioaddr);
       pci_release_regions(dev);
       pci_disable_device(dev);
}

MODULE_DEVICE_TABLE(pci, nand_pci_ids);

static struct pci_driver nand_pci_driver = {
       .name = SPECTRA_NAND_NAME,
       .id_table = nand_pci_ids,
       .probe = nand_pci_probe,
       .remove = nand_pci_remove,
};

u16 NAND_Flash_Init(void)
{
       int retval;
       u32 int_mask = INTR_STATUS0__DMA_CMD_COMP |
               INTR_STATUS0__ECC_TRANSACTION_DONE |
               INTR_STATUS0__ECC_ERR |
               INTR_STATUS0__PROGRAM_FAIL |
               INTR_STATUS0__ERASE_FAIL;

       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       FlashReg = GLOB_MEMMAP_NOCACHE(GLOB_HWCTL_REG_BASE,
                       GLOB_HWCTL_REG_SIZE);
       if (!FlashReg) {
               printk(KERN_ERR "Spectra: ioremap_nocache failed!");
               return -ENOMEM;
       }
       nand_dbg_print(NAND_DBG_WARN,
               "Spectra: Remapped reg base address: "
               "0x%p, len: %d\n",
               FlashReg, GLOB_HWCTL_REG_SIZE);

       FlashMem = GLOB_MEMMAP_NOCACHE(GLOB_HWCTL_MEM_BASE,
                       GLOB_HWCTL_MEM_SIZE);
       if (!FlashMem) {
               printk(KERN_ERR "Spectra: ioremap_nocache failed!");
               return -ENOMEM;
       }

       nand_dbg_print(NAND_DBG_WARN,
               "Spectra: Remapped flash base address: "
               "0x%p, len: %d\n",
               (void *)FlashMem, GLOB_HWCTL_MEM_SIZE);

       NAND_Flash_Reset();

       iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);
/*
       iowrite32(0, FlashReg + INTR_EN0);
       iowrite32(0, FlashReg + INTR_EN1);
       iowrite32(0, FlashReg + INTR_EN2);
       iowrite32(0, FlashReg + INTR_EN3);
*/

       iowrite32(int_mask, FlashReg + INTR_EN0);
       iowrite32(int_mask, FlashReg + INTR_EN1);
       iowrite32(int_mask, FlashReg + INTR_EN2);
       iowrite32(int_mask, FlashReg + INTR_EN3);

       iowrite32(0xFFFF, FlashReg + INTR_STATUS0);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS1);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS2);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS3);

       iowrite32(0x0F, FlashReg + RB_PIN_ENABLED);
       iowrite32(CHIP_EN_DONT_CARE__FLAG, FlashReg + CHIP_ENABLE_DONT_CARE);

       /* Should set value for these registers when init */
       iowrite32(1, FlashReg + ECC_ENABLE);
       iowrite32(0, FlashReg + TWO_ROW_ADDR_CYCLES);

       /* Enable the 2 lines code will enable pipeline_rw_ahead feature */
       /* and improve performance for about 10%. But will also cause a */
       /* 1 or 2 bit error when do a 300MB+ file copy/compare testing. */
       /* Suspect it's an ECC FIFO overflow issue. -- Yunpeng 2009.03.26 */
       /* iowrite32(1, FlashReg + CACHE_WRITE_ENABLE); */
       /* iowrite32(1, FlashReg + CACHE_READ_ENABLE); */

       retval = pci_register_driver(&nand_pci_driver);
       if (retval)
               return -ENOMEM;

       return PASS;
}

#endif

#if FLASH_CDMA
u16 NAND_Flash_Init(void)
{
       nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
                      __FILE__, __LINE__, __func__);

       FlashReg = GLOB_MEMMAP_NOCACHE(GLOB_HWCTL_REG_BASE,
                       GLOB_HWCTL_REG_SIZE);
       if (!FlashReg) {
               printk(KERN_ERR "Spectra: ioremap_nocache failed!");
               return -ENOMEM;
       }
       nand_dbg_print(NAND_DBG_WARN,
               "Spectra: Remapped reg base address: "
               "0x%p, len: %d\n",
               FlashReg, GLOB_HWCTL_REG_SIZE);

       FlashMem = GLOB_MEMMAP_NOCACHE(GLOB_HWCTL_MEM_BASE,
                       GLOB_HWCTL_MEM_SIZE);
       if (!FlashMem) {
               printk(KERN_ERR "Spectra: ioremap_nocache failed!");
               return -ENOMEM;
       }

       nand_dbg_print(NAND_DBG_WARN,
               "Spectra: Remapped flash base address: "
               "0x%p, len: %d\n",
               (void *)FlashMem, GLOB_HWCTL_MEM_SIZE);

       NAND_Flash_Reset();

       iowrite32(0, FlashReg + GLOBAL_INT_ENABLE);

       iowrite32(0, FlashReg + INTR_EN0);
       iowrite32(0, FlashReg + INTR_EN1);
       iowrite32(0, FlashReg + INTR_EN2);
       iowrite32(0, FlashReg + INTR_EN3);

       iowrite32(0xFFFF, FlashReg + INTR_STATUS0);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS1);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS2);
       iowrite32(0xFFFF, FlashReg + INTR_STATUS3);

       iowrite32(0x0F, FlashReg + RB_PIN_ENABLED);
       iowrite32(CHIP_EN_DONT_CARE__FLAG, FlashReg + CHIP_ENABLE_DONT_CARE);

       iowrite32(1, FlashReg + ECC_ENABLE);
       iowrite32(0, FlashReg + TWO_ROW_ADDR_CYCLES);

       /* Enable the 2 lines code will enable pipeline_rw_ahead feature */
       /* and improve performance for about 10%. But will also cause a */
       /* 1 or 2 bit error when do a 300MB+ file copy/compare testing. */
       /* Suspect it's an ECC FIFO overflow issue. -- Yunpeng 2009.03.26 */
       /* iowrite32(1, FlashReg + CACHE_WRITE_ENABLE); */
       /* iowrite32(1, FlashReg + CACHE_READ_ENABLE); */

       return PASS;
}

#endif

