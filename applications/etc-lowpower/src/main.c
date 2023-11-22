/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/usb/usb_device.h>

#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#include <zephyr/drivers/uart.h>
#include <hal/nrf_uart.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_LOWPOWER_LOG_LEVEL);

#ifdef CONFIG_PCF85263
#include "pcf85263a.h"
#endif

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   5000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#define LTE_LOGIC_TRANSLATOR_OE 30

struct etc_interface_event_data {
	struct gpio_callback callback;
};

static struct etc_interface_event_data hall_sensor_event_data;
static struct etc_interface_event_data rtc_int_event_data;

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static const struct device *uart_devs[] = {
#ifdef CONFIG_SERIAL
#ifdef CONFIG_BOARD_NRF52840DK_NRF52840
	DEVICE_DT_GET(DT_NODELABEL(uart0)),
#endif
	DEVICE_DT_GET(DT_NODELABEL(uart1)),
#ifdef CONFIG_USB_DEVICE_STACK
	DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0)),
	DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart1))
#endif
#endif
};

static const struct device *pm_devs[] = {
#ifdef CONFIG_BOARD_ETC
	DEVICE_DT_GET(DT_NODELABEL(spi2)),
	DEVICE_DT_GET(DT_NODELABEL(spi3)),
	// DEVICE_DT_GET(DT_NODELABEL(pwm0)),
#elif BOARD_NRF52840DK_NRF52840
	DEVICE_DT_GET(DT_NODELABEL(spi0)),
	DEVICE_DT_GET(DT_NODELABEL(spi1)),
#endif
};


static void hall_sensor_callback_handler(const struct device *port, 
					 struct gpio_callback *cb, 
					 gpio_port_pins_t pins)
{
}


static void rtc_int_callback_handler(const struct device *port, 
				     struct gpio_callback *cb,
				     gpio_port_pins_t pins)
{
}

static void gpio_init(void)
{
#ifdef CONFIG_BOARD_ETC
#if !defined(CONFIG_BOARD_ETC_0_3_0)
	const struct gpio_dt_spec sens_enable = 
		GPIO_DT_SPEC_GET(DT_NODELABEL(sense_enable), control_gpios);
	gpio_pin_configure_dt(&sens_enable, GPIO_OUTPUT_LOW);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
static const struct gpio_dt_spec onewire_slpz_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(onewire_slpz), control_gpios, 0);
#endif
	const struct gpio_dt_spec vsens_enable = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
	const struct gpio_dt_spec sens_sel0 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
	const struct gpio_dt_spec sens_sel1 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
	const struct gpio_dt_spec rtc_int = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);
	static const struct gpio_dt_spec hall_sensor_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hall_int), control_gpios, 0);

	gpio_pin_configure_dt(&vsens_enable, GPIO_OUTPUT_HIGH);
	gpio_pin_configure_dt(&sens_sel0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sens_sel1, GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&rtc_int, GPIO_INPUT);

#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_ACTIVE);
#endif

	gpio_pin_configure(gpio0, LTE_LOGIC_TRANSLATOR_OE, GPIO_ACTIVE_LOW | GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&hall_sensor_dt, GPIO_INPUT);
    	gpio_pin_interrupt_configure_dt(&hall_sensor_dt, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&hall_sensor_event_data.callback, hall_sensor_callback_handler, BIT(hall_sensor_dt.pin));
	gpio_add_callback(hall_sensor_dt.port, &hall_sensor_event_data.callback);

	gpio_pin_configure_dt(&rtc_int, GPIO_INPUT | GPIO_PULL_UP);
    	gpio_pin_interrupt_configure_dt(&rtc_int, GPIO_INT_EDGE_FALLING);
	gpio_init_callback(&rtc_int_event_data.callback, rtc_int_callback_handler, BIT(rtc_int.pin));
	gpio_add_callback(rtc_int.port, &rtc_int_event_data.callback);
#endif
}

static void peripheral_init(void) {
#ifdef CONFIG_PCF85263
	pcf85263a_init("I2C_0");
	pcf85263a_set_interrupt_io(true);
	pcf85263a_set_clkpin(false);
#endif	
}

static void peripheral_lp(void)
{
	int i;
#if CONFIG_PM_DEVICE
	for (i = 0; i < ARRAY_SIZE(pm_devs); i++) {
		pm_device_action_run(pm_devs[i], PM_DEVICE_ACTION_SUSPEND);
	}

	for (i = 0; i < ARRAY_SIZE(uart_devs); i++) {	
		pm_device_action_run(uart_devs[i], PM_DEVICE_ACTION_SUSPEND);
	}
#endif
}

void main(void)
{
	int ret;

	if (!device_is_ready(led.port)) {
		return;
	}
	LOG_INF("Hello");

	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (!device_is_ready(dev) || usb_enable(NULL)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return;
	}

	gpio_init();
	peripheral_init();
	peripheral_lp();

	gpio_pin_set_dt(&led, 0);

	while (1) {
		// ret = gpio_pin_toggle_dt(&led);
		// if (ret < 0) {
		// 	return;
		// }
		k_msleep(SLEEP_TIME_MS);
	}
}

void idle_fn()
{
	k_sleep(K_FOREVER);

	while (1) {
		LOG_WRN("Idle exits");
		k_sleep(K_SECONDS(5));
	}
}

K_THREAD_DEFINE(app_idle, 1024, idle_fn, NULL, NULL, NULL, 5, 0, 0);