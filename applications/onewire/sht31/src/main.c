/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static const struct gpio_dt_spec s0_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);

void port_selection(int port_index) {
	switch (port_index) {
		case 1:
			gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_ACTIVE);
			gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
			break;
		case 2:
			gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
			gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
			break;
		case 3:
			gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_ACTIVE);
			gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_ACTIVE);
			break;
		case 4:
			gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
			gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_ACTIVE);
			break;
	}
}

void main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(sensirion_sht31);
	int rc;

	if (!device_is_ready(dev)) {
		LOG_ERR("Device %s is not ready", dev->name);
		return;
	}

	/* Set the IO for selecting the ports 2 */
	port_selection(2);
	/* Call to re-configure the sensor */
	sensor_attr_set(dev, SENSOR_CHAN_ALL, SENSOR_ATTR_CONFIGURATION, NULL);

	while (1) {
		struct sensor_value temp, hum;

		rc = sensor_sample_fetch(dev);
		if (rc == 0) {
			rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP,
						&temp);
		}
		if (rc == 0) {
			rc = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY,
						&hum);
		}
		if (rc != 0) {
			LOG_ERR("SHT31: failed: %d\n", rc);
			break;
		}

		LOG_INF("SHT31: %.2f Cel ; %0.2f %%RH",
		       sensor_value_to_double(&temp),
		       sensor_value_to_double(&hum));

		k_sleep(K_MSEC(2000));
	}
}
