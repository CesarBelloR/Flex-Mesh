/*
 * Copyright (c) 2023 EXACT Technology
 */

#define DT_DRV_COMPAT maxim_ds28e18

#include <zephyr/drivers/i2c.h>
#include <zephyr/dt-bindings/i2c/i2c.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(DS28E18, CONFIG_ONEWIRE_I2C_LOG_LEVEL);

#include "ds28e18.h"

static const struct gpio_dt_spec s0_dt =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);

static int ds28e18_reset_bus(const struct device *dev);
static int ds28e18_onewire_configure(const struct device *dev);
static int ds28e18_populate_rom(const struct device *dev);
static int ds28e18_set_io(const struct device *dev);
static int ds28e18_onewire_device_status(const struct device *dev);

static int ds28e18_i2c_configure_set_clock_frequency(const struct device *dev, uint32_t clock_frequency);
static int ds28e18_i2c_configure_get_clock_frequency(const struct device *dev);

static int ds28e18_i2c_setup_write_seq(const struct device *dev, uint16_t addr, uint8_t* buf, uint8_t length);
static int ds28e18_i2c_setup_read_seq(const struct device *dev, uint16_t addr, uint8_t* buf, uint8_t length);
static int ds28e18_i2c_setup_run_seq(const struct device *dev, uint16_t addr, uint8_t cmd_len);

static int ds28e18_i2c_configure(const struct device *dev, uint32_t i2c_config);
static int ds28e18_i2c_transfer(const struct device *dev,
				  struct i2c_msg *msgs,
				  uint8_t num_msgs, uint16_t addr);
static int ds28e18_i2c_recover_bus(const struct device *dev);

static uint16_t ds28e18_crc16(const uint8_t *input, uint16_t len, uint16_t crc) 
{
	static const uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

	for (uint16_t i = 0; i < len; i++) {
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

static bool ds28e18_check_crc16(const uint8_t *input, uint16_t len, const uint8_t *inverted_crc) 
{
	uint16_t crc = 0;
	crc = ~ds28e18_crc16(input, len, crc);
	// LOG_DBG("CRC 0x%02x 0x%02x - 0x%02x 0x%02x", crc & 0xFF, crc >> 8, inverted_crc[0], inverted_crc[1]);
	return (crc & 0xFF) == inverted_crc[0] && (crc >> 8) == inverted_crc[1];
}

static int ds28e18_i2c_configure_set_clock_frequency(const struct device *dev, uint32_t clock_frequency) {
	struct ds28e18_config_reg config_reg = {0x00};

	switch (clock_frequency) {
	case 100000:
		config_reg.spd = 0;
		break;
	case 400000:
		config_reg.spd = 1;
		break;
	case 1000000:
		config_reg.spd = 2;
		break;
	default:
		LOG_ERR("unsupported speed");
		return -EINVAL;
	}
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	int ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[7] = {DS28E18_CMD_START, 2, DS28E18_CMD_WRITE_CONFIG, 0x00, 0x0, 0x00, DS28E18_CMD_RELEASE_BYTE};
	uint8_t rx_buf[5] = {0x00};
	memcpy(&tx_buf[3], &config_reg, sizeof(config_reg));
	uint16_t crc = 0;
	crc = ~ds28e18_crc16(tx_buf, 3, crc);
	tx_buf[4] = (uint8_t)(crc & 0x00FF);
	tx_buf[5] = (uint8_t)((crc & 0xFF00) >> 8);
	
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, sizeof(rx_buf));
	crc = (uint16_t)(rx_buf[4] << 8) | (uint16_t)(rx_buf[3]);
	bool crc_check = ds28e18_check_crc16(&rx_buf[1], 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static uint32_t ds28e18_i2c_get_clock_frequency(uint8_t spd) {
	switch (spd) {
		case 0: 
			return 100000;
		case 1:
			return 400000;
		case 2:
			return 1000000;
	}
	return 0;
}

static int ds28e18_i2c_configure_get_clock_frequency(const struct device *dev) {
	int ret;
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_match_rom(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[6] = {DS28E18_CMD_START, 1, DS28E18_CMD_READ_CONFIG, 0x0, 0x00, DS28E18_CMD_RELEASE_BYTE};
	uint8_t rx_buf[6] = {0x00};
	uint16_t crc = 0;
	crc = ~ds28e18_crc16(tx_buf, 3, crc);
	tx_buf[3] = (uint8_t)(crc & 0x00FF);
	tx_buf[4] = (uint8_t)((crc & 0xFF00) >> 8);
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, 6);
	crc = (uint16_t)(rx_buf[5] << 8) | (uint16_t)(rx_buf[4]);
	bool crc_check = ds28e18_check_crc16(&rx_buf[1], 3, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	}

	struct ds28e18_config_reg config_reg = {0x00};
	memcpy(&config_reg, &rx_buf[3], sizeof(uint8_t));
	LOG_DBG("Clock Speed %d", ds28e18_i2c_get_clock_frequency(config_reg.spd));
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_i2c_setup_write_seq(const struct device *dev, uint16_t addr, uint8_t* buf, uint8_t length) {
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	int ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}
	memset(data->tx_buf, 0, sizeof(data->tx_buf));
	memset(data->rx_buf, 0, sizeof(data->rx_buf));
	uint16_t crc = 0;
	uint8_t offset = 0;
	data->tx_buf[offset++] = DS28E18_CMD_START;
	data->tx_buf[offset++] = 3 + length;
	data->tx_buf[offset++] = DS28E18_CMD_WRITE_SEQ;
	data->tx_buf[offset++] = (uint8_t)(addr & 0x00FF);
	data->tx_buf[offset++] = 0x00;
	memcpy(&data->tx_buf[offset], buf, length);
	offset += length;
	crc = ~ds28e18_crc16(data->tx_buf, 5 + length, crc);
	data->tx_buf[offset++] = (uint8_t)(crc & 0x00FF);
	data->tx_buf[offset++] = (uint8_t)((crc & 0xFF00) >> 8);
	data->tx_buf[offset++] = DS28E18_CMD_RELEASE_BYTE;
	ret = w1_write_read(bus, &data->config, data->tx_buf, offset, data->rx_buf, 5);
	LOG_HEXDUMP_DBG(data->tx_buf, offset, "TX");
	LOG_HEXDUMP_DBG(data->rx_buf, 5, "RX");
	crc = (uint16_t)(data->rx_buf[4] << 8) | (uint16_t)(data->rx_buf[3]);
	bool crc_check = ds28e18_check_crc16(&data->rx_buf[1], 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (data->rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_i2c_setup_read_seq(const struct device *dev, uint16_t addr, uint8_t* buf, uint8_t length) {
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);
	
	(void)w1_lock_bus(bus);
	int ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	memset(data->tx_buf, 0, sizeof(data->tx_buf));
	memset(data->rx_buf, 0, sizeof(data->rx_buf));
	uint16_t crc = 0;
	uint8_t offset = 0;
	data->tx_buf[offset++] = DS28E18_CMD_START;
	data->tx_buf[offset++] = 3;
	data->tx_buf[offset++] = DS28E18_CMD_READ_SEQ;
	data->tx_buf[offset++] = (uint8_t)(addr & 0x00FF);
	struct ds28e18_read_reg reg = {0x00};
	reg.slen = length;
	reg.addr_hi = 0;
	memcpy(&data->tx_buf[offset], &reg, sizeof(reg));
	offset += sizeof(reg);
	crc = ~ds28e18_crc16(data->tx_buf, 5, crc);
	data->tx_buf[offset++] = (uint8_t)(crc & 0x00FF);
	data->tx_buf[offset++] = (uint8_t)((crc & 0xFF00) >> 8);
	data->tx_buf[offset++] = DS28E18_CMD_RELEASE_BYTE;
	ret = w1_write_read(bus, &data->config, data->tx_buf, offset, data->rx_buf, length + 5);
	LOG_HEXDUMP_DBG(data->tx_buf, offset, "TX");
	LOG_HEXDUMP_DBG(data->rx_buf, length + 5, "RX");
	crc = (uint16_t)(data->rx_buf[length + 4] << 8) | (uint16_t)(data->rx_buf[length + 3]);
	bool crc_check = ds28e18_check_crc16(&data->rx_buf[1], length + 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (data->rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	} else {
		memcpy(buf, &data->rx_buf[3], length);
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_i2c_setup_run_seq(const struct device *dev, uint16_t addr, uint8_t cmd_len) {
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	int ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	memset(data->tx_buf, 0, sizeof(data->tx_buf));
	memset(data->rx_buf, 0, sizeof(data->rx_buf));
	uint16_t crc = 0;
	uint8_t offset = 0;
	data->tx_buf[offset++] = DS28E18_CMD_START;
	data->tx_buf[offset++] = 4;
	data->tx_buf[offset++] = DS28E18_CMD_RUN_SEQ;
	data->tx_buf[offset++] = (uint8_t)(addr & 0x00FF);
	struct ds28e18_read_reg reg = {0x00};
	reg.addr_hi = 0;
	reg.slen = cmd_len;
	memcpy(&data->tx_buf[offset], &reg, sizeof(reg));
	offset += sizeof(reg);
	data->tx_buf[offset++] = 0x00; 

	crc = ~ds28e18_crc16(data->tx_buf, 6, crc);
	data->tx_buf[offset++] = (uint8_t)(crc & 0x00FF);
	data->tx_buf[offset++] = (uint8_t)((crc & 0xFF00) >> 8);
	data->tx_buf[offset++] = DS28E18_CMD_RELEASE_BYTE;
	ret = w1_write_block(bus, data->tx_buf, offset);
	if (ret) {
		LOG_ERR("Failed to write block (err %d)", ret);
		return ret;
	}

	k_msleep(1);
	ret = w1_read_block(bus, data->rx_buf, 5);
	if (ret) {
		LOG_ERR("Failed to read block (err %d)", ret);
		return ret;
	}

	LOG_HEXDUMP_DBG(data->tx_buf, offset, "TX");
	LOG_HEXDUMP_DBG(data->rx_buf, 5, "RX");
	crc = (uint16_t)(data->rx_buf[4] << 8) | (uint16_t)(data->rx_buf[3]);
	bool crc_check = ds28e18_check_crc16(&data->rx_buf[1], 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (data->rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static const struct i2c_driver_api ds28e18_i2c_driver_api = {
	.configure   = ds28e18_i2c_configure,
	.transfer    = ds28e18_i2c_transfer,
	.recover_bus = ds28e18_i2c_recover_bus,
};

static int ds28e18_i2c_configure(const struct device *dev,
				   uint32_t i2c_config)
{
	int ret;
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_match_rom(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[6] = {DS28E18_CMD_START, 1, DS28E18_CMD_READ_CONFIG, 0x0, 0x00, DS28E18_CMD_RELEASE_BYTE};
	uint8_t rx_buf[6] = {0x00};
	uint16_t crc = 0;
	crc = ~ds28e18_crc16(tx_buf, 3, crc);
	tx_buf[3] = (uint8_t)(crc & 0x00FF);
	tx_buf[4] = (uint8_t)((crc & 0xFF00) >> 8);
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, 6);
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_i2c_transfer(const struct device *dev,
				  struct i2c_msg *msgs,
				  uint8_t num_msgs, uint16_t addr)
{
	int ret = 0;
	uint8_t tx_buf[DS28E18_CMD_LEN] = {0x00};
	uint8_t rx_buf[DS28E18_CMD_LEN] = {0x00};
	uint8_t offset = 0;
	tx_buf[offset++] = DS28E18_CMD_I2C_START_CMD;
	size_t read_index = -1;
	for (size_t i = 0; i < num_msgs; i++) {
		if (I2C_MSG_ADDR_10_BITS & msgs[i].flags) {
			ret = -ENOTSUP;
			break;
		}

		if ((msgs[i].flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE) {
			tx_buf[offset++] = DS28E18_CMD_I2C_WRITE_DATA_CMD;
			tx_buf[offset++] = msgs[i].len + 1;
			tx_buf[offset++] = (uint8_t)((addr & 0xFF) << 1);
			memcpy(&tx_buf[offset], msgs[i].buf, msgs[i].len);
			offset += msgs[i].len;
		} else if ((msgs[i].flags & I2C_MSG_RW_MASK) == I2C_MSG_READ){
			read_index = i;
			tx_buf[offset++] = DS28E18_CMD_I2C_WRITE_DATA_CMD;
			tx_buf[offset++] = 1;
			tx_buf[offset++] = (uint8_t)(((addr & 0xFF) << 1) | 1);
			tx_buf[offset++] = DS28E18_CMD_I2C_READ_DATA_CMD;
			tx_buf[offset++] = msgs[i].len;
			for (int j = 0; j < msgs[i].len; j++) {
				tx_buf[offset++] = 0xFF;
			}
		}

		if (msgs[i].flags & I2C_MSG_STOP) {
			tx_buf[offset++] = DS28E18_CMD_I2C_STOP_CMD;
		}
	}

	ret = ds28e18_i2c_setup_write_seq(dev, 0x00, tx_buf, offset);
	if (ret) {
		LOG_ERR("Failed to setup write sequence (Err %d)", ret);
		return ret;
	}

	ret = ds28e18_i2c_setup_read_seq(dev, 0x00, rx_buf, offset);
	if (ret) {
		LOG_ERR("Failed to setup read sequence (Err %d)", ret);
		return ret;
	}

	ret = ds28e18_i2c_setup_run_seq(dev, 0x00, offset);
	if (ret) {
		LOG_ERR("Failed to setup run sequence (Err %d)", ret);
		return ret;
	}

	if (read_index != -1) {
		memset(rx_buf, 0, sizeof(rx_buf));
		ret = ds28e18_i2c_setup_read_seq(dev, 0x06, rx_buf, msgs[read_index].len);
		if (ret) {
			LOG_ERR("Failed to setup read sequence (Err %d)", ret);
			return ret;
		}
		memcpy(msgs[read_index].buf, rx_buf, msgs[read_index].len);
	}
	return 0;
}

static int ds28e18_i2c_recover_bus(const struct device *dev)
{
	return -ENOTSUP;
}

static int ds28e18_populate_rom(const struct device *dev) {
	int ret;
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_reset_select(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[10] = {DS28E18_CMD_START, 5, DS28E18_CMD_WRITE_IO_CONFIG};
	uint8_t rx_buf[5] = {0x00};
	uint16_t crc = 0;

	tx_buf[3] = 0x0B;
	tx_buf[4] = 0x03;
	tx_buf[5] = 0xA5;
	tx_buf[6] = 0x0F;

	crc = ~ds28e18_crc16(tx_buf, 7, crc);
	tx_buf[7] = (uint8_t)(crc & 0x00FF);
	tx_buf[8] = (uint8_t)((crc & 0xFF00) >> 8);
	tx_buf[9] = DS28E18_CMD_RELEASE_BYTE;
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, 5);
	crc = (uint16_t)(rx_buf[4] << 8) | (uint16_t)(rx_buf[3]);
	bool crc_check = ds28e18_check_crc16(&rx_buf[1], 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_reset_bus(const struct device *dev) {
	const struct ds28e18_i2c_config *cfg = dev->config;

	if (w1_reset_bus(cfg->bus) <= 0) {
		LOG_ERR("No 1-Wire slaves connected");
		return -ENODEV;
	}
	return 0;
}

static int ds28e18_onewire_configure(const struct device *dev)
{
	const struct ds28e18_i2c_config *cfg = dev->config;
	struct ds28e18_i2c_data *data = dev->data;

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
		LOG_ERR("Found 1-Wire slave is not a DS28E18");
		return -EINVAL;
	}

	LOG_DBG("Init DS28E18: ROM=%016llx",
		w1_rom_to_uint64(&data->config.rom));

	return 0;
}

static int ds28e18_set_io(const struct device *dev) {
	int ret;
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_match_rom(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[10] = {DS28E18_CMD_START, 5, DS28E18_CMD_WRITE_IO_CONFIG};
	uint8_t rx_buf[5] = {0x00};
	uint16_t crc = 0;

	tx_buf[3] = 0x0B;
	tx_buf[4] = 0x03;
	tx_buf[5] = 0xA5;
	tx_buf[6] = 0x0F;

	crc = ~ds28e18_crc16(tx_buf, 7, crc);
	tx_buf[7] = (uint8_t)(crc & 0x00FF);
	tx_buf[8] = (uint8_t)((crc & 0xFF00) >> 8);
	tx_buf[9] = DS28E18_CMD_RELEASE_BYTE;
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, 5);
	crc = (uint16_t)(rx_buf[4] << 8) | (uint16_t)(rx_buf[3]);
	bool crc_check = ds28e18_check_crc16(&rx_buf[1], 2, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_onewire_device_status(const struct device *dev) {
	int ret;
	struct ds28e18_i2c_data *data = dev->data;
	const struct device *bus = ds28e18_bus(dev);

	(void)w1_lock_bus(bus);
	ret = w1_match_rom(bus, &data->config);
	if (ret != 0) {
		goto out;
	}

	uint8_t tx_buf[6] = {DS28E18_CMD_START, 1, DS28E18_CMD_DEV_STATUS, 0x0, 0x00, DS28E18_CMD_RELEASE_BYTE};
	uint8_t rx_buf[9] = {0x00};
	uint16_t crc = 0;
	crc = ~ds28e18_crc16(tx_buf, 3, crc);
	tx_buf[3] = (uint8_t)(crc & 0x00FF);
	tx_buf[4] = (uint8_t)((crc & 0xFF00) >> 8);
	ret = w1_write_read(bus, &data->config, tx_buf, sizeof(tx_buf), rx_buf, 9);
	crc = (uint16_t)(rx_buf[8] << 8) | (uint16_t)(rx_buf[7]);
	bool crc_check = ds28e18_check_crc16(&rx_buf[1], 6, (const uint8_t *)&crc);
	if (!crc_check) { 
		ret = -EINVAL;
		goto out;
	}

	if (rx_buf[DS28E18_RETURN_BYTE_POS] != DS28E18_RETURN_VALID) {
		ret = -EINVAL;
	} else {
		LOG_DBG("POR Status 0x%02x", rx_buf[3]);
		LOG_DBG("Device Version 0x%02x", rx_buf[4]);
		LOG_DBG("MAN ID 0x%02x-0x%02x", rx_buf[5], rx_buf[6]);
	}
out:
	(void)w1_unlock_bus(bus);
	return ret;
}

static int ds28e18_i2c_init(const struct device *dev)
{
	const struct ds28e18_i2c_config *cfg = dev->config;
	struct ds28e18_i2c_data *data = dev->data;

	if (device_is_ready(cfg->bus) == 0) {
		LOG_DBG("w1 bus for is not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_LOW);
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_LOW);

	w1_uint64_to_rom(0ULL, &data->config.rom);
	int rc = ds28e18_reset_bus(dev);
	if (rc) {
		LOG_ERR("Failed to reset bus (err %d)", rc);
		return rc;
	}
	rc = ds28e18_populate_rom(dev);
	if (rc) {
		LOG_ERR("Failed to populate ROM (err %d)", rc);
		return rc;
	}
	rc = ds28e18_onewire_configure(dev);
	if (rc) {
		LOG_ERR("Failed to configure one-wire (err %d)", rc);
		return rc;
	}
	rc = ds28e18_set_io(dev);
	if (rc) {
		LOG_ERR("Failed to set IO (err %d)", rc);
		return rc;
	}
	rc = ds28e18_onewire_device_status(dev);
	if (rc) {
		LOG_ERR("Failed to get device status");
		return rc;
	}
	LOG_DBG("Set clock frequency at %d", cfg->frequency);
	rc = ds28e18_i2c_configure_set_clock_frequency(dev, cfg->frequency);
	if (rc) {
		LOG_ERR("Failed to set clock frequency (err %d)", rc);
		return rc;
	}
	rc = ds28e18_i2c_configure_get_clock_frequency(dev);
	if (rc) {
		LOG_ERR("Failed to get clock frequency (err %d)", rc);
		return rc;
	}
	return rc;
}

struct ds28e18_i2c_data ds28e18_i2c_data;
struct ds28e18_i2c_config ds28e18_i2c_config = {
	.bus = DEVICE_DT_GET(DT_INST_BUS(0)),
	.family = (uint8_t)DT_INST_PROP_OR(inst, family_code, 0x56),
	.frequency = (uint32_t)DT_INST_PROP_OR(inst, clock_frequency, 100000),
};

I2C_DEVICE_DT_INST_DEFINE(0, ds28e18_i2c_init, NULL,
		    &ds28e18_i2c_data, &ds28e18_i2c_config,
		    POST_KERNEL, CONFIG_ONEWIRE_I2C_INIT_PRIORITY,
		    &ds28e18_i2c_driver_api);

/*
 * Make sure that this driver is not initialized before the w1 bus is available
 */
BUILD_ASSERT(CONFIG_ONEWIRE_I2C_INIT_PRIORITY > CONFIG_W1_INIT_PRIORITY);