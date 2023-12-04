#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <version.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <ctype.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#endif
#ifdef CONFIG_MCUMGR_CMD_SHELL_MGMT
#include <zephyr/mgmt/mcumgr/grp/shell_mgmt/shell_mgmt.h>
#endif

#include "pcf85263a.h"
#include "adc.h"
#include "ds18b20.h"
#include "etc_cape.h"
#include "etc_device.h"
#include "watchdog.h"
#include "rtc_calib.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_TEST_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

static void app_driver_init(void) {
	pcf85263a_init("I2C_0");
	pcf85263a_set_interrupt_io(true);
	pcf85263a_set_clkpin(false);
	rtc_calib_init();
}

char key[] = "ElL10TaC4T";

void main(void)
{
	etc_device_nvs_init();
	etc_cape_init(key, 10, 0);
	etc_cape_set_key(key, 10); 
	uint32_t dtr = 0;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}

	app_driver_init();

	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	extern void etc_test_init(void);
	etc_test_init();
	while(1) {
		k_sleep(K_FOREVER);
	}
}
