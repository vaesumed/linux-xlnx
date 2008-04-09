#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/semaphore.h>
#include <ieee1394_hotplug.h>
#include <nodemgr.h>
#include <highlevel.h>
#include <ohci1394.h>
#include <hosts.h>
#include <dvbdev.h>

#include "firesat.h"
#include "avc_api.h"
#include "cmp.h"
#include "firesat-rc.h"
#include "firesat-ci.h"

static struct firesat_channel *firesat_channel_allocate(struct firesat *firesat)
{
	int k;

	printk(KERN_INFO "%s\n", __func__);

	if (down_interruptible(&firesat->demux_sem))
		return NULL;

	for (k = 0; k < 16; k++) {
		printk(KERN_INFO "%s: channel %d: active = %d, pid = 0x%x\n",__func__,k,firesat->channel[k].active,firesat->channel[k].pid);

		if (firesat->channel[k].active == 0) {
			firesat->channel[k].active = 1;
			up(&firesat->demux_sem);
			return &firesat->channel[k];
		}
	}

	up(&firesat->demux_sem);
	return NULL; // no more channels available
}

static int firesat_channel_collect(struct firesat *firesat, int *pidc, u16 pid[])
{
	int k, l = 0;

	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;

	for (k = 0; k < 16; k++)
		if (firesat->channel[k].active == 1)
			pid[l++] = firesat->channel[k].pid;

	up(&firesat->demux_sem);

	*pidc = l;

	return 0;
}

static int firesat_channel_release(struct firesat *firesat,
				   struct firesat_channel *channel)
{
	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;

	channel->active = 0;

	up(&firesat->demux_sem);
	return 0;
}

int firesat_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firesat *firesat = (struct firesat*)dvbdmxfeed->demux->priv;
	struct firesat_channel *channel;
	int pidc,k;
	u16 pids[16];

	printk(KERN_INFO "%s (pid %u)\n",__func__,dvbdmxfeed->pid);

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
	case DMX_TYPE_SEC:
		break;
	default:
		printk("%s: invalid type %u\n",__func__,dvbdmxfeed->type);
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			channel = firesat_channel_allocate(firesat);
			break;
		default:
			printk("%s: invalid pes type %u\n",__func__, dvbdmxfeed->pes_type);
			return -EINVAL;
		}
	} else {
		channel = firesat_channel_allocate(firesat);
	}

	if (!channel) {
		printk("%s: busy!\n", __func__);
		return -EBUSY;
	}

	dvbdmxfeed->priv = channel;

	channel->dvbdmxfeed = dvbdmxfeed;
	channel->pid = dvbdmxfeed->pid;
	channel->type = dvbdmxfeed->type;
	channel->firesat = firesat;

	if (firesat_channel_collect(firesat, &pidc, pids)) {
		firesat_channel_release(firesat, channel);
		return -EINTR;
	}

	if ((k = AVCTuner_SetPIDs(firesat, pidc, pids))) {
		firesat_channel_release(firesat, channel);
		printk("%s: AVCTuner failed with error %d\n", __func__, k);
		return k;
	}

	return 0;
}

int firesat_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct firesat *firesat = (struct firesat*)demux->priv;
	int k, l = 0;
	u16 pids[16];

	printk(KERN_INFO "%s (pid %u)\n", __func__, dvbdmxfeed->pid);

	if (dvbdmxfeed->type == DMX_TYPE_TS && !((dvbdmxfeed->ts_type & TS_PACKET) &&
				(demux->dmx.frontend->source != DMX_MEMORY_FE))) {

		if (dvbdmxfeed->ts_type & TS_DECODER) {

			if (dvbdmxfeed->pes_type >= DMX_TS_PES_OTHER ||
				!demux->pesfilter[dvbdmxfeed->pes_type])

				return -EINVAL;

			demux->pids[dvbdmxfeed->pes_type] |= 0x8000;
			demux->pesfilter[dvbdmxfeed->pes_type] = 0;
		}

		if (!(dvbdmxfeed->ts_type & TS_DECODER &&
			dvbdmxfeed->pes_type < DMX_TS_PES_OTHER))

			return 0;
	}

	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;

	// list except channel to be removed
	for (k = 0; k < 16; k++)
		if (firesat->channel[k].active == 1 && &firesat->channel[k] != (struct firesat_channel*)dvbdmxfeed->priv)
			pids[l++] = firesat->channel[k].pid;

	if ((k = AVCTuner_SetPIDs(firesat, l, pids))) {
		up(&firesat->demux_sem);
		return k;
	}

	((struct firesat_channel*)dvbdmxfeed->priv)->active = 0;

	up(&firesat->demux_sem);

	return 0;
}
