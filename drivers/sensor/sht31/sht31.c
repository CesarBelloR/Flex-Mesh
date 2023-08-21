/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sensirion_sht31

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "sht31.h"

LOG_MODULE_REGISTER(SHT31, CONFIG_SENSOR_LOG_LEVEL);

static const uint16_t measure_cmd[3] = {
	0x2400, 0x240B, 0x2416
};

static const int measure_wait[3] = {
	4000, 6000, 15000
};

/*
 * CRC algorithm parameters were taken from the
 * "Checksum Calculation" section of the datasheet.
 */
static uint8_t sht31_compute_crc(uint16_t value)
{
	uint8_t buf[2];

	sys_put_be16(value, buf);
	return crc8(buf, 2, 0x31, 0xFF, false);
}

int sht31_write_command(const struct device *dev, uint16_t cmd)
{
	const struct sht31_config *config = dev->config;
	uint8_t tx_buf[2];

	sys_put_be16(cmd, tx_buf);
	return i2c_write_dt(&config->bus, tx_buf, sizeof(tx_buf));
}

int sht31_write_reg(const struct device *dev, uint16_t cmd, uint16_t val)
{
	const struct sht31_config *config = dev->config;
	uint8_t tx_buf[5];

	sys_put_be16(cmd, &tx_buf[0]);
	sys_put_be16(val, &tx_buf[2]);
	tx_buf[4] = sht31_compute_crc(val);

	return i2c_write_dt(&config->bus, tx_buf, sizeof(tx_buf));
}

static int sht31_sample_fetch(const struct device *dev,
			       enum sensor_channel chan)
{
	const struct sht31_config *config = dev->config;
	struct sht31_data *data = dev->data;
	uint8_t rx_buf[6];
	uint16_t t_sample, rh_sample;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL);

	/* start single shot measurement */
	if (sht31_write_command(dev,
				 measure_cmd[SHT3XD_REPEATABILITY_IDX])
	    < 0) {
		LOG_DBG("Failed to set single shot measurement mode!");
		return -EIO;
	}
	k_sleep(K_MSEC(measure_wait[SHT3XD_REPEATABILITY_IDX] / USEC_PER_MSEC));

	if (i2c_read_dt(&config->bus, rx_buf, sizeof(rx_buf)) < 0) {
		LOG_DBG("Failed to read data sample!");
		return -EIO;
	}

	t_sample = sys_get_be16(&rx_buf[0]);
	if (sht31_compute_crc(t_sample) != rx_buf[2]) {
		LOG_DBG("Received invalid temperature CRC!");
		return -EIO;
	}

	rh_sample = sys_get_be16(&rx_buf[3]);
	if (sht31_compute_crc(rh_sample) != rx_buf[5]) {
		LOG_DBG("Received invalid relative humidity CRC!");
		return -EIO;
	}

	data->t_sample = t_sample;
	data->rh_sample = rh_sample;

	return 0;
}

static int sht31_channel_get(const struct device *dev,
			      enum sensor_channel chan,
			      struct sensor_value *val)
{
	const struct sht31_data *data = dev->data;
	uint64_t tmp;

	/*
	 * See datasheet "Conversion of Signal Output" section
	 * for more details on processing sample data.
	 */
	if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
		/* val = -45 + 175 * sample / (2^16 -1) */
		tmp = (uint64_t)data->t_sample * 175U;
		val->val1 = (int32_t)(tmp / 0xFFFF) - 45;
		val->val2 = ((tmp % 0xFFFF) * 1000000U) / 0xFFFF;
	} else if (chan == SENSOR_CHAN_HUMIDITY) {
		/* val = 100 * sample / (2^16 -1) */
		uint32_t tmp2 = (uint32_t)data->rh_sample * 100U;
		val->val1 = tmp2 / 0xFFFF;
		/* x * 100000 / 65536 == x * 15625 / 1024 */
		val->val2 = (tmp2 % 0xFFFF) * 15625U / 1024;
	} else {
		return -ENOTSUP;
	}

	return 0;
}

static int sht31_attr_set(const struct device *dev,
			   enum sensor_channel chan,
			   enum sensor_attribute attr,
			   const struct sensor_value *val)
{
	int ret;

#ifdef CONFIG_PM_DEVICE
	enum pm_device_state state;

	(void)pm_device_state_get(dev, &state);
	if (state != PM_DEVICE_STATE_ACTIVE) {
		return -EBUSY;
	}
#endif

	switch (attr) {
	case SENSOR_ATTR_CONFIGURATION:
		/* Configure the I2C again */
		i2c_configure(dev, 0);

		/* clear status register */
		if (sht31_write_command(dev, SHT3XD_CMD_CLEAR_STATUS) < 0) {
			LOG_DBG("Failed to clear status register!");
			return -EIO;
		}

		k_busy_wait(SHT3XD_CLEAR_STATUS_WAIT_USEC);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct sensor_driver_api sht31_driver_api = {
	.attr_set = sht31_attr_set,
	.sample_fetch = sht31_sample_fetch,
	.channel_get = sht31_channel_get,
};

static int sht31_init(const struct device *dev)
{
	const struct sht31_config *cfg = dev->config;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C bus %s is not ready!", cfg->bus.bus->name);
		return -EINVAL;
	}

	return 0;
}


#define SHT31_DEFINE(inst)							\
	struct sht31_data sht310_data_##inst;					\
	static const struct sht31_config sht310_cfg_##inst = {		\
		.bus = I2C_DT_SPEC_INST_GET(inst)				\
	};									\
	SENSOR_DEVICE_DT_INST_DEFINE(inst, sht31_init, NULL,			\
		&sht310_data_##inst, &sht310_cfg_##inst,			\
		POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,			\
		&sht31_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SHT31_DEFINE)
