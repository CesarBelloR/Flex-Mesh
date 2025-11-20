#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>

#include "data/etc_cape.h"
#include "lora_listener.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_LORA_LISTENER_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

char key[] = "ElL10TaC4T";

void main(void)
{
	etc_cape_init(key, 10, 0);
	etc_cape_set_key(key, 10);
	uint32_t dtr = 0;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}
	console_init();
	console_write(NULL, "Starting LoRa listener...\r\n", strlen("Starting LoRa listener...\r\n"));

	lora_receive();

	while(1) {
		k_sleep(K_SECONDS(1));
	}
}
