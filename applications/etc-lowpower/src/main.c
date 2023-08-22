/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>

#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#include <zephyr/drivers/uart.h>
#include <hal/nrf_uart.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_PCF85263
#include "pcf85263a.h"
#endif

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   5000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#define LTE_LOGIC_TRANSLATOR_OE 30

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

static void gpio_init(void)
{
#ifdef CONFIG_BOARD_ETC
#if !defined(CONFIG_BOARD_ETC_0_3_0)
	const struct gpio_dt_spec sens_enable = 
		GPIO_DT_SPEC_GET(DT_NODELABEL(sense_enable), control_gpios);
	gpio_pin_configure_dt(&sens_enable, GPIO_OUTPUT_LOW);
#endif
	const struct gpio_dt_spec vsens_enable = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
	const struct gpio_dt_spec sens_sel0 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
	const struct gpio_dt_spec sens_sel1 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
	const struct gpio_dt_spec rtc_int = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);

	gpio_pin_configure_dt(&vsens_enable, GPIO_OUTPUT_HIGH);
	gpio_pin_configure_dt(&sens_sel0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sens_sel1, GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&rtc_int, GPIO_INPUT);

	gpio_pin_configure(gpio0, LTE_LOGIC_TRANSLATOR_OE, GPIO_ACTIVE_LOW | GPIO_OUTPUT_INACTIVE);
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
