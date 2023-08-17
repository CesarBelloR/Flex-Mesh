/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_TMP1075_H_
#define ZEPHYR_DRIVERS_SENSOR_TMP1075_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#define TMP1075_REG_TEMPERATURE          0x00
#define TMP1075_DATA_NORMAL_SHIFT        4
#define TMP1075_REG_CONFIG   0x01
#define TMP1075_REG_TLOW         0x02
#define TMP1075_REG_THIGH        0x03

/* scale in micro degrees Celsius */
#define TMP1075_TEMP_SCALE       (0.0625f / 16.0f)

struct tmp1075_data {
	int16_t sample;
	uint16_t config_reg;
};

struct tmp1075_config {
	const struct i2c_dt_spec bus;
	bool extended_mode : 1;
};

#endif /*  ZEPHYR_DRIVERS_SENSOR_TMP1075_H_ */
