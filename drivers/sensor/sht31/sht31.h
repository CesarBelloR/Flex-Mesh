/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_SHT31_H_
#define ZEPHYR_DRIVERS_SENSOR_SHT31_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#define SHT3XD_CMD_FETCH                0xE000
#define SHT3XD_CMD_ART                  0x2B32
#define SHT3XD_CMD_READ_STATUS          0xF32D
#define SHT3XD_CMD_CLEAR_STATUS         0x3041

#define SHT3XD_CMD_WRITE_TH_HIGH_SET    0x611D
#define SHT3XD_CMD_WRITE_TH_HIGH_CLEAR  0x6116
#define SHT3XD_CMD_WRITE_TH_LOW_SET     0x610B
#define SHT3XD_CMD_WRITE_TH_LOW_CLEAR   0x6100

#if CONFIG_SHT3XD_REPEATABILITY_LOW
	#define SHT3XD_REPEATABILITY_IDX        0
#elif CONFIG_SHT3XD_REPEATABILITY_MEDIUM
	#define SHT3XD_REPEATABILITY_IDX        1
#elif CONFIG_SHT3XD_REPEATABILITY_HIGH
	#define SHT3XD_REPEATABILITY_IDX        2
#endif

#if CONFIG_SHT3XD_MPS_05
	#define SHT3XD_MPS_IDX          0
#elif CONFIG_SHT3XD_MPS_1
	#define SHT3XD_MPS_IDX          1
#elif CONFIG_SHT3XD_MPS_2
	#define SHT3XD_MPS_IDX          2
#elif CONFIG_SHT3XD_MPS_4
	#define SHT3XD_MPS_IDX          3
#elif CONFIG_SHT3XD_MPS_10
	#define SHT3XD_MPS_IDX          4
#endif

#define SHT3XD_CLEAR_STATUS_WAIT_USEC   1000

struct sht31_config {
	struct i2c_dt_spec bus;
};

struct sht31_data {
	uint16_t t_sample;
	uint16_t rh_sample;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_SHT31_H_ */
