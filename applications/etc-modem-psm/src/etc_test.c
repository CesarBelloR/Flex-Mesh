#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <drivers/lora.h>
#include <usb/usb_device.h>
#include <ctype.h>
#include <device.h>
#include <drivers/gpio.h>

#include "modem_api.h"


#include <logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_MODEM_PSM_LOG_LEVEL);


static int cmd_psm_enable(const struct shell *shell, size_t argc, char **argv) 
{
	int ret;
	
	ret = quectel_bg95_psm(true);
	if (ret < 0) {
		shell_print(shell, "Error requesting PSM.");
	}

	return ret;
}

static int cmd_psm_disable(const struct shell *shell, size_t argc, char **argv) 
{
	int ret;
	
	ret = quectel_bg95_psm(false);
	if (ret < 0) {
		shell_print(shell, "Error disabling PSM.");
	}

	return ret;
}

static int cmd_psm_get_timers(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	
	ret = quectel_bg95_get_psm_timers();
	if (ret < 0) {
		shell_print(shell, "Error requesting PSM timers");
	}

	return ret;
}

static int cmd_psm_request_wakeup(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	
	ret = quectel_bg95_psm_wakeup();
	if (ret < 0) {
		shell_print(shell, "Error requesting PSM wakeup");
	}

	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	psm_sub,
	SHELL_CMD(enable, NULL, "Enter PSM", cmd_psm_enable),
	SHELL_CMD(disable, NULL, "Disable PSM", cmd_psm_disable),
	SHELL_CMD(get, NULL, "Retrieve PSM Timers", cmd_psm_get_timers),
	SHELL_CMD(wakeup, NULL, "Wake up from PSM", cmd_psm_request_wakeup),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(psm, &psm_sub, "Modem PSM test commands", NULL);