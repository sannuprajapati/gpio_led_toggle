// SPDX-License-Identifier: GPL-2.0
/*
 * gpio_demo - GPIO consumer driver: button toggles LED (RPi 3B+)
 *
 *   LED    : GPIO17 (pin 11)  -> "led-gpios"    in DT
 *   Button : GPIO27 (pin 13)  -> "button-gpios" in DT (active-low, pull-up)
 *
 * sysfs:  /sys/bus/platform/devices/gpio-demo/led     (rw: 0/1/on/off)
 *         /sys/bus/platform/devices/gpio-demo/button  (ro: 1 = pressed)
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/of.h>

#define DEBOUNCE_MS	200	/* min time between accepted presses */
#define SETTLE_MS	20	/* wait for contact bounce before sampling */

struct gpio_demo {
	struct device *dev;
	struct gpio_desc *led;
	struct gpio_desc *btn;
	struct mutex lock;	/* protects led_on, last_press */
	unsigned long last_press;
	unsigned int presses;
	bool led_on;
	int irq;
};

static void gpio_demo_set_led(struct gpio_demo *gd, bool on)
{
	gd->led_on = on;
	/* logical value: gpiolib applies active-low from DT */
	gpiod_set_value_cansleep(gd->led, on);
}

/* Threaded handler: process context, may sleep */
static irqreturn_t gpio_demo_btn_thread(int irq, void *data)
{
	struct gpio_demo *gd = data;

	msleep(SETTLE_MS);
	/* ignore glitches / release bounce: must still be pressed */
	if (gpiod_get_value_cansleep(gd->btn) != 1)
		return IRQ_HANDLED;

	mutex_lock(&gd->lock);
	if (time_before(jiffies, gd->last_press + msecs_to_jiffies(DEBOUNCE_MS))) {
		mutex_unlock(&gd->lock);
		return IRQ_HANDLED;
	}
	gd->last_press = jiffies;
	gd->presses++;
	gpio_demo_set_led(gd, !gd->led_on);
	dev_info(gd->dev, "button press #%u, LED %s\n",
		 gd->presses, gd->led_on ? "ON" : "OFF");
	mutex_unlock(&gd->lock);

	return IRQ_HANDLED;
}

/* ---------------- sysfs ---------------- */
static ssize_t led_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct gpio_demo *gd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", gd->led_on);
}

static ssize_t led_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct gpio_demo *gd = dev_get_drvdata(dev);
	bool on;
	int ret;

	ret = kstrtobool(buf, &on);
	if (ret)
		return ret;

	mutex_lock(&gd->lock);
	gpio_demo_set_led(gd, on);
	mutex_unlock(&gd->lock);

	return count;
}
static DEVICE_ATTR_RW(led);

static ssize_t button_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct gpio_demo *gd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", gpiod_get_value_cansleep(gd->btn));
}
static DEVICE_ATTR_RO(button);

static ssize_t presses_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct gpio_demo *gd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", gd->presses);
}
static DEVICE_ATTR_RO(presses);

static struct attribute *gpio_demo_attrs[] = {
	&dev_attr_led.attr,
	&dev_attr_button.attr,
	&dev_attr_presses.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpio_demo);

/* devm action: LED off on unbind (runs after the IRQ is freed) */
static void gpio_demo_led_off(void *data)
{
	struct gpio_demo *gd = data;

	gpiod_set_value_cansleep(gd->led, 0);
}

static int gpio_demo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_demo *gd;
	int ret;

	gd = devm_kzalloc(dev, sizeof(*gd), GFP_KERNEL);
	if (!gd)
		return -ENOMEM;

	gd->dev = dev;
	mutex_init(&gd->lock);
	/* so the very first press is accepted even right after boot */
	gd->last_press = jiffies - msecs_to_jiffies(DEBOUNCE_MS);
	platform_set_drvdata(pdev, gd);

	/* "led" -> "led-gpios" property */
	gd->led = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(gd->led))
		return dev_err_probe(dev, PTR_ERR(gd->led), "failed to get led gpio\n");

	ret = devm_add_action_or_reset(dev, gpio_demo_led_off, gd);
	if (ret)
		return ret;

	gd->btn = devm_gpiod_get(dev, "button", GPIOD_IN);
	if (IS_ERR(gd->btn))
		return dev_err_probe(dev, PTR_ERR(gd->btn), "failed to get button gpio\n");

	gd->irq = gpiod_to_irq(gd->btn);
	if (gd->irq < 0)
		return dev_err_probe(dev, gd->irq, "no IRQ for button gpio\n");

	/* edge is on the raw line: press pulls the pin to GND -> falling */
	ret = devm_request_threaded_irq(dev, gd->irq, NULL, gpio_demo_btn_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"gpio-demo-btn", gd);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ %d\n", gd->irq);

	dev_info(dev, "probed: LED + button ready (irq %d)\n", gd->irq);
	return 0;
}

static const struct of_device_id gpio_demo_of_match[] = {
	{ .compatible = "sannu,gpio-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_demo_of_match);

static struct platform_driver gpio_demo_driver = {
	.probe = gpio_demo_probe,
	.driver = {
		.name = "gpio-demo",
		.of_match_table = gpio_demo_of_match,
		.dev_groups = gpio_demo_groups,
	},
};
module_platform_driver(gpio_demo_driver);

MODULE_AUTHOR("Sannu");
MODULE_DESCRIPTION("GPIO consumer demo: button toggles LED via gpiod API");
MODULE_LICENSE("GPL");
