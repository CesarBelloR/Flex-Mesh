#include <zephyr.h>
#include <init.h>
#include <drivers/gpio.h>
#include <devicetree.h>

#define VSENS_EN_PIN 23

static int init(const struct device *dev)
{
	const struct device* gpio_0_dev = device_get_binding("GPIO_0");
	if (gpio_0_dev == NULL) {
		return -EINVAL;
	}

    gpio_pin_configure(gpio_0_dev, VSENS_EN_PIN, GPIO_OUTPUT_ACTIVE);
	return 0;
}

SYS_INIT(init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
