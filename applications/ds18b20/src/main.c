/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <hal/nrf_gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "ds18b20.h"

void main(void)
{
	bool rc = ds18b20_init();
	LOG_DBG("rc %d", rc);
	while(1) {
		LOG_DBG("Poll %d", ds18b20_manualconvert());
		printf("Temp %.2f\n", ds18b20[0].temperature);
		k_sleep(K_SECONDS(1));
	}
}
