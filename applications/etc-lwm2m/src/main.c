#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <ctype.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_LWM2M_LOG_LEVEL);

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ETC test version %s", CONFIG_TEST_VERSION);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_version, NULL, "Show kernel version", cmd_version, 1, 0);

void main(void)
{
	printk("Starting...");
}
