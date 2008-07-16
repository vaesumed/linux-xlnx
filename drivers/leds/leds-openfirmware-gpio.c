/*
 * OpenFirmware bindings for GPIO connected LEDs
 *
 * Copyright (C) 2007 8D Technologies inc.
 * Raphael Assenat <raph@8d.com>
 * Copyright (C) 2008 MontaVista Software, Inc.
 * Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <asm/prom.h>

struct of_gpio_led {
	struct device_node *np;
	struct led_classdev cdev;
	unsigned int gpio;
	struct work_struct work;
	u8 new_level;
	u8 can_sleep;
};

static void gpio_led_work(struct work_struct *work)
{
	struct of_gpio_led *led = container_of(work, struct of_gpio_led, work);

	gpio_set_value_cansleep(led->gpio, led->new_level);
}

static void gpio_led_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	struct of_gpio_led *led = container_of(led_cdev, struct of_gpio_led,
					       cdev);
	int level;

	if (value == LED_OFF)
		level = 0;
	else
		level = 1;

	/* Setting GPIOs with I2C/etc requires a task context, and we don't
	 * seem to have a reliable way to know if we're already in one; so
	 * let's just assume the worst.
	 */
	if (led->can_sleep) {
		led->new_level = level;
		schedule_work(&led->work);
	} else {
		gpio_set_value(led->gpio, level);
	}
}

static int __devinit of_gpio_leds_probe(struct of_device *ofdev,
					const struct of_device_id *match)
{
	int ret;
	struct of_gpio_led *led;
	struct device_node *np = ofdev->node;

	led = kzalloc(sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->np = of_node_get(np);

	ret = of_get_gpio(np, 0);
	if (!gpio_is_valid(ret)) {
		dev_err(&ofdev->dev, "gpio is invalid\n");
		goto err_get_gpio;
	}
	led->gpio = ret;
	led->can_sleep = gpio_cansleep(led->gpio);

	led->cdev.name = of_get_property(np, "label", NULL);
	if (!led->cdev.name)
		led->cdev.name = dev_name(&ofdev->dev);
	led->cdev.brightness_set = gpio_led_set;

	ret = gpio_request(led->gpio, dev_name(&ofdev->dev));
	if (ret < 0) {
		dev_err(&ofdev->dev, "could not request gpio, status is %d\n",
			ret);
		goto err_gpio;
	}

	ret = gpio_direction_output(led->gpio, 0);
	if (ret) {
		dev_err(&ofdev->dev, "gpio could not be an output, "
			"status is %d\n", ret);
		goto err_gpio;
	}

	INIT_WORK(&led->work, gpio_led_work);
	dev_set_drvdata(&ofdev->dev, led);

	ret = led_classdev_register(&ofdev->dev, &led->cdev);
	if (ret < 0) {
		dev_err(&ofdev->dev, "could register led cdev, status is %d\n",
			ret);
		gpio_free(led->gpio);
		goto err_reg_cdev;
	}

	return 0;

err_reg_cdev:
	cancel_work_sync(&led->work);
err_gpio:
	gpio_free(led->gpio);
err_get_gpio:
	of_node_put(led->np);
	kfree(led);
	return ret;
}

static int __devexit of_gpio_leds_remove(struct of_device *ofdev)
{
	struct of_gpio_led *led = dev_get_drvdata(&ofdev->dev);

	led_classdev_unregister(&led->cdev);
	cancel_work_sync(&led->work);
	gpio_free(led->gpio);
	of_node_put(led->np);
	kfree(led);

	return 0;
}

#ifdef CONFIG_PM
static int of_gpio_led_suspend(struct of_device *ofdev, pm_message_t state)
{
	struct of_gpio_led *led = dev_get_drvdata(&ofdev->dev);

	led_classdev_suspend(&led->cdev);
	return 0;
}

static int of_gpio_led_resume(struct of_device *ofdev)
{
	struct of_gpio_led *led = dev_get_drvdata(&ofdev->dev);

	led_classdev_resume(&led->cdev);
	return 0;
}
#else
#define of_gpio_led_suspend NULL
#define of_gpio_led_resume NULL
#endif /* CONFIG_PM */

static const struct of_device_id of_gpio_leds_match[] = {
	{ .compatible = "gpio-led", },
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_leds_match);

static struct of_platform_driver of_gpio_leds_driver = {
	.driver = {
		.name = "of_gpio_leds",
		.owner = THIS_MODULE,
	},
	.match_table = of_gpio_leds_match,
	.probe = of_gpio_leds_probe,
	.remove = __devexit_p(of_gpio_leds_remove),
	.suspend = of_gpio_led_suspend,
	.resume = of_gpio_led_resume,
};

static int __init of_gpio_leds_init(void)
{
	return of_register_platform_driver(&of_gpio_leds_driver);
}
module_init(of_gpio_leds_init);

static void __exit of_gpio_leds_exit(void)
{
	of_unregister_platform_driver(&of_gpio_leds_driver);
}
module_exit(of_gpio_leds_exit);

MODULE_DESCRIPTION("OpenFirmware bindings for GPIO connected LEDs");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
