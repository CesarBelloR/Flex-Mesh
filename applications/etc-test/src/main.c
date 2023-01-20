#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <ctype.h>

#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_TEST_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

#define LTE_POWER_ON_OFF_PIN 4
#define LTE_PSM_IND_PIN 2
#define LTE_POWER_KEY_PIN 1
#define LTE_POWER_PON_TRIG 23

const struct device * gpio_0 = NULL;
const struct device * gpio_1 = NULL;

static void app_modem_init(void) {
	gpio_0 = device_get_binding("GPIO_0");
	gpio_1 = device_get_binding("GPIO_1");

	if (!device_is_ready(gpio_0)) {
		LOG_ERR("GPIO 0 is not ready");
		return;
	}

	if (!device_is_ready(gpio_1)) {
		LOG_ERR("GPIO 1 is not ready");
		return;
	}
	
	gpio_pin_configure(gpio_1, LTE_POWER_KEY_PIN, GPIO_OUTPUT);
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 0U);
	k_sleep(K_MSEC(500));
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 1U);
	k_sleep(K_MSEC(1000));
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 0U);
	k_sleep(K_MSEC(2500));
	LOG_INF("IO Done");
}

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
