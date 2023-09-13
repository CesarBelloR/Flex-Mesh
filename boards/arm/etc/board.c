#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

static const struct gpio_dt_spec vsen_en_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);

static int init(void)
{
	if (!device_is_ready(vsen_en_dt.port)) {
		return -EINVAL;
	}
	return 0;
}

SYS_INIT(init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
