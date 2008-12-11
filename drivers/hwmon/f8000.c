/*
 * f8000.c - driver for the Asus F8000 Super-I/O chip integrated hardware
 *           monitoring features
 * Copyright (C) 2008  Jean Delvare <khali@linux-fr.org>
 *
 * The F8000 was made by Fintek for Asus.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/acpi.h>

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static struct platform_device *pdev;

#define DRVNAME "f8000"

/*
 * Super-I/O constants and functions
 */

#define F8000_LD_HWM		0x04

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934
#define SIO_F8000_ID		0x0581

static inline int
superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int
superio_inw(int base, int reg)
{
	int val;
	outb(reg++, base);
	val = inb(base + 1) << 8;
	outb(reg, base);
	val |= inb(base + 1);
	return val;
}

static inline void
superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void
superio_enter(int base)
{
	outb(0x87, base);
	outb(0x87, base);
}

static inline void
superio_exit(int base)
{
	outb(0xaa, base);
}

/*
 * ISA constants
 */

#define REGION_LENGTH		8
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

/*
 * Registers
 */

#define F8000_REG_CONFIG		0x01
/* in nr from 0 to 2 (8-bit values) */
#define F8000_REG_IN(nr)		(0x20 + (nr))
/* fan nr from 0 to 3 (12-bit values, two registers) */
#define F8000_REG_FAN(nr)		(0xa0 + 16 * (nr))
/* temp nr from 0 to 2 (8-bit values) */
#define F8000_REG_TEMP(nr)		(0x70 + 2 * (nr))
#define F8000_REG_TEMP_HIGH(nr)		(0x81 + 2 * (nr))
#define F8000_REG_TEMP_CRIT(nr)		(0x80 + 2 * (nr))

/*
 * Data structures and manipulation thereof
 */

struct f8000_data {
	unsigned short addr;
	const char *name;
	struct device *hwmon_dev;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_limits;	/* In jiffies */

	/* Register values */
	u8 in[3];
	u16 fan[4];
	s8 temp[3];
	s8 temp_high[3];
	s8 temp_crit[3];
};

/* 16 mV/bit */
static inline long in_from_reg(u8 reg)
{
	return reg * 16;
}

/* The 4 most significant bits are not used */
static inline long fan_from_reg(u16 reg)
{
	reg &= 0xfff;
	if (!reg || reg == 0xfff)
		return 0;
	return 1500000 / reg;
}

/* 1 degree C/bit */
static inline long temp_from_reg(s8 reg)
{
	return reg * 1000;
}

/*
 * Device I/O access
 */

/* Must be called with data->update_lock held, except during initialization */
static u8 f8000_read8(struct f8000_data *data, u8 reg)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	return inb(data->addr + DATA_REG_OFFSET);
}

/* It is important to read the MSB first, because doing so latches the
   value of the LSB, so we are sure both bytes belong to the same value.
   Must be called with data->update_lock held, except during initialization */
static u16 f8000_read16(struct f8000_data *data, u8 reg)
{
	u16 val;

	outb(reg, data->addr + ADDR_REG_OFFSET);
	val = inb(data->addr + DATA_REG_OFFSET) << 8;
	outb(++reg, data->addr + ADDR_REG_OFFSET);
	val |= inb(data->addr + DATA_REG_OFFSET);

	return val;
}

static struct f8000_data *f8000_update_device(struct device *dev)
{
	struct f8000_data *data = dev_get_drvdata(dev);
	int nr;

	mutex_lock(&data->update_lock);

	/* Limit registers cache is refreshed after 60 seconds */
	if (time_after(jiffies, data->last_updated + 60 * HZ)
	 || !data->valid) {
		for (nr = 0; nr < 3; nr++) {
			data->temp_high[nr] = f8000_read8(data,
					      F8000_REG_TEMP_HIGH(nr));
			data->temp_crit[nr] = f8000_read8(data,
					      F8000_REG_TEMP_CRIT(nr));
		}

		data->last_limits = jiffies;
	}

	/* Measurement registers cache is refreshed after 1 second */
	if (time_after(jiffies, data->last_updated + HZ)
	 || !data->valid) {
		for (nr = 0; nr < 3; nr++) {
			data->in[nr] = f8000_read8(data,
				       F8000_REG_IN(nr));
		}
		for (nr = 0; nr < 4; nr++) {
			data->fan[nr] = f8000_read16(data,
					F8000_REG_FAN(nr));
		}
		for (nr = 0; nr < 3; nr++) {
			data->temp[nr] = f8000_read8(data,
					 F8000_REG_TEMP(nr));
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs interface
 */

static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	struct f8000_data *data = f8000_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in_from_reg(data->in[nr]));
}

static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct f8000_data *data = f8000_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", fan_from_reg(data->fan[nr]));
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct f8000_data *data = f8000_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp[nr]));
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute
			     *devattr, char *buf)
{
	struct f8000_data *data = f8000_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp_high[nr]));
}

static ssize_t show_temp_crit(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	struct f8000_data *data = f8000_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp_crit[nr]));
}

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct f8000_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, show_in, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_in, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_in, NULL, 2);

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_max, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp_crit, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO, show_temp_max, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO, show_temp_crit, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO, show_temp_max, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_crit, S_IRUGO, show_temp_crit, NULL, 2);

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *f8000_fan_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	NULL
};

static struct attribute *f8000_in_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	NULL
};

static struct attribute *f8000_temp_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	NULL
};

static const struct attribute_group f8000_fan_group = {
	.attrs = f8000_fan_attributes,
};

static const struct attribute_group f8000_in_group = {
	.attrs = f8000_in_attributes,
};

static const struct attribute_group f8000_temp_group = {
	.attrs = f8000_temp_attributes,
};

/*
 * Device registration and initialization
 */

static int __devinit f8000_probe(struct platform_device *pdev)
{
	struct f8000_data *data;
	struct resource *res;
	int err;
	u8 config;

	data = kzalloc(sizeof(struct f8000_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Out of memory\n");
		goto exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start + ADDR_REG_OFFSET, 2, DRVNAME)) {
		err = -EBUSY;
		dev_err(&pdev->dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)(res->start + ADDR_REG_OFFSET),
			(unsigned long)(res->start + ADDR_REG_OFFSET + 1));
		goto exit_free;
	}
	data->addr = res->start;
	data->name = "f8000";
	mutex_init(&data->update_lock);

	platform_set_drvdata(pdev, data);

	/* Configuration check */
	config = f8000_read8(data, F8000_REG_CONFIG);
	if (config & BIT(2)) {
		err = -ENODEV;
		dev_warn(&pdev->dev, "Hardware monitor is powered down\n");
		goto exit_release_region;
	}
	if (!(config & (BIT(1) | BIT(0)))) {
		err = -ENODEV;
		dev_warn(&pdev->dev, "Monitoring is disabled\n");
		goto exit_release_region;
	}

	/* Register sysfs interface files */
	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto exit_release_region;

	if (config & BIT(1)) {
		dev_info(&pdev->dev, "Fan monitoring is %s\n", "enabled");
		err = sysfs_create_group(&pdev->dev.kobj, &f8000_fan_group);
		if (err)
			goto exit_remove_files;
	} else {
		dev_info(&pdev->dev, "Fan monitoring is %s\n", "disabled");
	}

	if (config & BIT(0)) {
		dev_info(&pdev->dev, "Temperature and voltage monitoring is "
			 "%s\n", "enabled");
		err = sysfs_create_group(&pdev->dev.kobj, &f8000_temp_group);
		if (err)
			goto exit_remove_files;
		err = sysfs_create_group(&pdev->dev.kobj, &f8000_in_group);
		if (err)
			goto exit_remove_files;
	} else {
		dev_info(&pdev->dev, "Temperature and voltage monitoring is "
			 "%s\n", "disabled");
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n", err);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&pdev->dev.kobj, &f8000_fan_group);
	sysfs_remove_group(&pdev->dev.kobj, &f8000_temp_group);
	sysfs_remove_group(&pdev->dev.kobj, &f8000_in_group);
	device_remove_file(&pdev->dev, &dev_attr_name);
exit_release_region:
	release_region(res->start + ADDR_REG_OFFSET, 2);
exit_free:
	platform_set_drvdata(pdev, NULL);
	kfree(data);
exit:
	return err;
}

static int __devexit f8000_remove(struct platform_device *pdev)
{
	struct f8000_data *data = platform_get_drvdata(pdev);
	struct resource *res;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &f8000_fan_group);
	sysfs_remove_group(&pdev->dev.kobj, &f8000_temp_group);
	sysfs_remove_group(&pdev->dev.kobj, &f8000_in_group);
	device_remove_file(&pdev->dev, &dev_attr_name);
	platform_set_drvdata(pdev, NULL);
	kfree(data);

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start + ADDR_REG_OFFSET, 2);

	return 0;
}

static struct platform_driver f8000_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= f8000_probe,
	.remove		= __devexit_p(f8000_remove),
};

static int __init f8000_device_add(unsigned short address)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

	res.name = pdev->name;
	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit_device_put;

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

 exit_device_put:
	platform_device_put(pdev);
 exit:
	return err;
}

static int __init f8000_find(int sioaddr, unsigned short *address)
{
	int err = -ENODEV;
	u16 devid;

	superio_enter(sioaddr);

	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID)
		goto exit;

	devid = force_id ? force_id : superio_inw(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_F8000_ID:
		break;
	default:
		printk(KERN_INFO DRVNAME ": Unsupported Fintek device, "
		       "skipping\n");
		goto exit;
	}

	superio_select(sioaddr, F8000_LD_HWM);
	if (!(superio_inb(sioaddr, SIO_REG_ENABLE) & 0x01)) {
		printk(KERN_WARNING DRVNAME ": Device not activated, "
		       "skipping\n");
		goto exit;
	}

	*address = superio_inw(sioaddr, SIO_REG_ADDR);
	if (*address == 0) {
		printk(KERN_WARNING DRVNAME ": Base address not set, "
		       "skipping\n");
		goto exit;
	}
	*address &= ~(REGION_LENGTH - 1);	/* Ignore 3 LSB */

	err = 0;
	printk(KERN_INFO DRVNAME ": Found F8000 chip at %#x\n", *address);

 exit:
	superio_exit(sioaddr);
	return err;
}

static int __init f8000_init(void)
{
	int err;
	unsigned short address;

	if (f8000_find(0x4e, &address)
	 && f8000_find(0x2e, &address))
		return -ENODEV;

	err = platform_driver_register(&f8000_driver);
	if (err)
		goto exit;

	/* Sets global pdev as a side effect */
	err = f8000_device_add(address);
	if (err)
		goto exit_driver;

	return 0;

 exit_driver:
	platform_driver_unregister(&f8000_driver);
 exit:
	return err;
}

static void __exit f8000_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&f8000_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8000 hardware monitoring driver");

module_init(f8000_init);
module_exit(f8000_exit);
