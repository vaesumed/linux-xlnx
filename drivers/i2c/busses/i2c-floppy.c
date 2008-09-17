/* ------------------------------------------------------------------------ *
 * i2c-floppy.c I2C bus over floppy controller                              *
 * ------------------------------------------------------------------------ *
   Copyright (C) 2008 Herbert Poetzl <herbert@13thfloor.at>

   Somewhat based on i2c-parport-light.c driver
   Copyright (C) 2003-2007 Jean Delvare <khali@linux-fr.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ------------------------------------------------------------------------ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/io.h>

static struct platform_device *pdev;
static unsigned char dor;

static u16 base;
module_param(base, ushort, 0);
MODULE_PARM_DESC(base, "Base I/O address");


#define DEFAULT_BASE	0x3F0		/* for PC style hardware */
#define DRVNAME		"i2c-floppy"


#define FOFF_DOR	0x02
#define FOFF_DIR	0x07

#define FDOR_MOTEA	0x10
#define FDOR_MOTEB	0x20

#define FDIR_DCHNG	0x80

#define SCL		FDOR_MOTEA
#define SDA		FDOR_MOTEB
#define SDA_IN		FDIR_DCHNG

#define LO_INV		(SDA|SCL)
#define LI_INV		(SDA_IN)


/* ----- Low-level floppy access ------------------------------------------ */

static inline void port_dor_out(unsigned char d)
{
	outb(d ^ LO_INV, base + FOFF_DOR);
}

static inline unsigned char port_dir_in(void)
{
	return inb(base + FOFF_DIR) ^ LI_INV;
}


/* ----- I2C algorithm call-back functions and structures ----------------- */

static void floppy_setscl(void *data, int state)
{
	if (state)
		dor |= SCL;
	else
		dor &= ~SCL;

	port_dor_out(dor);
}

static void floppy_setsda(void *data, int state)
{
	if (state)
		dor |= SDA;
	else
		dor &= ~SDA;

	port_dor_out(dor);
}

static int floppy_getsda(void *data)
{
	return port_dir_in() & SDA_IN;
}

/* Encapsulate the functions above in the correct structure
   Note that getscl is set to NULL because SCL cannot be read
   back with the current driver */
static struct i2c_algo_bit_data floppy_algo_data = {
	.setsda		= floppy_setsda,
	.setscl		= floppy_setscl,
	.getsda		= floppy_getsda,
	.udelay		= 50,
	.timeout	= HZ,
};


/* ----- Driver registration ---------------------------------------------- */

static struct i2c_adapter floppy_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.algo_data	= &floppy_algo_data,
	.name		= "Floppy controller adapter",
};

static int __devinit i2c_floppy_probe(struct platform_device *pdev)
{
	int err;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, res->end - res->start + 1, DRVNAME))
		return -EBUSY;

	/* Reset hardware to a sane state (SCL and SDA high) */
	floppy_setsda(NULL, 1);
	floppy_setscl(NULL, 1);

	floppy_adapter.dev.parent = &pdev->dev;
	err = i2c_bit_add_bus(&floppy_adapter);
	if (err) {
		dev_err(&pdev->dev, "Unable to register with I2C\n");
		goto exit_region;
	}
	return 0;

exit_region:
	release_region(res->start, res->end - res->start + 1);
	return err;
}

static int __devexit i2c_floppy_remove(struct platform_device *pdev)
{
	struct resource *res;

	i2c_del_adapter(&floppy_adapter);

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start, res->end - res->start + 1);
	return 0;
}

static struct platform_driver i2c_floppy_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= i2c_floppy_probe,
	.remove		= __devexit_p(i2c_floppy_remove),
};

static int __init i2c_floppy_device_add(u16 address)
{
	struct resource res = {
		.start	= address,
		.end	= address + 7,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, -1);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

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

static int __init i2c_floppy_init(void)
{
	int err;

	if (base == 0) {
		pr_info(DRVNAME ": using default base 0x%x\n", DEFAULT_BASE);
		base = DEFAULT_BASE;
	}

	/* Sets global pdev as a side effect */
	err = i2c_floppy_device_add(base);
	if (err)
		goto exit;

	err = platform_driver_register(&i2c_floppy_driver);
	if (err)
		goto exit_device;

	return 0;

exit_device:
	platform_device_unregister(pdev);
exit:
	return err;
}

static void __exit i2c_floppy_exit(void)
{
	platform_driver_unregister(&i2c_floppy_driver);
	platform_device_unregister(pdev);
}


MODULE_AUTHOR("Herbert Poetzl <herbert@13thfloor.at>");
MODULE_DESCRIPTION("I2C bus over floppy controller");
MODULE_LICENSE("GPL");

module_init(i2c_floppy_init);
module_exit(i2c_floppy_exit);
