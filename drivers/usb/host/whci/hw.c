/*
 * Wireless Host Controller (WHC) hardware access helpers.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

void whc_write_wusbcmd(struct whc *whc, u32 mask, u32 val)
{
	unsigned long flags;
	u32 cmd;

	spin_lock_irqsave(&whc->lock, flags);

	cmd = le_readl(whc->base + WUSBCMD);
	cmd = (cmd & ~mask) | val;
	le_writel(cmd, whc->base + WUSBCMD);

	spin_unlock_irqrestore(&whc->lock, flags);
}

int whc_do_gencmd(struct whc *whc, u32 cmd, u32 params, void *addr, size_t len)
{
	unsigned long flags;
	dma_addr_t dma_addr;
	int t, ret = 0;

	if (addr)
		dma_addr = dma_map_single(&whc->umc->dev, addr, len, DMA_TO_DEVICE);
	else
		dma_addr = 0;

	mutex_lock(&whc->mutex);

	/* poke registers to start cmd */
	spin_lock_irqsave(&whc->lock, flags);

	le_writel(params, whc->base + WUSBGENCMDPARAMS);
	le_writeq(dma_addr, whc->base + WUSBGENADDR);

	le_writel(WUSBGENCMDSTS_ACTIVE | WUSBGENCMDSTS_IOC | cmd,
		  whc->base + WUSBGENCMDSTS);

	spin_unlock_irqrestore(&whc->lock, flags);

	/* wait for command to complete */
	t = wait_event_timeout(whc->cmd_wq,
			       (le_readl(whc->base + WUSBGENCMDSTS) & WUSBGENCMDSTS_ACTIVE) == 0,
			       WHC_GENCMD_TIMEOUT_MS);
	if (t == 0) {
		ret = -ETIMEDOUT;
		dev_err(&whc->umc->dev, "generic command timeout (%04x/%04x)\n",
			cmd, params);
	}

	mutex_unlock(&whc->mutex);

	if (addr)
		dma_unmap_single(&whc->umc->dev, dma_addr, len, DMA_TO_DEVICE);

	return ret;
}
