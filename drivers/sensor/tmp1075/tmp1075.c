/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tmp1075

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include "tmp1075.h"

LOG_MODULE_REGISTER(TMP1075, CONFIG_TMP1075_LOG_LEVEL);

typedef union {
	uint16_t bytes;
	struct {
		uint8_t unused;
		uint8_t shutdown : 1;
		uint8_t alert : 1;
		uint8_t polarity : 1;
		uint8_t fault : 2;
		uint8_t rate : 2;
		uint8_t os : 1;
	};
} tmp1075_cfg_reg;


static int tmp1075_reg_read(const struct tmp1075_config *cfg,
			   uint8_t reg, uint16_t *val)
{
	if (i2c_burst_read_dt(&cfg->bus, reg, (uint8_t *)val, sizeof(*val)) < 0) {
		return -EIO;
	}

	*val = sys_be16_to_cpu(*val);

	return 0;
}

static int tmp1075_reg_write(const struct tmp1075_config *cfg,
			    uint8_t reg, uint16_t val)
{
	uint8_t buf[3];

	buf[0] = reg;
	sys_put_be16(val, &buf[1]);

	return i2c_write_dt(&cfg->bus, buf, sizeof(buf));
}

static int tmp1075_start_conversion(const struct device *dev)
{
	int rc;
	const struct tmp1075_config *cfg = dev->config;

	tmp1075_cfg_reg reg = {0x00};
	rc = tmp1075_reg_read(cfg, TMP1075_REG_CONFIG, &reg.bytes);
	if (rc) {
		LOG_ERR("Failed to read configuration (err %d)", rc);
		return rc;
	}

	reg.os = 1;
	rc = tmp1075_reg_write(dev->config, TMP1075_REG_CONFIG, reg.bytes);
	if (rc) {
		LOG_ERR("Failed to set configuration (err %d)", rc);
		return rc;
	}

	return rc;
}

static int tmp1075_sample_fetch(const struct device *dev,
			       enum sensor_channel chan)
{
	struct tmp1075_data *drv_data = dev->data;
	const struct tmp1075_config *cfg = dev->config;
	uint16_t val;
	int rc;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_AMBIENT_TEMP);

	rc = tmp1075_start_conversion(dev);
	if (rc) {
		LOG_ERR("Failed to start conversion");
		return rc;
	}

	if (tmp1075_reg_read(cfg, TMP1075_REG_TEMPERATURE, &val) < 0) {
		LOG_ERR("Failed to read temperature");
		return -EIO;
	}

	drv_data->sample = arithmetic_shift_right((int16_t)val, TMP1075_DATA_NORMAL_SHIFT);

	return 0;
}

static int tmp1075_channel_get(const struct device *dev,
			      enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct tmp1075_data *drv_data = dev->data;
	int32_t uval;

	if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	uval = (int32_t)(drv_data->sample * TMP1075_TEMP_SCALE);
	val->val1 = uval / 1000000;
	val->val2 = uval % 1000000;

	return 0;
}

static const struct sensor_driver_api tmp1075_driver_api = {
	.sample_fetch = tmp1075_sample_fetch,
	.channel_get = tmp1075_channel_get,
};

int tmp1075_init(const struct device *dev)
{
	const struct tmp1075_config *cfg = dev->config;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C dev %s not ready", cfg->bus.bus->name);
		return -EINVAL;
	}

	return 0;
}


#define TMP1075_INST(inst)						    \
	static struct tmp1075_data tmp1075_data_##inst;			    \
	static const struct tmp1075_config tmp1075_config_##inst = {	    \
		.bus = I2C_DT_SPEC_INST_GET(inst),			    \
	};								    \
									    \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, tmp1075_init, NULL, &tmp1075_data_##inst, \
			      &tmp1075_config_##inst, POST_KERNEL,	    \
			      CONFIG_SENSOR_INIT_PRIORITY, &tmp1075_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TMP1075_INST)
