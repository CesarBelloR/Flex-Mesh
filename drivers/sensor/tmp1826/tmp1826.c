/*
 * Copyright (c) 2023 EXACT Technology
 */

#define DT_DRV_COMPAT ti_tmp1826

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "tmp1826.h"

LOG_MODULE_REGISTER(TMP1826, CONFIG_SENSOR_LOG_LEVEL);

static int tmp1826_configure(const struct device *dev);

/* measure wait time for 12-bit & 16-bit resolution respectively */
static const uint16_t measure_wait_ms = 10;

static inline void tmp1826_temperature_from_raw(uint8_t *temp_raw,
						struct sensor_value *val)
{
	int16_t temp = sys_get_le16(temp_raw);

	val->val1 = temp / 16;
	val->val2 = (temp % 16) * 1000000 / 16;
}

static uint8_t tmp1826_crc8(uint8_t *data_buf, uint8_t len)
{
    uint8_t crc = 0;
    uint8_t byte_cnt = 0;
    uint8_t bit_cnt = 0;
    for ( byte_cnt = 0; byte_cnt < len; byte_cnt++ ) 
    {
        crc ^= data_buf[byte_cnt];
        for ( bit_cnt = 0; bit_cnt < 8; bit_cnt++ ) 
        {
            if ( crc & 0x01 ) 
            {
                crc = ( crc >> 1 ) ^ 0x8C;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/*
 * Write scratch pad, read back, then copy to eeprom
 */
static int tmp1826_write_scratchpad(const struct device *dev,
				    struct tmp1826_scratchpad scratchpad)
{
	struct tmp1826_data *data = dev->data;
	const struct device *bus = tmp1826_bus(dev);
    uint8_t data_buf[10] = { 0 };
    uint8_t crc_byte = 0;

	data_buf[0] = TMP1826_CMD_WRITE_SCRATCHPAD;
    data_buf[1] = scratchpad.config_1 | TMP1826_CFG1_RESERVED;
    data_buf[2] = scratchpad.config_2;
    data_buf[3] = scratchpad.short_addr;
    data_buf[4] = (uint8_t) (scratchpad.temp_limit_low & 0xFF);
    data_buf[5] = (uint8_t) ((scratchpad.temp_limit_low >> 8) & 0xFF);
    data_buf[6] = (uint8_t) (scratchpad.temp_limit_high & 0xFF );
    data_buf[7] = (uint8_t) ((scratchpad.temp_limit_high >> 8) & 0xFF);
    data_buf[8] = (uint8_t) (scratchpad.temp_offset & 0xFF);
    data_buf[9] = (uint8_t) ((scratchpad.temp_offset >> 8) & 0xFF);

	int rc = w1_write_read(bus, &data->config, data_buf, sizeof(data_buf), &crc_byte, 1);
	if (rc) {
		LOG_ERR("Failed to write scratchpad (err %d)", rc);
		return rc;
	}

	uint8_t calc_crc = tmp1826_crc8(&data_buf[1], 9);
	if (crc_byte != calc_crc) {
		LOG_ERR("Failed in calculating CRC8 0x%02x - 0x%02x", calc_crc, crc_byte);
		return -EINVAL;
	}

	return 0;
}

static int tmp1826_read_scratchpad(const struct device *dev,
				   struct tmp1826_scratchpad *scratchpad)
{
	struct tmp1826_data *data = dev->data;
	const struct device *bus = tmp1826_bus(dev);
	uint8_t cmd = TMP1826_CMD_READ_SCRATCHPAD;
	uint8_t data_buf[18] = {0};
	int rc = w1_write_read(bus, &data->config, &cmd, 1,
			     (uint8_t *)&data_buf[0], 18);
	if (rc) {
		LOG_ERR("Failed to read scratchpad (err %d)", rc);
		return rc;
	}

	uint8_t first_calc_crc = tmp1826_crc8(&data_buf[0], 8);
	uint8_t second_calc_crc = tmp1826_crc8(&data_buf[9], 8);
	LOG_HEXDUMP_DBG(data_buf, sizeof(data_buf), "READ");
	if ((first_calc_crc != data_buf[8]) || (second_calc_crc != data_buf[17])) {
		LOG_ERR("Failed in calculating CRC8 0x%02x - 0x%02x and 0x%02x - 0x%02x", 
			first_calc_crc, data_buf[8], second_calc_crc, data_buf[17]);
		return -EINVAL;
	}
    scratchpad->temperature = ((int16_t) data_buf[1] << 8 ) | data_buf[0];
    scratchpad->status = data_buf[2];
    scratchpad->config_1 = data_buf[4];
    scratchpad->config_2 = data_buf[5];
    scratchpad->short_addr = data_buf[6];
    scratchpad->temp_limit_low = ((int16_t) data_buf[10] << 8 ) | data_buf[9];
    scratchpad->temp_limit_high = ((int16_t) data_buf[12] << 8 ) | data_buf[11];
    scratchpad->temp_offset = ((int16_t) data_buf[14] << 8 ) | data_buf[13];
	return 0;
}

/* Starts sensor temperature conversion without waiting for completion. */
static int tmp1826_temperature_convert(const struct device *dev)
{
	int ret;
	struct tmp1826_data *data = dev->data;
	const struct device *bus = tmp1826_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}
	ret = w1_write_byte(bus, TMP1826_CMD_CONVERT_T);
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

/*
 * Write resolution into configuration struct,
 * but don't write it to the sensor yet.
 */
static void tmp1826_set_default(const struct device *dev, uint8_t resolution)
{
	struct tmp1826_data *data = dev->data;

	uint8_t resolution_cfg = resolution == 12 ? TMP1826_CFG1_TEMP_FMT_12_BIT : 
						   TMP1826_CFG1_TEMP_FMT_16_BIT;
	
    data->scratchpad.config_1 = resolution_cfg | 
                           TMP1826_CFG1_CONV_TIME_SEL_5p5MS | 
                           TMP1826_CFG1_ALERT_MODE_COMPARATOR | 
                           TMP1826_CFG1_AVG_SEL_NO_AVG | 
                           TMP1826_CFG1_CONV_MODE_SEL_ONE_SHOT;
    data->scratchpad.config_2 =TMP1826_CFG2_OD_DIS | 
                           TMP1826_CFG2_FLEX_ADDR_MODE_HOST | 
                           TMP1826_CFG2_ARB_MODE_DIS | 
                           TMP1826_CFG2_HYSTERESIS_5_C | 
                           TMP1826_CFG2_LOCK_DIS;
}

static int tmp1826_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
	struct tmp1826_data *data = dev->data;
	int status;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL ||
			chan == SENSOR_CHAN_AMBIENT_TEMP);

	if (!data->lazy_loaded) {
		status = tmp1826_configure(dev);
		if (status < 0) {
			return status;
		}
		data->lazy_loaded = true;
	}

	status = tmp1826_temperature_convert(dev);
	if (status < 0) {
		LOG_DBG("W1 fetch error");
		return status;
	}
	k_msleep(measure_wait_ms);
	return tmp1826_read_scratchpad(dev, &data->scratchpad);
}

static int tmp1826_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct tmp1826_data *data = dev->data;

	if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	tmp1826_temperature_from_raw((uint8_t *)&data->scratchpad.temperature, val);
	return 0;
}

static int tmp1826_configure(const struct device *dev)
{
	const struct tmp1826_config *cfg = dev->config;
	struct tmp1826_data *data = dev->data;
	int ret;

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
		LOG_ERR("Found 1-Wire slave is not a TMP1826");
		return -EINVAL;
	}

	/* write default configuration */
	tmp1826_set_default(dev, cfg->resolution);
	ret = tmp1826_write_scratchpad(dev, data->scratchpad);
	if (ret < 0) {
		return ret;
	}
	LOG_DBG("Init TMP1826: ROM=%016llx\n",
		w1_rom_to_uint64(&data->config.rom));

	return 0;
}

int tmp1826_attr_set(const struct device *dev, enum sensor_channel chan,
		     enum sensor_attribute attr, const struct sensor_value *thr)
{
	struct tmp1826_data *data = dev->data;

	if ((enum sensor_attribute_w1)attr != SENSOR_ATTR_W1_ROM) {
		return -ENOTSUP;
	}

	data->lazy_loaded = false;
	w1_sensor_value_to_rom(thr, &data->config.rom);
	return 0;
}

static const struct sensor_driver_api tmp1826_driver_api = {
	.attr_set = tmp1826_attr_set,
	.sample_fetch = tmp1826_sample_fetch,
	.channel_get = tmp1826_channel_get,
};

static int tmp1826_init(const struct device *dev)
{
	const struct tmp1826_config *cfg = dev->config;
	struct tmp1826_data *data = dev->data;

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

#define TMP1826_CONFIG_INIT(inst)					       \
	{								       \
		.bus = DEVICE_DT_GET(DT_INST_BUS(inst)),		       \
		.family = (uint8_t)DT_INST_PROP_OR(inst, family_code, 0x26),   \
		.resolution = DT_INST_PROP(inst, resolution),		       \
	}

#define TMP1826_DEFINE(inst)						\
	static struct tmp1826_data tmp1826_data_##inst;			\
	static const struct tmp1826_config tmp1826_config_##inst =	\
		TMP1826_CONFIG_INIT(inst);				\
	SENSOR_DEVICE_DT_INST_DEFINE(inst,				\
			      tmp1826_init,				\
			      NULL,					\
			      &tmp1826_data_##inst,			\
			      &tmp1826_config_##inst,			\
			      POST_KERNEL,				\
			      CONFIG_SENSOR_INIT_PRIORITY,		\
			      &tmp1826_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TMP1826_DEFINE)
