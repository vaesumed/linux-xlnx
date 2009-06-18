/*
 *  OQO WMI UPMC Extras
 *
 *  Copyright (C) 2008 Brian S. Julin <bri@abrij.org>
 *
 *  Based on acer-wmi:
 *    Copyright (C) 2007-2008	Carlos Corbacho <cathectic@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * NOTE: You need to turn SMI on in BIOS (if dmidecode works, you already have)
 * NOTE: acpi-wmi support mandatory
 * NOTE: backlight and inputdev support a must, ifdefs will come later
 */

/*
 *
 *  0.3: added WLAN enable switch, restore settings on unload,
 *       resume/suspend handling
 *  0.2: Still not production-ready, but added ambient light sensor,
 *       backlight, and it prints the unit serial number to dmesg (do
 *       not know where to make that available to userspace yet.)
 *  0.1: This is a first cut.  Plan to reboot after playing with this.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/i8042.h>
#include <linux/input-polldev.h>
#include <linux/rfkill.h>

#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("Brian Julin");
MODULE_DESCRIPTION("OQO UPMC WMI Extras Driver");
MODULE_LICENSE("GPL");

#define OQO_LOGPREFIX "oqo-wmi: "
#define OQO_ERR KERN_ERR OQO_LOGPREFIX
#define OQO_NOTICE KERN_NOTICE OQO_LOGPREFIX
#define OQO_INFO KERN_INFO OQO_LOGPREFIX

#define OQO_KINE_MAXTRY 3

/* Store defined devices globally since we only have one instance. */
static struct platform_device *oqo_platform_device;
static struct backlight_device *oqo_backlight_device;
static struct rfkill *oqo_rfkill;
static struct input_dev *oqo_kine;
static struct input_polled_dev *oqo_kine_polled;

/* Likewise store current and original settings globally. */
struct oqo_settings {
	int lid_wakes;		/* not sure if ACPI handles/needs help here */
	int kine_itvl;
	int bl_bright;
};

static struct oqo_settings orig, curr;

/* Some of this code is left like in acer-wmi so we can add the older
   Model 01 and any future models more easily, but we should not expect
   it to be as complicated as Acer given each model is a leap rather than
   a subtle variant on the last, so we aren't using "quirks" perse.  Not
   sure if there is any real difference for our purposes between the o2
   and e2.
*/
struct oqo_model {
	const char *model;
	u16 model_subs;
};
#define MODEL_SUB_OQO_O2_SMB0 3

static struct oqo_model oqo_models[] = {
	{
	 .model = "Model 2",
	 .model_subs = MODEL_SUB_OQO_O2_SMB0,
	 },
	{}
};

static struct oqo_model *model;

static int force;
module_param(force, bool, 0644);
MODULE_PARM_DESC(force, "Force WMI detection even if DMI detection failed");

/*
 * OQO Model 2 SMBUS registers
 * We are just using WMI to read the Cx700 smbus, to share the
 * ACPI mutex (what may also eventually work in VMs/win32)
 * Using i2c-viapro directly could interfere with PM.
 */

#define OQO_O2_SMB0_WWAN_DSBL_ADDR	0x19
#define OQO_O2_SMB0_WWAN_DSBL_MASK	0x02
#define OQO_O2_SMB0_LUMIN_LO		0x20
#define OQO_O2_SMB0_LUMIN_HI		0x21
#define OQO_O2_SMB0_BL_LO		0x26
#define OQO_O2_SMB0_BL_HI		0x27
#define OQO_O2_SMB0_ACCEL_POLL_ITVL	0x45
#define OQO_O2_SMB0_ACCEL_XLO		0x50
#define OQO_O2_SMB0_ACCEL_XHI		0x51
#define OQO_O2_SMB0_ACCEL_YLO		0x52
#define OQO_O2_SMB0_ACCEL_YHI		0x53
#define OQO_O2_SMB0_ACCEL_ZLO		0x54
#define OQO_O2_SMB0_ACCEL_ZHI		0x55
/* These may be handled by ACPI not sure yet. */
#define OQO_O2_SMB0_LID_WAKES_ADDR	0x58
#define OQO_O2_SMB0_LID_WAKES_MASK	0x08

#define OQO_O2_SMB0_SERIAL_START	0x70
#define OQO_O2_SMB0_SERIAL_LEN		11

static char oqo_sn[OQO_O2_SMB0_SERIAL_LEN + 1];

/* Other addresses I have noticed used on the 02 SMBUS (from DSDT and whatnot)
 *
 * These are not used because the linux ACPI drivers work fine on them
 *
 * 0x0A -- processor sleep mode?
 * 0x0C -- ACPI events, probably clears when read.
 * 0x30 -- thermal zone
 *      There is something going on at 0x31 through 0x34 which is likely
 *      also thermal.  The values change over time.  Have not figured that
 *      out yet.
 * 0x41 -- AC detect
 * 0x42 -- LID button   ACTUALLY THIS DOES NOT WORK AND NEEDS TO BE FIXED
 * 0xa0 and 0xa1 -- battery something (presence? state?)
 * 0xa4 to 0xcf -- battery info (0xc8-0xca contains "OQO")
 * 0xd4 to 0xef -- other battery stats
 */

/*
 * OQO method GUIDs
 */
#define OQO_O2_AMW0_GUID "ABBC0F6D-8EA1-11D1-00A0-C90629100000"
MODULE_ALIAS("wmi:ABBC0F6D-8EA1-11D1-00A0-C90629100000");

/*
 * Interface type flags
 */
enum interface_type {
	OQO_O2_AMW0,
};

/* Each low-level interface must define at least some of the following */
struct wmi_interface {
	/* The WMI device type */
	u32 type;
};

static struct wmi_interface AMW0_interface = {
	.type = OQO_O2_AMW0,
};

/* The detected/chosen interface */
static struct wmi_interface *interface;

static int dmi_matched(const struct dmi_system_id *dmi)
{
	model = dmi->driver_data;
	/*
	 * Detect which ACPI-WMI interface we're using.
	 */
	if (wmi_has_guid(OQO_O2_AMW0_GUID))
		interface = &AMW0_interface;

	return 0;
}

static struct dmi_system_id oqo_dmis[] = {
	{
	 .callback = dmi_matched,
	 .ident = "OQO 02",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "OQO Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "OQO Model 2"),
		     },
	 .driver_data = oqo_models + 0,
	 },
	{}
};

/*
 * AMW0 (V1) interface on OQO Model 2
 *
 * wmba: has four functions selected by int arg 1.  arg2 is 3 byte buffer.
 *       1: performs GETB method on the SMBUS using bytes 0, 1 of Arg2
 *          returns a buffer object containing a single byte
 *       2: performs SETB on SMBUS using bytes 0, 1, 2 of Arg2
 *          returns 0 as int.
 *       3: dumps 256 values into a given SMBUS register (not used here)
 *          returns 0 as int.
 *       4: puts byte 0 of arg2 into some sort of busy flag.  Some ACPI
 *          funcs check this (==0) to decide if SMBUS operations are safe.
 *          returns 0 as int.
 * wmbb: simply returns the busy flag set by wmba #4
 */
static acpi_status oqo_smbus_getb(u8 addr, u8 *result)
{
	struct acpi_buffer input, res;
	acpi_status status;
	union acpi_object *obj;
	u32 arg2;

	input.length = 4;
	input.pointer = &arg2;
	res.length = ACPI_ALLOCATE_BUFFER;
	res.pointer = NULL;

	arg2 = addr;
	arg2 <<= 8;
	arg2 |= 0x12;		/* HOSTCMD */

	status = wmi_evaluate_method(OQO_O2_AMW0_GUID, 1, 1, &input, &res);

	if (status != AE_OK)
		return status;

	obj = (union acpi_object *)res.pointer;
	if (!obj)
		return AE_NULL_OBJECT;

	if (obj->type != ACPI_TYPE_BUFFER
	    || obj->buffer.length != 1 || obj->buffer.pointer == NULL) {
		kfree(obj);
		return AE_TYPE;
	}
	*result = ((u8 *) (obj->buffer.pointer))[0];
	kfree(obj);
	return status;
}

static acpi_status oqo_smbus_setb(u8 addr, u8 val)
{
	struct acpi_buffer input, res;
	acpi_status status;
	union acpi_object *obj;
	u32 arg2;

	input.length = 4;
	input.pointer = &arg2;
	res.length = ACPI_ALLOCATE_BUFFER;
	res.pointer = NULL;

	arg2 = val;
	arg2 <<= 8;
	arg2 |= addr;
	arg2 <<= 8;
	arg2 |= 0x12;		/* HOSTCMD */

	status = wmi_evaluate_method(OQO_O2_AMW0_GUID, 1, 2, &input, &res);

	if (status != AE_OK)
		return status;

	obj = (union acpi_object *)res.pointer;
	if (!obj)
		return AE_NULL_OBJECT;

	if (obj->type != ACPI_TYPE_INTEGER) {
		kfree(obj);
		return AE_TYPE;
	}
	kfree(obj);
	return status;
}

/*
 * We assume we are the only one using this ...ahem... "lock" on
 * the SMBUS because it would be pathetically noneffective otherwise.
 *
 * Nonzero silly_lock will keep certain ACPI routines away from the
 * SMBUS (if they aren't already on it when you call it.)  Zero
 *  silly_lock will let them back on
 *
 * This is probably useful before sleeping the system, and one
 * waits until any ACPI funcs would have long finished before
 * proceeding.  It seems harmless enough and will work to wrap
 * more accesses with it.
 */
static acpi_status oqo_lock_smbus(int silly_lock)
{
	struct acpi_buffer input, res;
	acpi_status status;
	union acpi_object *obj;
	u32 arg2;

	input.length = 4;
	input.pointer = &arg2;
	res.length = ACPI_ALLOCATE_BUFFER;
	res.pointer = NULL;

	arg2 = !!silly_lock;

	status = wmi_evaluate_method(OQO_O2_AMW0_GUID, 1, 4, &input, &res);

	if (status != AE_OK)
		return status;

	obj = (union acpi_object *)res.pointer;
	if (!obj)
		return AE_NULL_OBJECT;

	if (obj->type != ACPI_TYPE_INTEGER) {
		kfree(obj);
		return AE_TYPE;
	}
	kfree(obj);
	return status;
}

static int smread_s16(u8 hi_addr, u8 lo_addr)
{
	s16 ret = -1;
	acpi_status status;
	u8 r;

	/* Keep some ACPI routines off the SMBUS */
	status = oqo_lock_smbus(1);
	if (ACPI_FAILURE(status))
		goto skip;

	status = oqo_smbus_getb(hi_addr, &r);
	if (ACPI_FAILURE(status))
		goto skip;

	ret = r;
	ret <<= 8;

	status = oqo_smbus_getb(lo_addr, &r);
	if (ACPI_FAILURE(status)) {
		ret = -1;
		goto skip;
	}

	ret |= r;
	ret &= 0x7fff;
skip:
	/* Let ACPI routines back on the SMBUS */
	status = oqo_lock_smbus(0);
	if (ACPI_FAILURE(status))
		return -1;
	return (int)ret;
}

static int smwrite_s16(u8 hi_addr, u8 lo_addr, s16 val)
{
	acpi_status status;
	u8 r;
	int ret = -1;

	status = oqo_lock_smbus(1);
	if (ACPI_FAILURE(status))
		goto skip;

	r = (val >> 8) & 0x7f;
	status = oqo_smbus_setb(hi_addr, r);
	if (ACPI_FAILURE(status))
		goto skip;

	r = val & 0xff;
	status = oqo_smbus_setb(lo_addr, r);
	if (ACPI_FAILURE(status))
		goto skip;

	ret = 0;
skip:
	status = oqo_lock_smbus(0);
	if (ACPI_FAILURE(status))
		return -1;
	return ret;
}

static int smread_u8(u8 addr)
{
	int ret = -1;
	acpi_status status;
	u8 r;

	status = oqo_lock_smbus(1);
	if (ACPI_FAILURE(status))
		goto skip;

	status = oqo_smbus_getb(addr, &r);
	if (ACPI_FAILURE(status))
		goto skip;

	ret = r;
skip:
	status = oqo_lock_smbus(0);
	if (ACPI_FAILURE(status))
		return -1;
	return (int)ret;
}

static int smwrite_u8(u8 addr, u8 val)
{
	acpi_status status;
	int ret = -1;

	status = oqo_lock_smbus(1);
	if (ACPI_FAILURE(status))
		goto skip;

	status = oqo_smbus_setb(addr, val);
	if (ACPI_FAILURE(status))
		goto skip;

	ret = 0;
skip:
	status = oqo_lock_smbus(0);
	if (ACPI_FAILURE(status))
		return -1;
	return ret;
}

/*
 * Accelerometer inputdev
 */

/*
 * Get a reading of the accelerometer from the firwmware and push
 * it to an inputdev.
 *
 * Also the ambient light detector hitch-hikes on the inputdev, since
 * it could be useful in some of the same applications for accelerometers.
 *
 * Available information and a bit of poking have not found a
 * way to freeze a snapshot of the accelerometer data, so we have
 * to do consistency checks to reduce the odds that we mix low
 * and high bytes from different updates.
 *
 * Unfortunately SMBUS access is very slow (11ms) and the firmware API
 * does not provide 2-byte transfers, so mixed readings happen and
 * have to be corrected a lot.  (Do not know why; it should be a
 * multi-kHz.. bus and the reads take only a hundred-ish cycles/byte.
 * It is not the ACPI function -- it is slow on i2c-viapro as well.)
 *
 * Since there is such a big time lag between readings, the axis
 * are decoupled and reported separately on different timelines as
 * different events rather than as a set.
 */
static acpi_status oqo_read_kine(int *good, s16 *x, s16 *y, s16 *z,
				 u16 *lumin)
{
	u8 hiregs[4] = { OQO_O2_SMB0_ACCEL_XHI,
		OQO_O2_SMB0_ACCEL_YHI,
		OQO_O2_SMB0_ACCEL_ZHI,
		OQO_O2_SMB0_LUMIN_HI
	};
	u8 loregs[4] = { OQO_O2_SMB0_ACCEL_XLO,
		OQO_O2_SMB0_ACCEL_YLO,
		OQO_O2_SMB0_ACCEL_ZLO,
		OQO_O2_SMB0_LUMIN_LO
	};

	short ax[4] = { ABS_X, ABS_Y, ABS_Z, ABS_MISC };
	u8 realgood = 0;
	u16 res[4];
	acpi_status status;
	int i;

	*good = 0;

	/* Routine: Starting with the lo byte, read lo/hi bytes
	   alternately until two lo byte readings, match.  Then
	   take that reading and combine it with the hi reading
	   sandwiched between.  Errors can still happen when
	   jittering at wrap boundaries, but should be rare.

	   Don't use this for missile guidance.

	   Userspace post-processing error detection encouraged.
	 */
	for (i = 0; i < 4; i++) {
		int maxtry;
		u32 log;
		u8 r, lo, hi;

		lo = loregs[i];
		hi = hiregs[i];
		log = 0;

#define LOGRES(reg)  do { \
			status = oqo_smbus_getb(reg, &r);		\
			log <<= 8; log |= r; log &= 0xffffff;		\
			if (ACPI_FAILURE(status))			\
				goto leave;				\
		} while (0)

		maxtry = OQO_KINE_MAXTRY + 1;
		while (maxtry) {
			LOGRES(lo);
			if (maxtry <= OQO_KINE_MAXTRY &&
			    (log >> 16) == (log & 0xff)) {
				*(res + i) = log & 0xffff;
				break;
			}
			LOGRES(hi);
			maxtry--;
		}

		if (maxtry == OQO_KINE_MAXTRY)
			realgood |= 1 << i;

		if (maxtry) {
			*good |= 1 << i;
			/* JIC CYA: this bit may be reserved */
			res[3] &= 0x7fff;
			input_report_abs(oqo_kine, ax[i], (s16) res[i]);
		}
		/* else we had trouble getting the reading to lock
		   and we skip reporting this axis.
		 */
	}

	*x = (u16) res[0];
	*y = (u16) res[1];
	*z = (u16) res[2];
	*lumin = (u16) res[3];
	return status;
leave:
	return status;
}

/*
 * Generic Device (interface-independent)
 */

static void oqo_kine_poll(struct input_polled_dev *dev)
{
	s16 x, y, z;
	u16 lumin;
	int good;
	/*      struct timeval tv1, tv2; */

	if (dev != oqo_kine_polled)
		return;
	if (orig.kine_itvl < 0)
		return;

	x = y = z = 0;
	oqo_read_kine(&good, &x, &y, &z, &lumin);
}

static int __devinit oqo_kine_init(void)
{
	int err;

	oqo_kine = input_allocate_device();
	if (!oqo_kine)
		return -ENOMEM;

	oqo_kine->name = "OQO embedded accelerometer";
	oqo_kine->phys = "platform:oqo-wmi:kine";
	oqo_kine->id.bustype = 0;
	oqo_kine->id.vendor = 0;
	oqo_kine->id.product = 2;
	oqo_kine->id.version = 0;
	oqo_kine->evbit[0] = BIT_MASK(EV_ABS);
	set_bit(ABS_X, oqo_kine->absbit);
	set_bit(ABS_Y, oqo_kine->absbit);
	set_bit(ABS_Z, oqo_kine->absbit);
	set_bit(ABS_MISC, oqo_kine->absbit);
	oqo_kine->absmin[ABS_X] =
	    oqo_kine->absmin[ABS_Y] =
	    oqo_kine->absmin[ABS_Z] = oqo_kine->absmin[ABS_MISC] = -32768;
	oqo_kine->absmax[ABS_X] =
	    oqo_kine->absmax[ABS_Y] =
	    oqo_kine->absmax[ABS_Z] = oqo_kine->absmax[ABS_MISC] = 32767;

	dev_set_name(&oqo_kine->dev, "kine");

	oqo_kine_polled = input_allocate_polled_device();
	if (!oqo_kine_polled) {
		err = -ENOMEM;
		goto bail0;
	}

	oqo_kine_polled->poll = oqo_kine_poll;
	oqo_kine_polled->poll_interval = 250;
	oqo_kine_polled->input = oqo_kine;

	orig.kine_itvl = -1;	/* prevent callback from running */
	err = input_register_polled_device(oqo_kine_polled);
	if (err) {
		printk(OQO_ERR "Failed to register OQO kine input\n");
		goto bail1;
	}

	/* This will allow the callback to run now if successful. */
	orig.kine_itvl = smread_u8(OQO_O2_SMB0_ACCEL_POLL_ITVL);
	smwrite_u8(OQO_O2_SMB0_ACCEL_POLL_ITVL, 250);
	curr.kine_itvl = smread_u8(OQO_O2_SMB0_ACCEL_POLL_ITVL);
	if (orig.kine_itvl < 0 || curr.kine_itvl != 250) {
		printk(OQO_ERR "Test communication with kine sensor failed\n");
		err = -ENODEV;
		goto bail2;
	}

	printk(OQO_INFO "Created OQO kine input.\n");
	printk(OQO_INFO "Firmware interval %ims, driver interval %ims\n",
	       curr.kine_itvl, oqo_kine_polled->poll_interval);
	return 0;
bail2:
	input_unregister_polled_device(oqo_kine_polled);
bail1:
	input_free_polled_device(oqo_kine_polled);	/* frees oqo_kine */
	return err;
bail0:
	input_free_device(oqo_kine);
	return err;
}

static void __devexit oqo_kine_fini(void)
{
	smwrite_u8(OQO_O2_SMB0_ACCEL_POLL_ITVL, orig.kine_itvl);
	input_unregister_polled_device(oqo_kine_polled);
	input_free_polled_device(oqo_kine_polled);
}

/*
 * Backlight device
 */
static int read_brightness(struct backlight_device *bd)
{
	return (int)smread_s16(OQO_O2_SMB0_BL_HI, OQO_O2_SMB0_BL_LO);
}

static int update_bl_status(struct backlight_device *bd)
{
	return smwrite_s16(OQO_O2_SMB0_BL_HI,
			   OQO_O2_SMB0_BL_LO, (s16) bd->props.brightness);
}

static struct backlight_ops oqo_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int __devinit oqo_backlight_init(struct device *dev)
{
	struct backlight_device *bd;

	/*
	 * It would be nice if someone would figure out how backlights
	 * like these, which are not driven through the video hardware,
	 * are supposed to find their associated fb and bind to it (and
	 * rebind when fb drivers change.
	 *
	 * Most extras backlights just shove a junk name in like we do here,
	 * and don't end up integrated with fbcon sysfs as a result.
	 */
	bd = backlight_device_register("oqo-bl", dev, NULL, &oqo_bl_ops);

	if (IS_ERR(bd)) {
		printk(OQO_ERR "Could not register OQO backlight device\n");
		oqo_backlight_device = NULL;
		return PTR_ERR(bd);
	}

	oqo_backlight_device = bd;
	bd->props.max_brightness = 0x7fff;
	curr.bl_bright = orig.bl_bright = bd->props.brightness =
	    read_brightness(NULL);

	if (bd->props.brightness < 0)
		goto fail;

	backlight_update_status(bd);
	printk(OQO_INFO "Found backlight set at %i\n", bd->props.brightness);
	return 0;

fail:
	backlight_device_unregister(oqo_backlight_device);
	oqo_backlight_device = NULL;
	return -ENODEV;
}

static void __devexit oqo_backlight_fini(void)
{
	if (!oqo_backlight_device)
		return;
	oqo_backlight_device->props.brightness = orig.bl_bright;
	backlight_update_status(oqo_backlight_device);
	backlight_device_unregister(oqo_backlight_device);
}

/*
 * RFKill device
 */

static int oqo_rfkill_get(void *data, enum rfkill_state *state)
{
	int res;

	res = smread_u8(OQO_O2_SMB0_WWAN_DSBL_ADDR);
	if (res < 0)
		return res;

	res &= OQO_O2_SMB0_WWAN_DSBL_MASK;

	if (res)
		*state = RFKILL_STATE_SOFT_BLOCKED;
	else
		*state = RFKILL_STATE_UNBLOCKED;

	return 0;
}

static int oqo_rfkill_toggle(void *data, enum rfkill_state state)
{
	int res;

	res = smread_u8(OQO_O2_SMB0_WWAN_DSBL_ADDR);

	if (state == RFKILL_STATE_UNBLOCKED)
		res &= ~OQO_O2_SMB0_WWAN_DSBL_MASK;
	else
		res |= OQO_O2_SMB0_WWAN_DSBL_MASK;

	return smwrite_u8(OQO_O2_SMB0_WWAN_DSBL_ADDR, res);
}

static int __devinit oqo_rfkill_init(struct device *dev)
{
	int res;

	oqo_rfkill = rfkill_allocate(dev, RFKILL_TYPE_WWAN);
	if (!oqo_rfkill)
		return -ENODEV;

	res = smread_u8(OQO_O2_SMB0_WWAN_DSBL_ADDR);
	res &= OQO_O2_SMB0_WWAN_DSBL_MASK;

	oqo_rfkill->name = "oqo-wwan";
	if (res)
		oqo_rfkill->state = RFKILL_STATE_SOFT_BLOCKED;
	else
		oqo_rfkill->state = RFKILL_STATE_UNBLOCKED;

	oqo_rfkill->get_state = oqo_rfkill_get;
	oqo_rfkill->toggle_radio = oqo_rfkill_toggle;
	oqo_rfkill->user_claim_unsupported = 1;

	res = rfkill_register(oqo_rfkill);

	if (res)
		rfkill_free(oqo_rfkill);

	return res;
}

static void __devexit oqo_rfkill_fini(void)
{
	if (!oqo_rfkill)
		return;
	rfkill_unregister(oqo_rfkill);
}

/*
 * Platform device
 */

static int __devinit oqo_platform_probe(struct platform_device *device)
{
	int err;
	int i;
	char *troubleok = "trouble, but continuing.\n";

	memset(oqo_sn, 0, OQO_O2_SMB0_SERIAL_LEN + 1);
	for (i = 0; i < OQO_O2_SMB0_SERIAL_LEN; i++) {
		err = oqo_smbus_getb(OQO_O2_SMB0_SERIAL_START + i, oqo_sn + i);
		if (err) {
			printk(OQO_ERR "Serial number check failed.\n");
			return err;
		}
	}
	printk(OQO_INFO "Found OQO with serial number %s.\n", oqo_sn);

	err = oqo_backlight_init(&device->dev);
	if (err)
		printk(OQO_ERR "Backlight init %s", troubleok);

	err = oqo_rfkill_init(&device->dev);
	if (err)
		printk(OQO_ERR "RFKill init %s", troubleok);

	/* LID does not work at all yet, and this may be taken
	   care of by ACPI.
	 */
	orig.lid_wakes = smread_u8(OQO_O2_SMB0_LID_WAKES_ADDR);
	orig.lid_wakes &= OQO_O2_SMB0_LID_WAKES_MASK;
	orig.lid_wakes = curr.lid_wakes = !!orig.lid_wakes;
	if (orig.lid_wakes < 0) {
		printk(OQO_ERR "Wake on LID event %s", troubleok);
	} else {
		printk(OQO_INFO "Wake on LID is %s.\n",
		       (orig.lid_wakes ? "on" : "off"));
	}

	err = oqo_kine_init();
	return err;
}

static int oqo_platform_remove(struct platform_device *device)
{
	oqo_backlight_fini();
	oqo_rfkill_fini();
	oqo_kine_fini();

	return 0;
}

#ifdef CONFIG_PM

static int oqo_platform_suspend(struct platform_device *dev, pm_message_t state)
{
	if (!interface)
		return -ENOMEM;

	/* This sticks during boot so do not turn it entirely off */
	if (oqo_backlight_device) {
		curr.bl_bright = read_brightness(oqo_backlight_device);
		smwrite_s16(OQO_O2_SMB0_BL_HI, OQO_O2_SMB0_BL_LO, 256);
	}
	return 0;
}

static int oqo_platform_resume(struct platform_device *device)
{
	if (!interface)
		return -ENOMEM;

	if (oqo_backlight_device) {
		smwrite_s16(OQO_O2_SMB0_BL_HI,
			    OQO_O2_SMB0_BL_LO, curr.bl_bright);
	}

	return 0;
}

#else
#define oqo_platform_suspend NULL
#define oqo_platform_resume  NULL
#endif

static struct platform_driver oqo_platform_driver = {
	.driver = {
		   .name = "oqo-wmi",
		   .owner = THIS_MODULE,
		   },
	.probe = oqo_platform_probe,
	.remove = oqo_platform_remove,
	.suspend = oqo_platform_suspend,
	.resume = oqo_platform_resume,
};

static int __init oqo_wmi_init(void)
{
	int err;

	dmi_check_system(oqo_dmis);

	if (!interface && force) {
		model = oqo_models;
		if (wmi_has_guid(OQO_O2_AMW0_GUID))
			interface = &AMW0_interface;
	}

	if (!interface) {
		printk(OQO_ERR "No or unsupported WMI interface. Aborting.\n");
		printk(OQO_ERR "Hint: Get dmidecode working and try again.\n");
		printk(OQO_ERR "(Check \"System Management BIOS\" in BIOS)\n");
		if (!force)
			printk(OQO_ERR "Use the force option to skip DMI"
			       " checking\n");
		return -ENODEV;
	}

	err = platform_driver_register(&oqo_platform_driver);
	if (err) {
		printk(OQO_ERR "platform_driver_register gave %d.\n", err);
		goto bail0;
	}

	oqo_platform_device = platform_device_alloc("oqo-wmi", -1);
	if (!oqo_platform_device) {
		printk(OQO_ERR "Could not allocate platform device.\n");
		err = -ENOMEM;
		goto bail1;
	}

	err = platform_device_add(oqo_platform_device);
	if (err) {
		printk(OQO_ERR "platform_device_add gave %d.\n", err);
		platform_device_put(oqo_platform_device);
		goto bail1;
	}

	return 0;

bail1:
	platform_driver_unregister(&oqo_platform_driver);
bail0:
	return err;
}

static void __exit oqo_wmi_fini(void)
{
	platform_device_del(oqo_platform_device);
	platform_driver_unregister(&oqo_platform_driver);

	return;
}

module_init(oqo_wmi_init);
module_exit(oqo_wmi_fini);
