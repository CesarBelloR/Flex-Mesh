/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>

#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#include "pcf85263a.h"

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
PINCTRL_DT_DEFINE(DT_NODELABEL(spi1));
static const struct pinctrl_dev_config *spi1_pinctrl = 
			PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(spi1));
static const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static const struct device *pm_devs[] = {
	DEVICE_DT_GET(DT_NODELABEL(spi1)),
	DEVICE_DT_GET(DT_NODELABEL(spi2)),
	DEVICE_DT_GET(DT_NODELABEL(i2c0)),
};

static void gpio_init(void)
{
	const struct gpio_dt_spec sens_enable = 
		GPIO_DT_SPEC_GET(DT_NODELABEL(sense_enable), control_gpios);
	const struct gpio_dt_spec vsens_enable = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
	const struct gpio_dt_spec sens_sel0 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
	const struct gpio_dt_spec sens_sel1 = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
	const struct gpio_dt_spec rtc_int = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);

	gpio_pin_configure_dt(&sens_enable, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&vsens_enable, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sens_sel0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sens_sel1, GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&rtc_int, GPIO_INPUT);

	gpio_pin_configure(gpio0, LTE_LOGIC_TRANSLATOR_OE, GPIO_ACTIVE_LOW | GPIO_OUTPUT_ACTIVE);
}

static void peripheral_init(void) {
	pcf85263a_init("I2C_0");
	pcf85263a_set_interrupt_io(true);
	pcf85263a_set_clkpin(false);
}

static void peripheral_lp(void)
{
	int i;
#if CONFIG_PM_DEVICE
	for (i = 0; i < ARRAY_SIZE(pm_devs); i++) {
		pm_device_action_run(pm_devs[i], PM_DEVICE_ACTION_SUSPEND);
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

	peripheral_init();
	gpio_init();
	peripheral_lp();

	gpio_pin_set_dt(&led, 0);

	while (1) {
		// ret = gpio_pin_toggle_dt(&led);
		// if (ret < 0) {
		// 	return;
		// }
		ret = gpio_pin_toggle(gpio0, LTE_LOGIC_TRANSLATOR_OE);
		k_msleep(SLEEP_TIME_MS);
	}
}
