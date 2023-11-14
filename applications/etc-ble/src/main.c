#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "flex_ble.h"

int main(void)
{
#if defined(CONFIG_USB_DEVICE_STACK)
	uint32_t dtr = 0;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return 0;
	}

	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
#endif
	LOG_INF("Flex BLE application");
	int rc = etc_ble_init();
	if (rc) {
		LOG_ERR("Failed to initialize the BLE");
		return rc;
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
