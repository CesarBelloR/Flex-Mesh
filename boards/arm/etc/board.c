#include <zephyr.h>
#include <init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <devicetree.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

#define VSENS_EN_PIN 23
#define BQ24195_I2C_7BIT_ADDR (0x6B)

static int init(const struct device *dev)
{
	const struct device *gpio_0_dev = device_get_binding("GPIO_0");
	if (gpio_0_dev == NULL) {
		return -EINVAL;
	}

	gpio_pin_configure(gpio_0_dev, VSENS_EN_PIN, GPIO_OUTPUT_ACTIVE);

	k_msleep(50);
	return 0;
}

SYS_INIT(init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
