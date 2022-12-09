#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <version.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <ctype.h>

#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_TEST_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

static void app_driver_init(void) {
	extern void etc_test_init(void);
	etc_test_init();
	ui_init();
	adc_init();
	pcf85263a_init("I2C_0");
}

char key[] = "ElL10TaC4T";

void main(void)
{
	etc_cape_init(key, 10, 0);
	etc_cape_set_key(key, 10); 
	uint32_t dtr = 0;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}

	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	app_driver_init();
	while(1) {
		k_sleep(K_MSEC(100));
	}
}
