/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

void main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(sensirion_sht3xd);
	int rc;

	if (!device_is_ready(dev)) {
		LOG_ERR("Device %s is not ready", dev->name);
		return;
	}
	

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
			LOG_ERR("SHT3XD: failed: %d\n", rc);
			break;
		}

		LOG_INF("SHT3XD: %.2f Cel ; %0.2f %%RH",
		       sensor_value_to_double(&temp),
		       sensor_value_to_double(&hum));

		k_sleep(K_MSEC(2000));
	}
}
