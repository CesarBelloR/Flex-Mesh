#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <usb/usb_device.h>
#include <ctype.h>
#include <device.h>
#include <drivers/flash.h>
#include <drivers/gpio.h>
#include <jesd216.h>
#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ETC test version %s", CONFIG_TEST_VERSION);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_version, NULL, "Show kernel version", cmd_version, 1, 0);

static int cmd_adc_request(const struct shell *shell, size_t argc, char **argv)
{
	int channel = atoi(argv[1]);
	shell_print(shell, "ADC Channel %d - Value %d", channel, adc_get_channel(channel));

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc, NULL, "Get ADC raw data", cmd_adc_request, 2, 0);

static int cmd_ds18b20_request(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ds18b20_init();
	k_sleep(K_SECONDS(1));
	if (ds18b20_manualconvert()) {
		shell_print(shell, "DS18B20 return %d", (int)ds18b20[0].temperature);
	} else {
		shell_print(shell, "DS18B20 error");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ds18b20, NULL, "Get DS18B20 temperature sensor", cmd_ds18b20_request, 1, 0);

static int cmd_ui_request(const struct shell *shell, size_t argc, char **argv)
{
	int pattern = atoi(argv[1]);
	shell_print(shell, "Set UI color %d", pattern);
	ui_led_set_pattern((enum ui_led_pattern)pattern);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ui, NULL, "Set color UI", cmd_ui_request, 2, 0);

static int cmd_pcf85263_set_time(const struct shell *shell, size_t argc, char **argv)
{
	int utc_time = atoi(argv[1]);
	shell_print(shell, "Set time UTC %d", utc_time);
	pcf85263a_rtc_set_time(utc_time);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_set_time, NULL, "Set time in PCF85263", cmd_pcf85263_set_time, 2, 0);

static int cmd_pcf85263_get_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	time_t utc_time = 0;
	pcf85263a_rtc_get_time(&utc_time);
	shell_print(shell, "Get time UTC %d", utc_time);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_get_time, NULL, "Get time in PCF85263", cmd_pcf85263_get_time, 1, 0);

#if DT_HAS_COMPAT_STATUS_OKAY(jedec_spi_nor)
#define FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(jedec_spi_nor)
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_qspi_nor)
#define FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_qspi_nor)
#else
#error Unsupported flash driver
#define FLASH_NODE DT_INVALID_NODE
#endif

static int cmd_external_flash_get_info(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	const struct device *dev = DEVICE_DT_GET(FLASH_NODE);

	if (!device_is_ready(dev)) {
		shell_error(shell, "%s: device not ready", dev->name);
		return 0;
	}

	uint8_t id[3] = {0x00};
	int rc = flash_read_jedec_id(dev, id);
	if (rc == 0) {
		shell_print(shell, "jedec-id = [%02x %02x %02x];\n",
		       id[0], id[1], id[2]);
	} else {
		shell_error(shell, "JEDEC ID read failed: %d", rc);
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_flash_info, NULL, "Get information of external flash", cmd_external_flash_get_info, 1, 0);

static int cmd_charge_module(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = bq24195_init();
	if (ret) {
		shell_print(shell, "BQ24195 error");
	} else {
		shell_print(shell, "BQ24195 passed");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_charge, NULL, "Check BQ24195 IC", cmd_charge_module, 1, 0);

#define GPIO_USER_BUTTON_PIN 5
static int cmd_button_pull_module(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	const struct device *dev = device_get_binding("GPIO_1");
	if (dev == NULL) {
		shell_print(shell, "Can't get GPIO_1 for button");
		return 0;
	} else {
		gpio_pin_configure(dev, GPIO_USER_BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);
		k_usleep(50);
		uint32_t t0 = k_uptime_get_32();
		while(k_uptime_get_32() - t0 < 10000) {
			int btn_status = gpio_pin_get(dev, GPIO_USER_BUTTON_PIN);
			if (btn_status == 0) {
				shell_print(shell, "Button User pressed");
				break;
			}
		}
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_button_user, NULL, "Check Button User", cmd_button_pull_module, 1, 0);
