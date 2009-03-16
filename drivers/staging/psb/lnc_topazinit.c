/**
 * file lnc_topazinit.c
 * TOPAZ initialization and mtx-firmware upload
 *
 */

/**************************************************************************
 *
 * Copyright (c) 2008 Intel Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/* include headers */

#include <linux/firmware.h>

#include "drmP.h"
#include "drm.h"

/* #define DRM_DEBUG_CODE 2 */

#include "psb_drv.h"
#include "lnc_topaz.h"

/* register default values
 * THIS HEADER IS ONLY INCLUDE ONCE*/
static unsigned long topaz_default_regs[183][3] = {
	{MVEA_START, 0x00000000, 0x00000000},
	{MVEA_START, 0x00000004, 0x00000400},
	{MVEA_START, 0x00000008, 0x00000000},
	{MVEA_START, 0x0000000C, 0x00000000},
	{MVEA_START, 0x00000010, 0x00000000},
	{MVEA_START, 0x00000014, 0x00000000},
	{MVEA_START, 0x00000018, 0x00000000},
	{MVEA_START, 0x0000001C, 0x00000000},
	{MVEA_START, 0x00000020, 0x00000120},
	{MVEA_START, 0x00000024, 0x00000000},
	{MVEA_START, 0x00000028, 0x00000000},
	{MVEA_START, 0x00000100, 0x00000000},
	{MVEA_START, 0x00000104, 0x00000000},
	{MVEA_START, 0x00000108, 0x00000000},
	{MVEA_START, 0x0000010C, 0x00000000},
	{MVEA_START, 0x0000011C, 0x00000001},
	{MVEA_START, 0x0000012C, 0x00000000},
	{MVEA_START, 0x00000180, 0x00000000},
	{MVEA_START, 0x00000184, 0x00000000},
	{MVEA_START, 0x00000188, 0x00000000},
	{MVEA_START, 0x0000018C, 0x00000000},
	{MVEA_START, 0x00000190, 0x00000000},
	{MVEA_START, 0x00000194, 0x00000000},
	{MVEA_START, 0x00000198, 0x00000000},
	{MVEA_START, 0x0000019C, 0x00000000},
	{MVEA_START, 0x000001A0, 0x00000000},
	{MVEA_START, 0x000001A4, 0x00000000},
	{MVEA_START, 0x000001A8, 0x00000000},
	{MVEA_START, 0x000001AC, 0x00000000},
	{MVEA_START, 0x000001B0, 0x00000000},
	{MVEA_START, 0x000001B4, 0x00000000},
	{MVEA_START, 0x000001B8, 0x00000000},
	{MVEA_START, 0x000001BC, 0x00000000},
	{MVEA_START, 0x000001F8, 0x00000000},
	{MVEA_START, 0x000001FC, 0x00000000},
	{MVEA_START, 0x00000200, 0x00000000},
	{MVEA_START, 0x00000204, 0x00000000},
	{MVEA_START, 0x00000208, 0x00000000},
	{MVEA_START, 0x0000020C, 0x00000000},
	{MVEA_START, 0x00000210, 0x00000000},
	{MVEA_START, 0x00000220, 0x00000001},
	{MVEA_START, 0x00000224, 0x0000001F},
	{MVEA_START, 0x00000228, 0x00000100},
	{MVEA_START, 0x0000022C, 0x00001F00},
	{MVEA_START, 0x00000230, 0x00000101},
	{MVEA_START, 0x00000234, 0x00001F1F},
	{MVEA_START, 0x00000238, 0x00001F01},
	{MVEA_START, 0x0000023C, 0x0000011F},
	{MVEA_START, 0x00000240, 0x00000200},
	{MVEA_START, 0x00000244, 0x00001E00},
	{MVEA_START, 0x00000248, 0x00000002},
	{MVEA_START, 0x0000024C, 0x0000001E},
	{MVEA_START, 0x00000250, 0x00000003},
	{MVEA_START, 0x00000254, 0x0000001D},
	{MVEA_START, 0x00000258, 0x00001F02},
	{MVEA_START, 0x0000025C, 0x00000102},
	{MVEA_START, 0x00000260, 0x0000011E},
	{MVEA_START, 0x00000264, 0x00000000},
	{MVEA_START, 0x00000268, 0x00000000},
	{MVEA_START, 0x0000026C, 0x00000000},
	{MVEA_START, 0x00000270, 0x00000000},
	{MVEA_START, 0x00000274, 0x00000000},
	{MVEA_START, 0x00000278, 0x00000000},
	{MVEA_START, 0x00000280, 0x00008000},
	{MVEA_START, 0x00000284, 0x00000000},
	{MVEA_START, 0x00000288, 0x00000000},
	{MVEA_START, 0x0000028C, 0x00000000},
	{MVEA_START, 0x00000314, 0x00000000},
	{MVEA_START, 0x00000318, 0x00000000},
	{MVEA_START, 0x0000031C, 0x00000000},
	{MVEA_START, 0x00000320, 0x00000000},
	{MVEA_START, 0x00000324, 0x00000000},
	{MVEA_START, 0x00000348, 0x00000000},
	{MVEA_START, 0x00000380, 0x00000000},
	{MVEA_START, 0x00000384, 0x00000000},
	{MVEA_START, 0x00000388, 0x00000000},
	{MVEA_START, 0x0000038C, 0x00000000},
	{MVEA_START, 0x00000390, 0x00000000},
	{MVEA_START, 0x00000394, 0x00000000},
	{MVEA_START, 0x00000398, 0x00000000},
	{MVEA_START, 0x0000039C, 0x00000000},
	{MVEA_START, 0x000003A0, 0x00000000},
	{MVEA_START, 0x000003A4, 0x00000000},
	{MVEA_START, 0x000003A8, 0x00000000},
	{MVEA_START, 0x000003B0, 0x00000000},
	{MVEA_START, 0x000003B4, 0x00000000},
	{MVEA_START, 0x000003B8, 0x00000000},
	{MVEA_START, 0x000003BC, 0x00000000},
	{MVEA_START, 0x000003D4, 0x00000000},
	{MVEA_START, 0x000003D8, 0x00000000},
	{MVEA_START, 0x000003DC, 0x00000000},
	{MVEA_START, 0x000003E0, 0x00000000},
	{MVEA_START, 0x000003E4, 0x00000000},
	{MVEA_START, 0x000003EC, 0x00000000},
	{MVEA_START, 0x000002D0, 0x00000000},
	{MVEA_START, 0x000002D4, 0x00000000},
	{MVEA_START, 0x000002D8, 0x00000000},
	{MVEA_START, 0x000002DC, 0x00000000},
	{MVEA_START, 0x000002E0, 0x00000000},
	{MVEA_START, 0x000002E4, 0x00000000},
	{MVEA_START, 0x000002E8, 0x00000000},
	{MVEA_START, 0x000002EC, 0x00000000},
	{MVEA_START, 0x000002F0, 0x00000000},
	{MVEA_START, 0x000002F4, 0x00000000},
	{MVEA_START, 0x000002F8, 0x00000000},
	{MVEA_START, 0x000002FC, 0x00000000},
	{MVEA_START, 0x00000300, 0x00000000},
	{MVEA_START, 0x00000304, 0x00000000},
	{MVEA_START, 0x00000308, 0x00000000},
	{MVEA_START, 0x0000030C, 0x00000000},
	{MVEA_START, 0x00000290, 0x00000000},
	{MVEA_START, 0x00000294, 0x00000000},
	{MVEA_START, 0x00000298, 0x00000000},
	{MVEA_START, 0x0000029C, 0x00000000},
	{MVEA_START, 0x000002A0, 0x00000000},
	{MVEA_START, 0x000002A4, 0x00000000},
	{MVEA_START, 0x000002A8, 0x00000000},
	{MVEA_START, 0x000002AC, 0x00000000},
	{MVEA_START, 0x000002B0, 0x00000000},
	{MVEA_START, 0x000002B4, 0x00000000},
	{MVEA_START, 0x000002B8, 0x00000000},
	{MVEA_START, 0x000002BC, 0x00000000},
	{MVEA_START, 0x000002C0, 0x00000000},
	{MVEA_START, 0x000002C4, 0x00000000},
	{MVEA_START, 0x000002C8, 0x00000000},
	{MVEA_START, 0x000002CC, 0x00000000},
	{MVEA_START, 0x00000080, 0x00000000},
	{MVEA_START, 0x00000084, 0x80705700},
	{MVEA_START, 0x00000088, 0x00000000},
	{MVEA_START, 0x0000008C, 0x00000000},
	{MVEA_START, 0x00000090, 0x00000000},
	{MVEA_START, 0x00000094, 0x00000000},
	{MVEA_START, 0x00000098, 0x00000000},
	{MVEA_START, 0x0000009C, 0x00000000},
	{MVEA_START, 0x000000A0, 0x00000000},
	{MVEA_START, 0x000000A4, 0x00000000},
	{MVEA_START, 0x000000A8, 0x00000000},
	{MVEA_START, 0x000000AC, 0x00000000},
	{MVEA_START, 0x000000B0, 0x00000000},
	{MVEA_START, 0x000000B4, 0x00000000},
	{MVEA_START, 0x000000B8, 0x00000000},
	{MVEA_START, 0x000000BC, 0x00000000},
	{MVEA_START, 0x000000C0, 0x00000000},
	{MVEA_START, 0x000000C4, 0x00000000},
	{MVEA_START, 0x000000C8, 0x00000000},
	{MVEA_START, 0x000000CC, 0x00000000},
	{MVEA_START, 0x000000D0, 0x00000000},
	{MVEA_START, 0x000000D4, 0x00000000},
	{MVEA_START, 0x000000D8, 0x00000000},
	{MVEA_START, 0x000000DC, 0x00000000},
	{MVEA_START, 0x000000E0, 0x00000000},
	{MVEA_START, 0x000000E4, 0x00000000},
	{MVEA_START, 0x000000E8, 0x00000000},
	{MVEA_START, 0x000000EC, 0x00000000},
	{MVEA_START, 0x000000F0, 0x00000000},
	{MVEA_START, 0x000000F4, 0x00000000},
	{MVEA_START, 0x000000F8, 0x00000000},
	{MVEA_START, 0x000000FC, 0x00000000},
	{TOPAZ_VLC_START, 0x00000000, 0x00000000},
	{TOPAZ_VLC_START, 0x00000004, 0x00000000},
	{TOPAZ_VLC_START, 0x00000008, 0x00000000},
	{TOPAZ_VLC_START, 0x0000000C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000010, 0x00000000},
	{TOPAZ_VLC_START, 0x00000014, 0x00000000},
	{TOPAZ_VLC_START, 0x0000001C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000020, 0x00000000},
	{TOPAZ_VLC_START, 0x00000024, 0x00000000},
	{TOPAZ_VLC_START, 0x0000002C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000034, 0x00000000},
	{TOPAZ_VLC_START, 0x00000038, 0x00000000},
	{TOPAZ_VLC_START, 0x0000003C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000040, 0x00000000},
	{TOPAZ_VLC_START, 0x00000044, 0x00000000},
	{TOPAZ_VLC_START, 0x00000048, 0x00000000},
	{TOPAZ_VLC_START, 0x0000004C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000050, 0x00000000},
	{TOPAZ_VLC_START, 0x00000054, 0x00000000},
	{TOPAZ_VLC_START, 0x00000058, 0x00000000},
	{TOPAZ_VLC_START, 0x0000005C, 0x00000000},
	{TOPAZ_VLC_START, 0x00000060, 0x00000000},
	{TOPAZ_VLC_START, 0x00000064, 0x00000000},
	{TOPAZ_VLC_START, 0x00000068, 0x00000000},
	{TOPAZ_VLC_START, 0x0000006C, 0x00000000}
};

#define FIRMWARE_NAME "topaz_fw.bin"

/* define structure */
/* firmware file's info head */
struct topaz_fwinfo {
	unsigned int ver:16;
	unsigned int codec:16;

	unsigned int text_size;
	unsigned int data_size;
	unsigned int data_location;
};

/* firmware data array define  */
struct topaz_codec_fw {
	uint32_t ver;
	uint32_t codec;

	uint32_t text_size;
	uint32_t data_size;
	uint32_t data_location;

	struct ttm_buffer_object *text;
	struct ttm_buffer_object *data;
};



/* static function define */
static int topaz_upload_fw(struct drm_device *dev,
			enum drm_lnc_topaz_codec codec);
static inline void topaz_set_default_regs(struct drm_psb_private
					  *dev_priv);

#define UPLOAD_FW_BY_DMA 0

#if UPLOAD_FW_BY_DMA
static void topaz_dma_transfer(struct drm_psb_private *dev_priv,
			       uint32_t channel, uint32_t src_phy_addr,
			       uint32_t offset, uint32_t dst_addr,
			       uint32_t byte_num, uint32_t is_increment,
			       uint32_t is_write);
#else
static void topaz_mtx_upload_by_register(struct drm_device *dev,
					 uint32_t mtx_mem, uint32_t addr,
					 uint32_t size,
					 struct ttm_buffer_object *buf);
#endif

static void topaz_write_core_reg(struct drm_psb_private *dev_priv,
				 uint32_t reg, const uint32_t val);
static void topaz_read_core_reg(struct drm_psb_private *dev_priv,
				uint32_t reg, uint32_t *ret_val);
static void get_mtx_control_from_dash(struct drm_psb_private *dev_priv);
static void release_mtx_control_from_dash(struct drm_psb_private
					  *dev_priv);
static void topaz_mmu_hwsetup(struct drm_psb_private *dev_priv);

#ifdef DEBUG_FUNCTION
static int topaz_test_null(struct drm_device *dev, uint32_t seq);
static void topaz_mmu_flush(struct drm_device *dev);
static void topaz_mmu_test(struct drm_device *dev, uint32_t sync_value);
#endif

/* globale variable define */
struct topaz_codec_fw topaz_fw[IMG_CODEC_NUM];

uint32_t topaz_read_mtx_mem(struct drm_psb_private *dev_priv,
			    uint32_t byte_addr)
{
	uint32_t read_val;
	uint32_t reg, bank_size, ram_bank_size, ram_id;

	TOPAZ_READ32(0x3c, &reg);
	reg = 0x0a0a0606;
	bank_size = (reg & 0xF0000) >> 16;

	ram_bank_size = (uint32_t) (1 << (bank_size + 2));
	ram_id = (byte_addr - MTX_DATA_MEM_BASE) / ram_bank_size;

	MTX_WRITE32(MTX_CR_MTX_RAM_ACCESS_CONTROL,
		    F_ENCODE(0x18 + ram_id, MTX_MTX_MCMID) |
		    F_ENCODE(byte_addr >> 2, MTX_MTX_MCM_ADDR) |
		    F_ENCODE(1, MTX_MTX_MCMR));

	/* ?? poll this reg? */
	topaz_wait_for_register(dev_priv,
				MTX_START + MTX_CR_MTX_RAM_ACCESS_STATUS,
				1, 1);

	MTX_READ32(MTX_CR_MTX_RAM_ACCESS_DATA_TRANSFER, &read_val);

	return read_val;
}

void topaz_write_mtx_mem(struct drm_psb_private *dev_priv,
			 uint32_t byte_addr, uint32_t val)
{
	uint32_t ram_id = 0;
	uint32_t reg, bank_size, ram_bank_size;

	TOPAZ_READ32(0x3c, &reg);

	/* PSB_DEBUG_GENERAL ("TOPAZ: DEBUG REG(%x)\n", reg); */
	reg = 0x0a0a0606;

	bank_size = (reg & 0xF0000) >> 16;

	ram_bank_size = (uint32_t) (1 << (bank_size + 2));
	ram_id = (byte_addr - MTX_DATA_MEM_BASE) / ram_bank_size;

	MTX_WRITE32(MTX_CR_MTX_RAM_ACCESS_CONTROL,
		    F_ENCODE(0x18 + ram_id, MTX_MTX_MCMID) |
		    F_ENCODE(byte_addr >> 2, MTX_MTX_MCM_ADDR));

	MTX_WRITE32(MTX_CR_MTX_RAM_ACCESS_DATA_TRANSFER, val);

	/* ?? poll this reg? */
	topaz_wait_for_register(dev_priv,
				MTX_START + MTX_CR_MTX_RAM_ACCESS_STATUS,
				1, 1);

	return;
}

void topaz_write_mtx_mem_multiple_setup(struct drm_psb_private *dev_priv,
					uint32_t byte_addr)
{
	uint32_t ram_id = 0;
	uint32_t reg, bank_size, ram_bank_size;

	TOPAZ_READ32(0x3c, &reg);

	reg = 0x0a0a0606;

	bank_size = (reg & 0xF0000) >> 16;

	ram_bank_size = (uint32_t) (1 << (bank_size + 2));
	ram_id = (byte_addr - MTX_DATA_MEM_BASE) / ram_bank_size;

	MTX_WRITE32(MTX_CR_MTX_RAM_ACCESS_CONTROL,
		    F_ENCODE(0x18 + ram_id, MTX_MTX_MCMID) |
		    F_ENCODE(1, MTX_MTX_MCMAI) |
		    F_ENCODE(byte_addr >> 2, MTX_MTX_MCM_ADDR));
}

void topaz_write_mtx_mem_multiple(struct drm_psb_private *dev_priv,
				  uint32_t val)
{
	MTX_WRITE32(MTX_CR_MTX_RAM_ACCESS_DATA_TRANSFER, val);
}


int topaz_wait_for_register(struct drm_psb_private *dev_priv,
			uint32_t addr, uint32_t value, uint32_t mask)
{
	uint32_t tmp;
	uint32_t count = 10000;

	/* # poll topaz register for certain times */
	while (count) {
		/* #.# read */
		MM_READ32(addr, 0, &tmp);

		if (value == (tmp & mask))
			return 0;

		/* #.# delay and loop */
		DRM_UDELAY(100);
		--count;
	}

	/* # now waiting is timeout, return 1 indicat failed */
	/* XXX: testsuit means a timeout 10000 */

	DRM_ERROR("TOPAZ:time out to poll addr(0x%x) expected value(0x%08x), "
		"actual 0x%08x (0x%08x & 0x%08x)\n",
		addr, value, tmp & mask, tmp, mask);

	return -EBUSY;

}


void lnc_topaz_reset_wq(struct work_struct *work)
{
	struct drm_psb_private *dev_priv =
	    container_of(work, struct drm_psb_private, topaz_watchdog_wq);

	struct psb_scheduler *scheduler = &dev_priv->scheduler;
	unsigned long irq_flags;

	mutex_lock(&dev_priv->topaz_mutex);
	dev_priv->topaz_needs_reset = 1;
	dev_priv->topaz_current_sequence++;
	PSB_DEBUG_GENERAL
	    ("MSVDXFENCE: incremented topaz_current_sequence to :%d\n",
	     dev_priv->topaz_current_sequence);

	psb_fence_error(scheduler->dev, LNC_ENGINE_ENCODE,
			dev_priv->topaz_current_sequence, _PSB_FENCE_TYPE_EXE,
			DRM_CMD_HANG);

	spin_lock_irqsave(&dev_priv->watchdog_lock, irq_flags);
	dev_priv->timer_available = 1;
	spin_unlock_irqrestore(&dev_priv->watchdog_lock, irq_flags);

	spin_lock_irqsave(&dev_priv->topaz_lock, irq_flags);

	/* psb_msvdx_flush_cmd_queue(scheduler->dev); */

	spin_unlock_irqrestore(&dev_priv->topaz_lock, irq_flags);

	psb_schedule_watchdog(dev_priv);
	mutex_unlock(&dev_priv->topaz_mutex);
}


/* this function finish the first part of initialization, the rest
 * should be done in topaz_setup_fw
 */
int lnc_topaz_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	uint32_t core_id, core_rev;
	void *topaz_bo_virt;
	int ret = 0, is_iomem;

	PSB_DEBUG_GENERAL("TOPAZ: init topaz data structures\n");

	/* # initialize comand topaz queueing [msvdx_queue] */
	INIT_LIST_HEAD(&dev_priv->topaz_queue);
	/* # init mutex? CHECK: mutex usage [msvdx_mutex] */
	mutex_init(&dev_priv->topaz_mutex);
	/* # spin lock init? CHECK spin lock usage [msvdx_lock] */
	spin_lock_init(&dev_priv->topaz_lock);

	/* # topaz status init. [msvdx_busy] */
	dev_priv->topaz_busy = 0;
	dev_priv->topaz_cmd_seq = 0;

	/* # gain write back structure,we may only need 32+4=40DW */
	ret = ttm_buffer_object_create(bdev, 4096,
				ttm_bo_type_kernel,
				DRM_PSB_FLAG_MEM_MMU | TTM_PL_FLAG_NO_EVICT,
				0, 0, 0, NULL,  &(dev_priv->topaz_bo));
	if (ret != 0) {
		DRM_ERROR("TOPAZ: failed to allocate topaz BO.\n");
		return ret;
	}

	ret = ttm_bo_kmap(dev_priv->topaz_bo, 0,
			dev_priv->topaz_bo->num_pages,
			&dev_priv->topaz_bo_kmap);
	if (ret) {
		DRM_ERROR("TOPAZ: map topaz BO bo failed......\n");
		ttm_bo_unref(&dev_priv->topaz_bo);
		return ret;
	}

	topaz_bo_virt = ttm_kmap_obj_virtual(&dev_priv->topaz_bo_kmap,
					&is_iomem);
	dev_priv->topaz_ccb_wb =  (void *) topaz_bo_virt;
	dev_priv->topaz_wb_offset = dev_priv->topaz_bo->offset;
	dev_priv->topaz_sync_addr =  (uint32_t *) (topaz_bo_virt + 2048);
	dev_priv->topaz_sync_offset = dev_priv->topaz_wb_offset + 2048;
	PSB_DEBUG_GENERAL("TOPAZ: allocated BO for WriteBack and SYNC command,"
		"WB offset=0x%08x, SYNC offset=0x%08x\n",
		dev_priv->topaz_wb_offset, dev_priv->topaz_sync_offset);

	*(dev_priv->topaz_sync_addr) =  ~0; /* reset sync seq */

	/* # reset topaz */
	MVEA_WRITE32(MVEA_CR_IMG_MVEA_SRST,
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_SPE_SOFT_RESET) |
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_IPE_SOFT_RESET) |
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_CMPRS_SOFT_RESET) |
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_JMCOMP_SOFT_RESET) |
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_CMC_SOFT_RESET) |
		     F_ENCODE(1, MVEA_CR_IMG_MVEA_DCF_SOFT_RESET));

	MVEA_WRITE32(MVEA_CR_IMG_MVEA_SRST,
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_SPE_SOFT_RESET) |
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_IPE_SOFT_RESET) |
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_CMPRS_SOFT_RESET) |
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_JMCOMP_SOFT_RESET) |
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_CMC_SOFT_RESET) |
		     F_ENCODE(0, MVEA_CR_IMG_MVEA_DCF_SOFT_RESET));

	/* # set up MMU */
	topaz_mmu_hwsetup(dev_priv);

	PSB_DEBUG_GENERAL("TOPAZ: defer firmware loading to the place"
			"when receiving user space commands\n");

#if 0 /* can't load FW here */
	/* #.# load fw to driver */
	PSB_DEBUG_GENERAL("TOPAZ: will init firmware\n");
	ret = topaz_init_fw(dev);
	if (ret != 0)
		return -1;

	topaz_setup_fw(dev, FW_H264_NO_RC);/* just for test */
#endif
	/* <msvdx does> # minimal clock */

	/* <msvdx does> # return 0 */
	TOPAZ_READ32(TOPAZ_CR_IMG_TOPAZ_CORE_ID, &core_id);
	TOPAZ_READ32(TOPAZ_CR_IMG_TOPAZ_CORE_REV, &core_rev);

	PSB_DEBUG_GENERAL("TOPAZ: core_id(%x) core_rev(%x)\n",
			core_id, core_rev);

	return 0;
}

int lnc_topaz_uninit(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int n;

	/* flush MMU */
	PSB_DEBUG_GENERAL("XXX: need to flush mmu cache here??\n");
	/* topaz_mmu_flushcache (dev_priv); */

	/* # reset TOPAZ chip */
	lnc_topaz_reset(dev_priv);

	/* release resources */
	/* # release write back memory */
	dev_priv->topaz_ccb_wb = NULL;

	/* # release firmware */
	/* XXX: but this handlnig should be reconsidered */
	/* XXX: there is no jpeg firmware...... */
#if 0 /* FIX WHEN FIRMWARE IS LOADED */
	for (n = 1; n < IMG_CODEC_NUM; ++n) {
		ttm_bo_unref(&topaz_fw[n].text);
		ttm_bo_unref(&topaz_fw[n].data);
	}
#endif
	ttm_bo_kunmap(&dev_priv->topaz_bo_kmap);
	ttm_bo_unref(&dev_priv->topaz_bo);

	return 0;
}

int lnc_topaz_reset(struct drm_psb_private *dev_priv)
{
	return 0;
#if 0
	int ret = 0;
	/* # software reset */
	MTX_WRITE32(MTX_CORE_CR_MTX_SOFT_RESET_OFFSET,
		    MTX_CORE_CR_MTX_SOFT_RESET_MTX_RESET_MASK);

	/* # call lnc_wait_for_register, wait reset finished */
	topaz_wait_for_register(dev_priv,
				MTX_START + MTX_CORE_CR_MTX_ENABLE_OFFSET,
				MTX_CORE_CR_MTX_ENABLE_MTX_ENABLE_MASK,
				MTX_CORE_CR_MTX_ENABLE_MTX_ENABLE_MASK);

	/* # if reset finised  */
	PSB_DEBUG_GENERAL("XXX: add condition judgement for topaz wait...\n");
	/* #.# clear interrupt enable flag */

	/* #.# clear pending interrupt flags */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_INTCLEAR,
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MTX) |
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MTX_HALT) |
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MVEA) |
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MMU_FAULT)
	    );
	/* # destroy topaz mutex in drm_psb_privaet [msvdx_mutex] */

	/* # return register value which is waited above */

	PSB_DEBUG_GENERAL("called\n");
	return 0;
#endif
}

/* read firmware bin file and load all data into driver */
int topaz_init_fw(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	const struct firmware *raw;
	unsigned char *ptr;
	int ret = 0;
	int n;
	struct topaz_fwinfo *cur_fw;
	int cur_size;
	struct topaz_codec_fw *cur_codec;
	struct ttm_buffer_object **cur_drm_obj;
	struct ttm_bo_kmap_obj tmp_kmap;
	int is_iomem;

	dev_priv->stored_initial_qp = 0;

	/* # get firmware */
	ret = request_firmware(&raw, FIRMWARE_NAME, &dev->pdev->dev);
	if (ret != 0) {
		DRM_ERROR("TOPAZ: request_firmware failed: %d\n", ret);
		return ret;
	}

	PSB_DEBUG_GENERAL("TOPAZ: opened firmware\n");

	if (raw->size < sizeof(struct topaz_fwinfo)) {
		DRM_ERROR("TOPAZ: firmware file is not correct size.\n");
		goto out;
	}

	ptr = (unsigned char *) raw->data;

	if (!ptr) {
		DRM_ERROR("TOPAZ: failed to load firmware.\n");
		goto out;
	}

	/* # load fw from file */
	PSB_DEBUG_GENERAL("TOPAZ: load firmware.....\n");
	cur_fw = NULL;
	/* didn't use the first element */
	for (n = 1; n < IMG_CODEC_NUM; ++n) {
		cur_fw = (struct topaz_fwinfo *) ptr;

		cur_codec = &topaz_fw[cur_fw->codec];
		cur_codec->ver = cur_fw->ver;
		cur_codec->codec = cur_fw->codec;
		cur_codec->text_size = cur_fw->text_size;
		cur_codec->data_size = cur_fw->data_size;
		cur_codec->data_location = cur_fw->data_location;

		PSB_DEBUG_GENERAL("TOPAZ: load firemware %s.\n",
				codec_to_string(cur_fw->codec));

		/* #.# handle text section */
		cur_codec->text = NULL;
		ptr += sizeof(struct topaz_fwinfo);
		cur_drm_obj = &cur_codec->text;
		cur_size = cur_fw->text_size;

		/* #.# malloc DRM object for fw storage */
		ret = ttm_buffer_object_create(bdev, cur_size,
				ttm_bo_type_kernel,
				DRM_PSB_FLAG_MEM_MMU | TTM_PL_FLAG_NO_EVICT,
				0, 0, 0, NULL, cur_drm_obj);
		if (ret) {
			DRM_ERROR("Failed to allocate firmware.\n");
			goto out;
		}

		/* #.# fill DRM object with firmware data */
		ret = ttm_bo_kmap(*cur_drm_obj, 0, (*cur_drm_obj)->num_pages,
				&tmp_kmap);
		if (ret) {
			PSB_DEBUG_GENERAL("drm_bo_kmap failed: %d\n", ret);
			ttm_bo_unref(cur_drm_obj);
			*cur_drm_obj = NULL;
			goto out;
		}

		memcpy(ttm_kmap_obj_virtual(&tmp_kmap, &is_iomem), ptr,
		       cur_size);

		ttm_bo_kunmap(&tmp_kmap);

		/* #.# handle data section */
		cur_codec->data = NULL;
		ptr += cur_fw->text_size;
		cur_drm_obj = &cur_codec->data;
		cur_size = cur_fw->data_size;

		/* #.# malloc DRM object for fw storage */
		ret = ttm_buffer_object_create(bdev, cur_size,
				ttm_bo_type_kernel,
				DRM_PSB_FLAG_MEM_MMU | TTM_PL_FLAG_NO_EVICT,
				0, 0, 0, NULL, cur_drm_obj);
		if (ret) {
			DRM_ERROR("Failed to allocate firmware.\n");
			goto out;
		}

		/* #.# fill DRM object with firmware data */
		ret = ttm_bo_kmap(*cur_drm_obj, 0, (*cur_drm_obj)->num_pages,
				&tmp_kmap);
		if (ret) {
			PSB_DEBUG_GENERAL("drm_bo_kmap failed: %d\n", ret);
			ttm_bo_unref(cur_drm_obj);
			*cur_drm_obj = NULL;
			goto out;
		}

		memcpy(ttm_kmap_obj_virtual(&tmp_kmap, &is_iomem), ptr,
		       cur_size);

		ttm_bo_kunmap(&tmp_kmap);

		/* #.# validate firmware */

		/* #.# update ptr */
		ptr += cur_fw->data_size;
	}

	release_firmware(raw);

	PSB_DEBUG_GENERAL("TOPAZ: return from firmware init\n");

	return 0;

out:
	if (raw) {
		PSB_DEBUG_GENERAL("release firmware....\n");
		release_firmware(raw);
	}

	return -1;
}

/* setup fw when start a new context */
int topaz_setup_fw(struct drm_device *dev, enum drm_lnc_topaz_codec codec)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t mem_size = 24 * 1024; /* follow DDK */
	uint32_t verify_pc;

#if 0
	if (codec == dev_priv->topaz_current_codec) {
		LNC_TRACEL("TOPAZ: reuse previous codec\n");
		return 0;
	}
#endif

	/* XXX: need to rest topaz? */
	PSB_DEBUG_GENERAL("XXX: should reset topaz when context change?\n");

	/* XXX: interrupt enable shouldn't be enable here,
	 * this funtion is called when interrupt is enable,
	 * but here, we've no choice since we have to call setup_fw by
	 * manual */
	/* # upload firmware, clear interruputs and start the firmware
	 * -- from  hostutils.c in TestSuits*/

	/* # reset MVEA */
	MVEA_WRITE32(MVEA_CR_IMG_MVEA_SRST,
		F_ENCODE(1, MVEA_CR_IMG_MVEA_SPE_SOFT_RESET) |
		F_ENCODE(1, MVEA_CR_IMG_MVEA_IPE_SOFT_RESET) |
		F_ENCODE(1, MVEA_CR_IMG_MVEA_CMPRS_SOFT_RESET) |
		F_ENCODE(1, MVEA_CR_IMG_MVEA_JMCOMP_SOFT_RESET) |
		F_ENCODE(1, MVEA_CR_IMG_MVEA_CMC_SOFT_RESET) |
		F_ENCODE(1, MVEA_CR_IMG_MVEA_DCF_SOFT_RESET));

	MVEA_WRITE32(MVEA_CR_IMG_MVEA_SRST,
		F_ENCODE(0, MVEA_CR_IMG_MVEA_SPE_SOFT_RESET) |
		F_ENCODE(0, MVEA_CR_IMG_MVEA_IPE_SOFT_RESET) |
		F_ENCODE(0, MVEA_CR_IMG_MVEA_CMPRS_SOFT_RESET) |
		F_ENCODE(0, MVEA_CR_IMG_MVEA_JMCOMP_SOFT_RESET) |
		F_ENCODE(0, MVEA_CR_IMG_MVEA_CMC_SOFT_RESET) |
		F_ENCODE(0, MVEA_CR_IMG_MVEA_DCF_SOFT_RESET));


	topaz_mmu_hwsetup(dev_priv);

	lnc_topaz_disableirq(dev);

	PSB_DEBUG_GENERAL("TOPAZ: will setup firmware....\n");

	topaz_set_default_regs(dev_priv);

	/* # reset mtx */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_SRST,
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_MVEA_SOFT_RESET) |
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_MTX_SOFT_RESET) |
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_VLC_SOFT_RESET));

	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_SRST, 0x0);

	/* # upload fw by drm */
	PSB_DEBUG_GENERAL("TOPAZ: will upload firmware\n");

	topaz_upload_fw(dev, codec);

	PSB_DEBUG_GENERAL("TOPAZ: after upload fw ....\n");

	/* XXX: In power save mode, need to save the complete data memory
	 * and restore it. MTX_FWIF.c record the data size  */
	PSB_DEBUG_GENERAL("TOPAZ:in power save mode need to save memory?\n");

	PSB_DEBUG_GENERAL("TOPAZ: setting up pc address\n");
	topaz_write_core_reg(dev_priv, TOPAZ_MTX_PC, PC_START_ADDRESS);

	PSB_DEBUG_GENERAL("TOPAZ: verify pc address\n");

	topaz_read_core_reg(dev_priv, TOPAZ_MTX_PC, &verify_pc);

	/* enable auto clock is essential for this driver */
	TOPAZ_WRITE32(TOPAZ_CR_TOPAZ_AUTO_CLK_GATE,
		      F_ENCODE(1, TOPAZ_CR_TOPAZ_VLC_AUTO_CLK_GATE) |
		      F_ENCODE(1, TOPAZ_CR_TOPAZ_DB_AUTO_CLK_GATE));
	MVEA_WRITE32(MVEA_CR_MVEA_AUTO_CLOCK_GATING,
		     F_ENCODE(1, MVEA_CR_MVEA_IPE_AUTO_CLK_GATE) |
		     F_ENCODE(1, MVEA_CR_MVEA_SPE_AUTO_CLK_GATE) |
		     F_ENCODE(1, MVEA_CR_MVEA_CMPRS_AUTO_CLK_GATE) |
		     F_ENCODE(1, MVEA_CR_MVEA_JMCOMP_AUTO_CLK_GATE));

	PSB_DEBUG_GENERAL("TOPAZ: current pc(%08X) vs %08X\n",
		   verify_pc, PC_START_ADDRESS);

	/* # turn on MTX */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_INTCLEAR,
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MTX));

	MTX_WRITE32(MTX_CORE_CR_MTX_ENABLE_OFFSET,
		    MTX_CORE_CR_MTX_ENABLE_MTX_ENABLE_MASK);

	/* # poll on the interrupt which the firmware will generate */
	topaz_wait_for_register(dev_priv,
				TOPAZ_START + TOPAZ_CR_IMG_TOPAZ_INTSTAT,
				F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTS_MTX),
				F_MASK(TOPAZ_CR_IMG_TOPAZ_INTS_MTX));

	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_INTCLEAR,
		      F_ENCODE(1, TOPAZ_CR_IMG_TOPAZ_INTCLR_MTX));

	PSB_DEBUG_GENERAL("TOPAZ: after topaz mtx setup ....\n");

	/* # get ccb buffer addr -- file hostutils.c */
	dev_priv->topaz_ccb_buffer_addr = topaz_read_mtx_mem(dev_priv,
					MTX_DATA_MEM_BASE + mem_size - 4);
	dev_priv->topaz_ccb_ctrl_addr = topaz_read_mtx_mem(dev_priv,
					MTX_DATA_MEM_BASE + mem_size - 8);
	dev_priv->topaz_ccb_size = topaz_read_mtx_mem(dev_priv,
			dev_priv->topaz_ccb_ctrl_addr +  MTX_CCBCTRL_CCBSIZE);
	dev_priv->topaz_cmd_windex = 0;

	PSB_DEBUG_GENERAL("TOPAZ:ccb_buffer_addr(%x),ctrl_addr(%x) size(%d)\n",
			dev_priv->topaz_ccb_buffer_addr,
			dev_priv->topaz_ccb_ctrl_addr,
			dev_priv->topaz_ccb_size);

	/* XXX: is there any need to record next cmd num??
	 * we use fence seqence number to record it
	 */
	dev_priv->topaz_cmd_seq = 0;

	/* # write back the initial QP Value */
	topaz_write_mtx_mem(dev_priv,
			dev_priv->topaz_ccb_ctrl_addr + MTX_CCBCTRL_INITQP,
			dev_priv->stored_initial_qp);

	PSB_DEBUG_GENERAL("TOPAZ: write WB mem address 0x%08x\n",
		dev_priv->topaz_wb_offset);
	topaz_write_mtx_mem(dev_priv, MTX_DATA_MEM_BASE + mem_size - 12,
			dev_priv->topaz_wb_offset);

	/* this kick is essential for mtx.... */
	*((uint32_t *) dev_priv->topaz_ccb_wb) = 0x01020304;
	topaz_mtx_kick(dev_priv, 1);
	DRM_UDELAY(1000);
	PSB_DEBUG_GENERAL("TOPAZ: DDK expected 0x12345678 in WB memory,"
			" and here it is 0x%08x\n",
			*((uint32_t *) dev_priv->topaz_ccb_wb));

	*((uint32_t *) dev_priv->topaz_ccb_wb) = 0x0;/* reset it to 0 */
	PSB_DEBUG_GENERAL("TOPAZ: firmware uploaded.\n");

	lnc_topaz_enableirq(dev);

#if 0
	topaz_mmu_flush(dev);

	topaz_test_null(dev, 0xe1e1);
	topaz_test_null(dev, 0xe2e2);
	topaz_mmu_test(dev, 0x12345678);
	topaz_test_null(dev, 0xe3e3);
	topaz_mmu_test(dev, 0x8764321);

	topaz_test_null(dev, 0xe4e4);
	topaz_test_null(dev, 0xf3f3);
#endif

	return 0;
}

#if UPLOAD_FW_BY_DMA
int topaz_upload_fw(struct drm_device *dev, drm_lnc_topaz_codec codec)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	const struct topaz_codec_fw *cur_codec_fw;
	uint32_t text_size, data_size;
	uint32_t data_location;

	/* # refer HLD document */

	/* # MTX reset */
	PSB_DEBUG_GENERAL("TOPAZ: mtx reset.\n");
	MTX_WRITE32(MTX_CORE_CR_MTX_SOFT_RESET_OFFSET,
		    MTX_CORE_CR_MTX_SOFT_RESET_MTX_RESET_MASK);

	DRM_UDELAY(6000);

	/* # upload the firmware by DMA */
	cur_codec_fw = &topaz_fw[codec];

	PSB_DEBUG_GENERAL("Topaz:upload codec %s(%d) text sz=%d data sz=%d"
			" data location(%d)\n",	codec_to_string(codec), codec,
			cur_codec_fw->text_size, cur_codec_fw->data_size,
			cur_codec_fw->data_location);

	/* # upload text */
	text_size = cur_codec_fw->text_size;

	/* setup the MTX to start recieving data:
	   use a register for the transfer which will point to the source
	   (MTX_CR_MTX_SYSC_CDMAT) */
	/* #.# fill the dst addr */
	MTX_WRITE32(MTX_CR_MTX_SYSC_CDMAA, 0x80900000);
	MTX_WRITE32(MTX_CR_MTX_SYSC_CDMAC,
		    F_ENCODE(2, MTX_BURSTSIZE) |
		    F_ENCODE(0, MTX_RNW) |
		    F_ENCODE(1, MTX_ENABLE) |
		    F_ENCODE(text_size, MTX_LENGTH));

	/* #.# set DMAC access to host memory via BIF */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_DMAC_MODE, 1);

	/* #.# transfer the codec */
	topaz_dma_transfer(dev_priv, 0, cur_codec_fw->text->offset, 0,
			   MTX_CR_MTX_SYSC_CDMAT, text_size, 0, 0);

	/* #.# wait dma finish */
	topaz_wait_for_register(dev_priv,
				DMAC_START + IMG_SOC_DMAC_IRQ_STAT(0),
				F_ENCODE(1, IMG_SOC_TRANSFER_FIN),
				F_MASK(IMG_SOC_TRANSFER_FIN));

	/* #.# clear interrupt */
	DMAC_WRITE32(IMG_SOC_DMAC_IRQ_STAT(0), 0);


	/* # upload data */
	data_size = cur_codec_fw->data_size;
	data_location = cur_codec_fw->data_location;

	/* #.# fill the dst addr */
	MTX_WRITE32(MTX_CR_MTX_SYSC_CDMAA,
		    0x80900000 + data_location - 0x82880000);
	MTX_WRITE32(MTX_CR_MTX_SYSC_CDMAC,
		F_ENCODE(2, MTX_BURSTSIZE) |
		F_ENCODE(0, MTX_RNW) |
		F_ENCODE(1, MTX_ENABLE) |
		F_ENCODE(data_size, MTX_LENGTH));

	/* #.# set DMAC access to host memory via BIF */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_DMAC_MODE, 1);

	/* #.# transfer the codec */
	topaz_dma_transfer(dev_priv, 0, cur_codec_fw->data->offset, 0,
			   MTX_CR_MTX_SYSC_CDMAT, data_size, 0, 0);

	/* #.# wait dma finish */
	topaz_wait_for_register(dev_priv,
				DMAC_START + IMG_SOC_DMAC_IRQ_STAT(0),
				F_ENCODE(1, IMG_SOC_TRANSFER_FIN),
				F_MASK(IMG_SOC_TRANSFER_FIN));

	/* #.# clear interrupt */
	DMAC_WRITE32(IMG_SOC_DMAC_IRQ_STAT(0), 0);

	/* # return access to topaz core */
	TOPAZ_WRITE32(TOPAZ_CR_IMG_TOPAZ_DMAC_MODE, 0);

	return 0;
}

#else

void topaz_mtx_upload_by_register(struct drm_device *dev, uint32_t mtx_mem,
				  uint32_t addr, uint32_t size,
				  struct ttm_buffer_object *buf)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t *buf_p;
	uint32_t debug_reg, bank_size, bank_ram_size, bank_count;
	uint32_t cur_ram_id, ram_addr , ram_id;
	int map_ret, lp;
	struct ttm_bo_kmap_obj bo_kmap;
	int is_iomem;
	uint32_t cur_addr;

	get_mtx_control_from_dash(dev_priv);

	map_ret = ttm_bo_kmap(buf, 0, buf->num_pages, &bo_kmap);
	if (map_ret) {
		DRM_ERROR("TOPAZ: drm_bo_kmap failed: %d\n", map_ret);
		return;
	}
	buf_p = (uint32_t *) ttm_kmap_obj_virtual(&bo_kmap, &is_iomem);


	TOPAZ_READ32(TOPAZ_CORE_CR_MTX_DEBUG_OFFSET, &debug_reg);
	debug_reg = 0x0a0a0606;
	bank_size = (debug_reg & 0xf0000) >> 16;
	bank_ram_size = 1 << (bank_size + 2);

	bank_count = (debug_reg & 0xf00) >> 8;

	topaz_wait_for_register(dev_priv,
		MTX_START+MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_OFFSET,
		MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_MTX_MTX_MCM_STAT_MASK,
		MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_MTX_MTX_MCM_STAT_MASK);

	cur_ram_id = -1;
	cur_addr = addr;
	for (lp = 0; lp < size / 4; ++lp) {
		ram_id = mtx_mem + (cur_addr / bank_ram_size);

		if (cur_ram_id != ram_id) {
			ram_addr = cur_addr >> 2;

			MTX_WRITE32(MTX_CORE_CR_MTX_RAM_ACCESS_CONTROL_OFFSET,
				F_ENCODE(ram_id, MTX_MTX_MCMID) |
				F_ENCODE(ram_addr, MTX_MTX_MCM_ADDR) |
				F_ENCODE(1, MTX_MTX_MCMAI));

			cur_ram_id = ram_id;
		}
		cur_addr += 4;

		MTX_WRITE32(MTX_CORE_CR_MTX_RAM_ACCESS_DATA_TRANSFER_OFFSET,
			*(buf_p + lp));

		topaz_wait_for_register(dev_priv,
			MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_OFFSET + MTX_START,
		MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_MTX_MTX_MCM_STAT_MASK,
		MTX_CORE_CR_MTX_RAM_ACCESS_STATUS_MTX_MTX_MCM_STAT_MASK);
	}

	ttm_bo_kunmap(&bo_kmap);

	PSB_DEBUG_GENERAL("TOPAZ: register data upload done\n");
	return;
}

int topaz_upload_fw(struct drm_device *dev, enum drm_lnc_topaz_codec codec)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	const struct topaz_codec_fw *cur_codec_fw;
	uint32_t text_size, data_size;
	uint32_t data_location;

	/* # refer HLD document */
	/* # MTX reset */
	PSB_DEBUG_GENERAL("TOPAZ: mtx reset.\n");
	MTX_WRITE32(MTX_CORE_CR_MTX_SOFT_RESET_OFFSET,
		    MTX_CORE_CR_MTX_SOFT_RESET_MTX_RESET_MASK);

	DRM_UDELAY(6000);

	/* # upload the firmware by DMA */
	cur_codec_fw = &topaz_fw[codec];

	PSB_DEBUG_GENERAL("Topaz: upload codec %s text size(%d) data size(%d)"
			" data location(0x%08x)\n", codec_to_string(codec),
			cur_codec_fw->text_size, cur_codec_fw->data_size,
			cur_codec_fw->data_location);

	/* # upload text */
	text_size = cur_codec_fw->text_size;

	topaz_mtx_upload_by_register(dev, LNC_MTX_CORE_CODE_MEM,
				     PC_START_ADDRESS - MTX_MEMORY_BASE,
				     text_size, cur_codec_fw->text);

	/* # upload data */
	data_size = cur_codec_fw->data_size;
	data_location = cur_codec_fw->data_location;

	topaz_mtx_upload_by_register(dev, LNC_MTX_CORE_DATA_MEM,
				     data_location - 0x82880000, data_size,
				     cur_codec_fw->data);

	return 0;
}

#endif /* UPLOAD_FW_BY_DMA */

void
topaz_dma_transfer(struct drm_psb_private *dev_priv, uint32_t channel,
		   uint32_t src_phy_addr, uint32_t offset,
		   uint32_t dst_addr, uint32_t byte_num,
		   uint32_t is_increment, uint32_t is_write)
{
	uint32_t dmac_count;
	uint32_t irq_stat;
	uint32_t count;

	PSB_DEBUG_GENERAL("TOPAZ: using dma to transfer firmware\n");
	/* # check that no transfer is currently in progress and no
	   interrupts are outstanding ?? (why care interrupt) */
	DMAC_READ32(IMG_SOC_DMAC_COUNT(channel), &dmac_count);

	/* assert(0==(dmac_count & (MASK_IMG_SOC_EN | MASK_IMG_SOC_LIST_EN)));*/

	/* no hold off period */
	DMAC_WRITE32(IMG_SOC_DMAC_PER_HOLD(channel), 0);
	/* clear previous interrupts */
	DMAC_WRITE32(IMG_SOC_DMAC_IRQ_STAT(channel), 0);
	/* check irq status */
	DMAC_READ32(IMG_SOC_DMAC_IRQ_STAT(channel), &irq_stat);
	/* assert(0 == irq_stat); */

	DMAC_WRITE32(IMG_SOC_DMAC_SETUP(channel),
		     ((src_phy_addr +   offset) & MASK_IMG_SOC_START_ADDRESS));
	count = DMAC_VALUE_COUNT(DMAC_BSWAP_NO_SWAP, DMAC_PWIDTH_32_BIT,
				is_write, DMAC_PWIDTH_32_BIT, byte_num);
	/* generate an interrupt at the end of transfer */
	count |= MASK_IMG_SOC_TRANSFER_IEN;
	count |= F_ENCODE(is_write, IMG_SOC_DIR);
	DMAC_WRITE32(IMG_SOC_DMAC_COUNT(channel), count);

	DMAC_WRITE32(IMG_SOC_DMAC_PERIPH(channel),
		     DMAC_VALUE_PERIPH_PARAM(DMAC_ACC_DEL_0,
					     is_increment, DMAC_BURST_2));

	DMAC_WRITE32(IMG_SOC_DMAC_PERIPHERAL_ADDR(channel), dst_addr);

	/* Finally, rewrite the count register with
	 * the enable bit set to kick off the transfer
	 */
	DMAC_WRITE32(IMG_SOC_DMAC_COUNT(channel), count | MASK_IMG_SOC_EN);

	PSB_DEBUG_GENERAL("TOPAZ: dma transfer started.\n");

	return;
}

void topaz_set_default_regs(struct drm_psb_private *dev_priv)
{
	int n;
	int count = sizeof(topaz_default_regs) / (sizeof(unsigned long) * 3);

	for (n = 0; n < count; n++)
		MM_WRITE32(topaz_default_regs[n][0],
			   topaz_default_regs[n][1],
			   topaz_default_regs[n][2]);

}

void topaz_write_core_reg(struct drm_psb_private *dev_priv, uint32_t reg,
			  const uint32_t val)
{
	uint32_t tmp;
	get_mtx_control_from_dash(dev_priv);

	/* put data into MTX_RW_DATA */
	MTX_WRITE32(MTX_CORE_CR_MTX_REGISTER_READ_WRITE_DATA_OFFSET, val);

	/* request a write */
	tmp = reg &
		~MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK;
	MTX_WRITE32(MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_OFFSET, tmp);

	/* wait for operation finished */
	topaz_wait_for_register(dev_priv,
		MTX_START +
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_OFFSET,
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK,
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK);

	release_mtx_control_from_dash(dev_priv);
}

void topaz_read_core_reg(struct drm_psb_private *dev_priv, uint32_t reg,
			uint32_t *ret_val)
{
	uint32_t tmp;

	get_mtx_control_from_dash(dev_priv);

	/* request a write */
	tmp = (reg &
		~MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK);
	MTX_WRITE32(MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_OFFSET,
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_RNW_MASK | tmp);

	/* wait for operation finished */
	topaz_wait_for_register(dev_priv,
		MTX_START +
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_OFFSET,
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK,
		MTX_CORE_CR_MTX_REGISTER_READ_WRITE_REQUEST_MTX_DREADY_MASK);

	/* read  */
	MTX_READ32(MTX_CORE_CR_MTX_REGISTER_READ_WRITE_DATA_OFFSET,
		   ret_val);

	release_mtx_control_from_dash(dev_priv);
}

void get_mtx_control_from_dash(struct drm_psb_private *dev_priv)
{
	int debug_reg_slave_val;

	/* GetMTXControlFromDash */
	TOPAZ_WRITE32(TOPAZ_CORE_CR_MTX_DEBUG_OFFSET,
		      F_ENCODE(1, TOPAZ_CR_MTX_DBG_IS_SLAVE) |
		      F_ENCODE(2, TOPAZ_CR_MTX_DBG_GPIO_OUT));
	do {
		TOPAZ_READ32(TOPAZ_CORE_CR_MTX_DEBUG_OFFSET,
			     &debug_reg_slave_val);
	} while ((debug_reg_slave_val & 0x18) != 0);

	/* save access control */
	TOPAZ_READ32(MTX_CORE_CR_MTX_RAM_ACCESS_CONTROL_OFFSET,
		     &dev_priv->topaz_dash_access_ctrl);
}

void release_mtx_control_from_dash(struct drm_psb_private *dev_priv)
{
	/* restore access control */
	TOPAZ_WRITE32(MTX_CORE_CR_MTX_RAM_ACCESS_CONTROL_OFFSET,
		      dev_priv->topaz_dash_access_ctrl);

	/* release bus */
	TOPAZ_WRITE32(TOPAZ_CORE_CR_MTX_DEBUG_OFFSET,
		      F_ENCODE(1, TOPAZ_CR_MTX_DBG_IS_SLAVE));
}

void topaz_mmu_hwsetup(struct drm_psb_private *dev_priv)
{
	uint32_t pd_addr = psb_get_default_pd_addr(dev_priv->mmu);

	/* bypass all request while MMU is being configured */
	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0,
		      F_ENCODE(1, TOPAZ_CR_MMU_BYPASS));

	/* set MMU hardware at the page table directory */
	PSB_DEBUG_GENERAL("TOPAZ: write PD phyaddr=0x%08x "
			"into MMU_DIR_LIST0/1\n", pd_addr);
	TOPAZ_WRITE32(TOPAZ_CR_MMU_DIR_LIST_BASE(0), pd_addr);
	TOPAZ_WRITE32(TOPAZ_CR_MMU_DIR_LIST_BASE(1), pd_addr);

	/* setup index register, all pointing to directory bank 0 */
	TOPAZ_WRITE32(TOPAZ_CR_MMU_BANK_INDEX, 0);

	/* now enable MMU access for all requestors */
	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0, 0);
}

void topaz_mmu_flushcache(struct drm_psb_private *dev_priv)
{
	uint32_t mmu_control;

	PSB_DEBUG_GENERAL("XXX: Only one PTD/PTE cache"
			" so flush using the master core\n");

	/* XXX: disable interrupt */

	TOPAZ_READ32(TOPAZ_CR_MMU_CONTROL0, &mmu_control);
	mmu_control |= F_ENCODE(1, TOPAZ_CR_MMU_INVALDC);
	PSB_DEBUG_GENERAL("Set Invalid flag (this causes a flush with MMU\n"
		   "still operating afterwards even if not cleared,\n"
		   "but may want to replace with MMU_FLUSH?\n");

	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0, mmu_control);

	/* clear it */
	mmu_control |= (~F_ENCODE(1, TOPAZ_CR_MMU_INVALDC));
	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0, mmu_control);
}

#ifdef DEBUG_FUNCTION
struct reg_pair {
	uint32_t base;
	uint32_t offset;
};

struct reg_pair reg_array[] = {
	{MTX_START, MTX_CR_MTX_ENABLE},
	{MTX_START, MTX_CR_MTX_STATUS},
	{MTX_START, MTX_CR_MTX_KICK},
	{MTX_START, MTX_CR_MTX_KICKI},
	{MTX_START, MTX_CR_MTX_FAULT0},
	{MTX_START, MTX_CR_MTX_REGISTER_READ_WRITE_DATA},
	{MTX_START, MTX_CR_MTX_REGISTER_READ_WRITE_REQUEST},
	{MTX_START, MTX_CR_MTX_RAM_ACCESS_DATA_EXCHANGE},
	{MTX_START, MTX_CR_MTX_RAM_ACCESS_DATA_TRANSFER},
	{MTX_START, MTX_CR_MTX_RAM_ACCESS_CONTROL},
	{MTX_START, MTX_CR_MTX_RAM_ACCESS_STATUS},
	{MTX_START, MTX_CR_MTX_SOFT_RESET},
	{MTX_START, MTX_CR_MTX_SYSC_CDMAC},
	{MTX_START, MTX_CR_MTX_SYSC_CDMAA},
	{MTX_START, MTX_CR_MTX_SYSC_CDMAS0},
	{MTX_START, MTX_CR_MTX_SYSC_CDMAS1},
	{MTX_START, MTX_CR_MTX_SYSC_CDMAT},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_SRST},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_INTSTAT},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_INTENAB},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_INTCLEAR},
	{TOPAZ_START, TOPAZ_CR_TOPAZ_MAN_CLK_GATE},
	{TOPAZ_START, TOPAZ_CR_TOPAZ_AUTO_CLK_GATE},
	{TOPAZ_START, TOPAZ_CR_TOPAZ_MTX_C_RATIO},
	{TOPAZ_START, TOPAZ_CR_MMU_STATUS},
	{TOPAZ_START, TOPAZ_CR_MMU_MEM_REQ},
	{TOPAZ_START, TOPAZ_CR_MMU_CONTROL0},
	{TOPAZ_START, TOPAZ_CR_MMU_CONTROL1},
	{TOPAZ_START, TOPAZ_CR_MMU_BANK_INDEX},
	{TOPAZ_START, TOPAZ_CR_MTX_DEBUG},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_DMAC_MODE},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_RSVD0},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_CORE_ID},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_CORE_REV},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_CORE_DES1},
	{TOPAZ_START, TOPAZ_CR_IMG_TOPAZ_CORE_DES2},
	/* {TOPAZ_START, TOPAZ_CR_MMU_TILE(X) }, */
	/* {TOPAZ_START, TOPAZ_CR_MMU_DIR_LIST_BASE(X) }, */

	{MVEA_START, MVEA_CR_IMG_MVEA_SRST},
	{MVEA_START, MVEA_CR_IMG_MVEA_INTSTAT},
	{MVEA_START, MVEA_CR_IMG_MVEA_INTENAB},
	{MVEA_START, MVEA_CR_IMG_MVEA_INTCLEAR},
	{MVEA_START, MVEA_CR_IMG_MVEA_INT_COMB_SEL},
	{MVEA_START, MVEA_CR_MVEA_START},
	{MVEA_START, MVEA_CR_MVEA_BUSY},
	{MVEA_START, MVEA_CR_MVEA_DMACMDFIFO_WAIT},
	{MVEA_START, MVEA_CR_MVEA_DMACMDFIFO_STATUS},
	{MVEA_START, MVEA_CR_MVEA_AUTO_CLOCK_GATING},
	{MVEA_START, MVEA_CR_MVEA_MAN_CLOCK_GATING},
	{MVEA_START, MVEA_CR_CMC_ESB_DIAGNOSTICS},
	{MVEA_START, MVEA_CR_CMC_DMA_DIAGNOSTICS},
	{MVEA_START, MVEA_CR_CMC_SIGNATURE_ENC_MEM_WDATA},
	{MVEA_START, MVEA_CR_CMC_SIGNATURE_ENC_MEM_ADDR},
	{MVEA_START, MVEA_CR_CMC_PROC_ESB_ACCESS},
	{MVEA_START, MVEA_CR_CMC_LRB_LOGICAL_OFFSET},
	{MVEA_START, MVEA_CR_CMPRS_ACKNOWLEDGE},
	{MVEA_START, MVEA_CR_CMPRS_SBLK_THRESHOLD},
	{MVEA_START, MVEA_CR_CMPRS_COEFF_COST_H},
	{MVEA_START, MVEA_CR_CMPRS_COEFF_COST_L},
	{MVEA_START, MVEA_CR_CMPRS_COEFF_THRESHOLD},
	{MVEA_START, MVEA_CR_CMPRS_SBLK_RIGHT},
	{MVEA_START, MVEA_CR_CMPRS_SBLK_BOTTOM},
	{MVEA_START, MVEA_CR_CMPRS_TRANS_CRC},
	{MVEA_START, MVEA_CR_CMPRS_QUANT_CRC},
	{MVEA_START, MVEA_CR_CMPRS_DIAGNOSTIC1},
	{MVEA_START, MVEA_CR_CMPRS_RLE_CONTROL},
	{MVEA_START, MVEA_CR_CMPRS_RLE_STATUS},
	{MVEA_START, MVEA_CR_CMPRS_MAX_CYCLE_COUNT},
	{MVEA_START, MVEA_CR_CMPRS_MAX_CYCLE_MB},
	{MVEA_START, MVEA_CR_CMPRS_MAX_CYCLE_RESET},
	{MVEA_START, MVEA_CR_CMPRS_VLC_CRC},
	{MVEA_START, MVEA_CR_IPE_PERFORMANCE_LAMBDA},
	{MVEA_START, MVEA_CR_IPE_PERFORMANCE_QP},
	{MVEA_START, MVEA_CR_IPE_CONTROL},
	{MVEA_START, MVEA_CR_IPE_SEARCH_STATUS},
	{MVEA_START, MVEA_CR_IPE_INT_MVCOST},
	{MVEA_START, MVEA_CR_IPE_MB_SAD},
	{MVEA_START, MVEA_CR_IPE_DIAG1},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_0},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_1},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_2},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_3},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_4},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_5},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_6},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_7},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_8},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_9},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_10},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_11},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_12},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_13},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_14},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_15},
	{MVEA_START, MVEA_CR_IPE_JITTER_TABLE_16},
	{MVEA_START, MVEA_CR_IPE_CTRL_CRC},
	{MVEA_START, MVEA_CR_IPE_WDATA_CRC},
	{MVEA_START, MVEA_CR_IPE_MB_PERFORMANCE_CLEAR},
	{MVEA_START, MVEA_CR_IPE_MB_PERFORMANCE_RESULT},
	{MVEA_START, MVEA_CR_IPE_MB_PERFORMANCE_MB_NUMBER},
	{MVEA_START, MVEA_CR_IPE_VECTOR_CLIPPING},
	{MVEA_START, MVEA_CR_JMCOMP_CONTROL},
	{MVEA_START, MVEA_CR_JMCOMP_JPEG_BLOCK_TYPES},
	{MVEA_START, MVEA_CR_JMCOMP_JPEG_LUMA_PRED},
	{MVEA_START, MVEA_CR_JMCOMP_JPEG_CHROMA_PREDS},
	{MVEA_START, MVEA_CR_JMCOMP_CRC},
	{MVEA_START, MVEA_CR_JMCOMP_VLC_CRC},
	{MVEA_START, MVEA_CR_JMCOMP_PERFORMANCE_0},
	{MVEA_START, MVEA_CR_JMCOMP_PERFORMANCE_1},
	{MVEA_START, MVEA_CR_JMCOMP_PERFORMANCE_2},
	{MVEA_START, MVEA_CR_JMCOMP_QP_VALUE},
	{MVEA_START, MVEA_CR_SPE_CONTROL},
	{MVEA_START, MVEA_CR_SPE_INTRA_COST},
	{MVEA_START, MVEA_CR_SPE_ZERO_THRESH},
	{MVEA_START, MVEA_CR_SPE_REQUEST},
	{MVEA_START, MVEA_CR_SPE_INTER_SUM_MIN_SADS},
	{MVEA_START, MVEA_CR_SPE_DIAGNOSTIC1},
	{MVEA_START, MVEA_CR_SPE_INTER_SAD_SIGNATURE},
	{MVEA_START, MVEA_CR_SPE_INTRA_SAD_SIGNATURE},
	{MVEA_START, MVEA_CR_SPE_INTRA16_CONTROL},
	{MVEA_START, MVEA_CR_SPE_INT_MVCOST},
	{MVEA_START, MVEA_CR_SPE_INTER_BIAS_CONTROL},
	{MVEA_START, MVEA_CR_IMG_MVEA_RSVD0},
	{MVEA_START, MVEA_CR_IPE_PRED_VECTOR_BIAS},
	{MVEA_START, MVEA_CR_SPE_PRED_VECTOR_BIAS},
	{MVEA_START, MVEA_CR_SPE_MB_COUNT},
	{MVEA_START, MVEA_CR_SPE_PRED_VECTOR},
	{MVEA_START, MVEA_CR_SPE_MAX_CYCLE_COUNT},
	{MVEA_START, MVEA_CR_SPE_MAX_CYCLE_MB},
	{MVEA_START, MVEA_CR_SPE_INTRA_SUM_MIN_SADS},
	{MVEA_START, MVEA_CR_SPE_MAX_CYCLE_RESET},
	{MVEA_START, MVEA_CR_IPE_SKIPPED_MV},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(0)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(1)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(2)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(3)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(4)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(5)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(6)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(7)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(8)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(9)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(10)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(11)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(12)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(13)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(14)},
	{MVEA_START, MVEA_CR_JMCOMP_CHROMA_QUANTISER(15)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(0)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(1)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(2)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(3)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(4)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(5)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(6)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(7)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(8)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(9)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(10)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(11)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(12)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(13)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(14)},
	{MVEA_START, MVEA_CR_JMCOMP_LUMA_QUANTISER(15)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(0)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(1)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(2)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(3)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(4)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(5)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(6)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(7)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(8)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(9)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(10)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(11)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(12)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(13)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(14)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(15)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(16)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(17)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(18)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(19)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(20)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(21)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(22)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(23)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(24)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(25)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(26)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(27)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(28)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(29)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(30)},
	{MVEA_START, MVEA_CR_CMC_ESB_LOGICAL_REGION_SETUP(31)},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_CONTROL},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_STATUS},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_INFO_0},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_INFO_1},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_INFO_2},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_STUFF_HEAD_CTRL},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_HEADER_FIFO},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_HEADER_CTRL},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_HEADER_STATUS},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_RATE_CTRL_0},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_RATE_CTRL_1},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_BUFFER_SIZE},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_SIGNATURE_0},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_SIGNATURE_1},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_SIGNATURE_2},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_SIGNATURE_3},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_SIGNATURE_4},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_JPEG_CFG},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_PERFORMANCE_0},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_PERFORMANCE_1},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_PERFORMANCE_2},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_IPCM_0},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_IPCM_1},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_MPEG4_CFG},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_MB_PARAMS},
	{TOPAZ_VLC_START, TOPAZ_VLC_CR_VLC_RESET}
};


static int ccb_offset;

void dump_reg(struct drm_psb_private *dev_priv)
{
	int count, n;
	uint32_t val;

	count = sizeof(reg_array) / sizeof(reg_array[1]);

	DRM_UDELAY(100);
	n = 0;
	while (n < count) {
		MM_READ32(reg_array[n].base, reg_array[n].offset, &val);
		PSB_DEBUG_GENERAL("0x%08X,0x%08X,0x%08X\n", reg_array[n].base,
			reg_array[n].offset, val);
		++n;
	}
	DRM_UDELAY(1000);
}



static int topaz_test_null(struct drm_device *dev, uint32_t seq)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* XXX: here we finished firmware setup....
	 * using a NULL command to verify the
	 * correctness of firmware
	 */
	uint32_t null_cmd;
	uint32_t cmd_seq;

	null_cmd = 0 | (1 << 8) | (seq) << 16;
	topaz_write_mtx_mem(dev_priv,
		dev_priv->topaz_ccb_buffer_addr + ccb_offset,
		null_cmd);

	topaz_mtx_kick(dev_priv, 1);

	DRM_UDELAY(1000); /* wait to finish */

	cmd_seq = topaz_read_mtx_mem(dev_priv,
			dev_priv->topaz_ccb_ctrl_addr + 4);

	PSB_DEBUG_GENERAL("Topaz: Sent NULL with sequence=0x%08x,"
			" got sequence=0x%08x (WB_seq=0x%08x,WB_roff=%d)\n",
			seq, cmd_seq, WB_SEQ, WB_ROFF);

	PSB_DEBUG_GENERAL("Topaz: after NULL test, query IRQ and clear it\n");

	topaz_test_queryirq(dev);
	topaz_test_clearirq(dev);

	ccb_offset += 4;

	return 0;
}

static void topaz_mmu_flush(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t val;

	TOPAZ_READ32(TOPAZ_CR_MMU_CONTROL0, &val);
	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0,
		val | F_ENCODE(1, TOPAZ_CR_MMU_INVALDC));
	wmb();
	TOPAZ_WRITE32(TOPAZ_CR_MMU_CONTROL0,
		val & ~F_ENCODE(0, TOPAZ_CR_MMU_INVALDC));
	TOPAZ_READ32(TOPAZ_CR_MMU_CONTROL0, &val);
}

/*
 * this function will test whether the mmu is correct:
 * it get a drm_buffer_object and use CMD_SYNC to write
 * certain value into this buffer.
 */
static void topaz_mmu_test(struct drm_device *dev, uint32_t sync_value)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t sync_cmd;
	unsigned long real_pfn;
	int ret;
	uint32_t cmd_seq;

	*((uint32_t *)dev_priv->topaz_sync_addr) = 0xeeeeeeee;

	/* topaz_mmu_flush(dev); */

	sync_cmd = MTX_CMDID_SYNC | (3 << 8) | (0xeeee) << 16;

	topaz_write_mtx_mem_multiple_setup(dev_priv,
			dev_priv->topaz_ccb_buffer_addr + ccb_offset);

	topaz_write_mtx_mem_multiple(dev_priv, sync_cmd);
	topaz_write_mtx_mem_multiple(dev_priv, dev_priv->topaz_sync_offset);
	topaz_write_mtx_mem_multiple(dev_priv, sync_value);

	topaz_mtx_kick(dev_priv, 1);

	ret = psb_mmu_virtual_to_pfn(psb_mmu_get_default_pd(dev_priv->mmu),
				dev_priv->topaz_sync_offset, &real_pfn);
	if (ret != 0) {
		PSB_DEBUG_GENERAL("psb_mmu_virtual_to_pfn failed,exit\n");
		return;
	}
	PSB_DEBUG_GENERAL("TOPAZ: issued SYNC command, "
			"BO offset=0x%08x (pfn=%lu), synch value=0x%08x\n",
		dev_priv->topaz_sync_offset, real_pfn, sync_value);

	/* XXX: if we can use interrupt, we can wait this command finish */
	/* topaz_wait_for_register (dev_priv,
	   TOPAZ_START + TOPAZ_CR_IMG_TOPAZ_INTSTAT, 0xf, 0xf); */
	DRM_UDELAY(1000);

	cmd_seq = topaz_read_mtx_mem(dev_priv,
				dev_priv->topaz_ccb_ctrl_addr + 4);
	PSB_DEBUG_GENERAL("Topaz: cmd_seq equals 0x%x, and expected 0x%x "
			"(WB_seq=0x%08x,WB_roff=%d),synch value is 0x%x,"
			"expected 0x%08x\n",
			cmd_seq, 0xeeee, WB_SEQ, WB_ROFF,
			*((uint32_t *)dev_priv->topaz_sync_addr), sync_value);

	PSB_DEBUG_GENERAL("Topaz: after MMU test, query IRQ and clear it\n");
	topaz_test_queryirq(dev);
	topaz_test_clearirq(dev);

	ccb_offset += 3*4; /* shift 3DWs */
}

#endif
