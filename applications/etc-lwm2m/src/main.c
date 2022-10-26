#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <ctype.h>
#include "bq24195.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_LWM2M_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ETC test version %s", CONFIG_TEST_VERSION);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_version, NULL, "Show kernel version", cmd_version, 1, 0);

static void app_driver_init(void) {
	bq24195_init();
}

void main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	const struct device *dev_bg95 = DEVICE_DT_GET_ONE(quectel_bg95);

	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}
	
	app_driver_init();

	while (!device_is_ready(dev_bg95)) {
		k_sleep(K_MSEC(100));
	}

	printk("Starting...");
}
