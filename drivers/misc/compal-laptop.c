/*-*-linux-c-*-*/

/*
  Copyright (C) 2008 Cezary Jackiewicz <cezary.jackiewicz (at) gmail.com>

  based on MSI driver

  Copyright (C) 2006 Lennart Poettering <mzxreary (at) 0pointer (dot) de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 */

/*
 * compal-laptop.c - Compal laptop support.
 *
 * This driver registers itself in the Linux backlight control subsystem
 * and rfkill switch subsystem.
 *
 * This driver might work on other laptops produced by Compal. If you
 * want to try it you can pass force=1 as argument to the module which
 * will force it to load even when the DMI data doesn't identify the
 * laptop as FL9x.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/autoconf.h>
#include <linux/rfkill.h>

#define COMPAL_DRIVER_VERSION "0.3.0"
#define COMPAL_DRIVER_NAME "compal-laptop"
#define COMPAL_ERR KERN_ERR COMPAL_DRIVER_NAME ": "
#define COMPAL_INFO KERN_INFO COMPAL_DRIVER_NAME ": "

#define COMPAL_LCD_LEVEL_MAX 8

#define COMPAL_EC_COMMAND_WIRELESS 0xBB
#define COMPAL_EC_COMMAND_LCD_LEVEL 0xB9

#define KILLSWITCH_MASK 0x10
#define WLAN_MASK	0x01
#define BT_MASK 	0x02

/* rfkill switches */
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wlan_rfkill;

static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force driver load, ignore DMI data");

/* Hardware access */

static int set_lcd_level(int level)
{
	if (level < 0 || level >= COMPAL_LCD_LEVEL_MAX)
		return -EINVAL;

	ec_write(COMPAL_EC_COMMAND_LCD_LEVEL, level);

	return 0;
}

static int get_lcd_level(void)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_LCD_LEVEL, &result);

	return (int) result;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	return get_lcd_level();
}


static int bl_update_status(struct backlight_device *b)
{
	return set_lcd_level(b->props.brightness);
}

static struct backlight_ops compalbl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status	= bl_update_status,
};

static struct backlight_device *compalbl_device;

/* Platform device */

static struct platform_driver compal_driver = {
	.driver = {
		.name = COMPAL_DRIVER_NAME,
		.owner = THIS_MODULE,
	}
};

static struct platform_device *compal_device;

/* rfkill stuff */

static int wlan_rfk_set(void *data, enum rfkill_state state)
{
	u8 result, value;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		return 0;
	else {
		if (state == RFKILL_STATE_ON)
			value = (u8) (result | WLAN_MASK);
		else
			value = (u8) (result & ~WLAN_MASK);
		ec_write(COMPAL_EC_COMMAND_WIRELESS, value);
	}

	return 0;
}

static int wlan_rfk_get(void *data, enum rfkill_state *state)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		*state = RFKILL_STATE_OFF;
	else
		*state = result & WLAN_MASK;

	return 0;
}

static int bluetooth_rfk_set(void *data, enum rfkill_state state)
{
	u8 result, value;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		return 0;
	else {
		if (state == RFKILL_STATE_ON)
			value = (u8) (result | BT_MASK);
		else
			value = (u8) (result & ~BT_MASK);
		ec_write(COMPAL_EC_COMMAND_WIRELESS, value);
	}

	return 0;
}

static int bluetooth_rfk_get(void *data, enum rfkill_state *state)
{
	u8 result;

	ec_read(COMPAL_EC_COMMAND_WIRELESS, &result);

	if ((result & KILLSWITCH_MASK) == 0)
		*state = RFKILL_STATE_OFF;
	else
		*state = (result & BT_MASK) >> 1;

	return 0;
}

static int __init compal_rfkill(struct rfkill **rfk,
				const enum rfkill_type rfktype,
				const char *name,
				int (*toggle_radio)(void *, enum rfkill_state),
				int (*get_state)(void *, enum rfkill_state *))
{
	int res;

	(*rfk) = rfkill_allocate(&compal_device->dev, rfktype);
	if (!*rfk) {
		printk(COMPAL_ERR
				"failed to allocate memory for rfkill class\n");
		return -ENOMEM;
	}

	(*rfk)->name = name;
	(*rfk)->get_state = get_state;
	(*rfk)->toggle_radio = toggle_radio;
	(*rfk)->user_claim_unsupported = 1;

	res = rfkill_register(*rfk);
	if (res < 0) {
		printk(COMPAL_ERR
				"failed to register %s rfkill switch: %d\n",
				name, res);
		rfkill_free(*rfk);
		*rfk = NULL;
		return res;
	}

	return 0;
}

/* Initialization */

static int dmi_check_cb(const struct dmi_system_id *id)
{
	printk(COMPAL_INFO "Identified laptop model '%s'.\n",
		id->ident);

	return 0;
}

static struct dmi_system_id __initdata compal_dmi_table[] = {
	{
		.ident = "FL90/IFL90",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL90"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL90/IFL90",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL90"),
			DMI_MATCH(DMI_BOARD_VERSION, "REFERENCE"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL91/IFL91",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFL91"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FL92/JFL92",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "JFL92"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "FT00/IFT00",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IFT00"),
			DMI_MATCH(DMI_BOARD_VERSION, "IFT00"),
		},
		.callback = dmi_check_cb
	},
	{ }
};

static int __init compal_init(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	if (!force && !dmi_check_system(compal_dmi_table))
		return -ENODEV;

	/* Register backlight stuff */

	if (!acpi_video_backlight_support()) {
		compalbl_device = backlight_device_register(COMPAL_DRIVER_NAME, NULL, NULL,
							    &compalbl_ops);
		if (IS_ERR(compalbl_device))
			return PTR_ERR(compalbl_device);

		compalbl_device->props.max_brightness = COMPAL_LCD_LEVEL_MAX-1;
	}

	ret = platform_driver_register(&compal_driver);
	if (ret)
		goto fail_backlight;

	/* Register platform stuff */

	compal_device = platform_device_alloc(COMPAL_DRIVER_NAME, -1);
	if (!compal_device) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(compal_device);
	if (ret)
		goto fail_platform_device;

	/* Register rfkill stuff */

	compal_rfkill(&wlan_rfkill,
					RFKILL_TYPE_WLAN,
					"compal_laptop_wlan_sw",
					wlan_rfk_set,
					wlan_rfk_get);

	compal_rfkill(&bluetooth_rfkill,
					RFKILL_TYPE_BLUETOOTH,
					"compal_laptop_bluetooth_sw",
					bluetooth_rfk_set,
					bluetooth_rfk_get);

	printk(COMPAL_INFO "driver "COMPAL_DRIVER_VERSION
			" successfully loaded.\n");

	return 0;

fail_platform_device:

	platform_device_put(compal_device);

fail_platform_driver:

	platform_driver_unregister(&compal_driver);

fail_backlight:

	backlight_device_unregister(compalbl_device);

	return ret;
}

static void __exit compal_cleanup(void)
{
	if (bluetooth_rfkill) {
		rfkill_unregister(bluetooth_rfkill);
		bluetooth_rfkill = NULL;
	}

	if (wlan_rfkill) {
		rfkill_unregister(wlan_rfkill);
		wlan_rfkill = NULL;
	}

	platform_device_unregister(compal_device);
	platform_driver_unregister(&compal_driver);
	backlight_device_unregister(compalbl_device);

	printk(COMPAL_INFO "driver unloaded.\n");
}

module_init(compal_init);
module_exit(compal_cleanup);

MODULE_AUTHOR("Cezary Jackiewicz");
MODULE_DESCRIPTION("Compal Laptop Support");
MODULE_VERSION(COMPAL_DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_ALIAS("dmi:*:rnIFL90:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnIFL90:rvrREFERENCE:*");
MODULE_ALIAS("dmi:*:rnIFL91:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnJFL92:rvrIFT00:*");
MODULE_ALIAS("dmi:*:rnIFT00:rvrIFT00:*");
