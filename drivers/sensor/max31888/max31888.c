/*
 * Copyright (c) 2023 EXACT Technology
 */

#define DT_DRV_COMPAT maxim_max31888

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "max31888.h"

LOG_MODULE_REGISTER(MAX31888, CONFIG_SENSOR_LOG_LEVEL);

static int max31888_configure(const struct device *dev);

/* measure wait time  */
static const uint16_t measure_wait_ms = 20;

static inline void max31888_temperature_from_raw(uint8_t *temp_raw, struct sensor_value *val) 
{
	int16_t temp = sys_get_be16(temp_raw);
	int64_t celsius_value = ((float)(temp) * 0.005) * 1000000;
	val->val1 = celsius_value / 1000000;
	val->val2 = celsius_value % 1000000;
	if (val->val2 < 0) {
		val->val2 *= (-1);
	}
}

static uint16_t max31888_crc16(const uint8_t *input, uint16_t len, uint16_t crc) 
{
	static const uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

	for (uint16_t i = 0; i < len; i++) {
		// Even though we're just copying a byte from the input,
		// we'll be doing 16-bit computation with it.
		uint16_t cdata = input[i];
		cdata = (cdata ^ crc) & 0xff;
		crc >>= 8;

		if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4])
			crc ^= 0xC001;

		cdata <<= 6;
		crc ^= cdata;
		cdata <<= 1;
		crc ^= cdata;
	}
	return crc;
}

static bool max31888_check_crc16(const uint8_t *input, uint16_t len, const uint8_t *inverted_crc) 
{
	uint16_t crc = 0;
	crc = ~max31888_crc16(input, len, crc);
	LOG_DBG("CRC 0x%02x 0x%02x - 0x%02x 0x%02x", crc & 0xFF, crc >> 8, inverted_crc[0], inverted_crc[1]);
	return (crc & 0xFF) == inverted_crc[0] && (crc >> 8) == inverted_crc[1];
}

static int max31888_read_fifo(const struct device *dev, struct max31888_fifo *fifo) 
{
	struct max31888_data *data = dev->data;
	const struct device *bus = max31888_bus(dev);
	uint8_t cmd_buf[5] = {MAX31888_CMD_READ_REGISTER, MAX31888_CMD_FIFO_DATA_REGISTER, 0x01};
	int rc = w1_write_read(bus, &data->config, cmd_buf, 3, (uint8_t *)&fifo[0], 4);
	if (rc) {
		LOG_ERR("Failed to write/read bus (err %d)", rc);
		return rc;
	}

	cmd_buf[3] = (uint8_t)((fifo->temp & 0xFF00) >> 8);
	cmd_buf[4] = (uint8_t)(fifo->temp & 0xFF);
	LOG_HEXDUMP_INF((uint8_t *)&fifo[0], 4, "READ");
	bool crc_check = max31888_check_crc16(cmd_buf, sizeof(cmd_buf), (const uint8_t *)&fifo->crc);
	if (!crc_check) { 
		return -EINVAL;
	}
	return 0;
}

/* Starts sensor temperature conversion without waiting for completion. */
static int max31888_temperature_convert(const struct device *dev) {
	int ret;
	struct max31888_data *data = dev->data;
	const struct device *bus = max31888_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}
	uint8_t cmd_buf[1] = {MAX31888_CMD_CONVERT_T};
	uint8_t crc[2] = {0x00};
	ret = w1_write_read(bus, &data->config, cmd_buf, 1, crc, 2);
	bool crc_check = max31888_check_crc16(cmd_buf, sizeof(cmd_buf), (const uint8_t *)crc);
	if (!crc_check) { 
		ret = -EINVAL;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int max31888_sample_fetch(const struct device *dev, enum sensor_channel chan) {
	struct max31888_data *data = dev->data;
	int status;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_AMBIENT_TEMP);

	if (!data->lazy_loaded) {
		status = max31888_configure(dev);
		if (status < 0) {
			return status;
		}
		data->lazy_loaded = true;
	}

	status = max31888_temperature_convert(dev);
	if (status < 0) {
		LOG_DBG("W1 fetch error");
		return status;
	}
	k_msleep(measure_wait_ms);
	return max31888_read_fifo(dev, &data->fifo);
}

static int max31888_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val) {
	struct max31888_data *data = dev->data;

	if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	max31888_temperature_from_raw((uint8_t *)&data->fifo.temp, val);
	return 0;
}

static int max31888_configure(const struct device *dev) {
	const struct max31888_config *cfg = dev->config;
	struct max31888_data *data = dev->data;

	if (w1_reset_bus(cfg->bus) <= 0) {
		LOG_ERR("No 1-Wire slaves connected");
		return -ENODEV;
	}

	/* In single drop configurations the rom can be read from device */
	if (w1_get_slave_count(cfg->bus) == 1) {
		if (w1_rom_to_uint64(&data->config.rom) == 0ULL) {
			(void)w1_read_rom(cfg->bus, &data->config.rom);
		}
	} else if (w1_rom_to_uint64(&data->config.rom) == 0ULL) {
		LOG_DBG("nr: %d", w1_get_slave_count(cfg->bus));
		LOG_ERR("ROM required, because multiple slaves are on the bus");
		return -EINVAL;
	}

	if ((cfg->family != 0) && (cfg->family != data->config.rom.family)) {
		LOG_ERR("Found 1-Wire slave is not a MAX31888");
		return -EINVAL;
	}

	LOG_DBG("Init MAX31888: ROM=%016llx\n", w1_rom_to_uint64(&data->config.rom));

	return 0;
}

int max31888_attr_set(const struct device *dev, enum sensor_channel chan, enum sensor_attribute attr, const struct sensor_value *thr) {
	struct max31888_data *data = dev->data;

	if ((enum sensor_attribute_w1)attr != SENSOR_ATTR_W1_ROM) {
		return -ENOTSUP;
	}

	data->lazy_loaded = false;
	w1_sensor_value_to_rom(thr, &data->config.rom);
	return 0;
}

static const struct sensor_driver_api max31888_driver_api = {
		.attr_set = max31888_attr_set,
		.sample_fetch = max31888_sample_fetch,
		.channel_get = max31888_channel_get,
};

static int max31888_init(const struct device *dev) {
	const struct max31888_config *cfg = dev->config;
	struct max31888_data *data = dev->data;

	if (device_is_ready(cfg->bus) == 0) {
		LOG_DBG("w1 bus is not ready");
		return -ENODEV;
	}

	w1_uint64_to_rom(0ULL, &data->config.rom);
	data->lazy_loaded = false;
	/* in multidrop configurations the rom is need, but is not set during
	 * driver initialization, therefore do lazy initialization in all cases.
	 */

	return 0;
}

#define MAX31888_CONFIG_INIT(inst)                                             \
	{                                                                            \
		.bus = DEVICE_DT_GET(DT_INST_BUS(inst)),                                   \
		.family = (uint8_t)DT_INST_PROP_OR(inst, family_code, 0x54),               \
	}

#define MAX31888_DEFINE(inst)                                                  \
	static struct max31888_data max31888_data_##inst;                            \
	static const struct max31888_config max31888_config_##inst =                 \
			MAX31888_CONFIG_INIT(inst);                                              \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, max31888_init, NULL,                      \
		&max31888_data_##inst, &max31888_config_##inst, \
		POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,       \
		&max31888_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAX31888_DEFINE)
