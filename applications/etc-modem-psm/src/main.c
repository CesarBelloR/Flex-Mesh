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
LOG_MODULE_REGISTER(main, CONFIG_ETC_MODEM_PSM_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not ACM CDC UART device");

#define LTE_PSM_IND_PIN	2

struct psm_ind {
	/* 1 rising, 0 falling */
	uint8_t edge;
	struct k_work work;
};

const struct device * gpio_0 = NULL;

static struct gpio_callback psm_ind_gpio_callback;
struct psm_ind psm_ind;

static void psm_ind_work_fn(struct k_work *work)
{
	struct psm_ind *data = 
		CONTAINER_OF(work, struct psm_ind, work);
	
	if (!data->edge) {
		LOG_INF("PSM entered.");
	} else {
		LOG_INF("Woken up from PSM.");
	}
}

static void psm_ind_callback(const struct device *dev,
			     struct gpio_callback *cb, uint32_t pins)
{
	psm_ind.edge = gpio_pin_get(gpio_0, LTE_PSM_IND_PIN);
	k_work_submit(&psm_ind.work);
}

static void app_modem_init(void) {
	int ret;

	gpio_0 = device_get_binding("GPIO_0");

	if (!device_is_ready(gpio_0)) {
		LOG_ERR("GPIO 0 is not ready");
		return;
	}
	
	gpio_pin_configure(gpio_0, LTE_PSM_IND_PIN, GPIO_INPUT | GPIO_ACTIVE_LOW);
	gpio_init_callback(&psm_ind_gpio_callback, psm_ind_callback,
			   BIT(LTE_PSM_IND_PIN));
	ret = gpio_add_callback(gpio_0, &psm_ind_gpio_callback);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return;
	}

	ret = gpio_pin_interrupt_configure(gpio_0, LTE_PSM_IND_PIN,
					  GPIO_INT_EDGE_BOTH);
					
	k_work_init(&psm_ind.work, psm_ind_work_fn);
}


void main(void)
{
	uint32_t dtr = 0;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}

	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	app_modem_init();
	while(1) {
		k_sleep(K_MSEC(100));
	}
}
