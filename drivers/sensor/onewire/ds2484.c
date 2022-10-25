/***************************************************************************/
/*!
\file       ds2484.c
\brief      DS2484 - Bridge IC to support I2C to One-wire interface

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <drivers/sensor.h>
#include <kernel.h>
#include <logging/log.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <sys/crc.h>
#include "ds2484.h"

LOG_MODULE_REGISTER(DS2484, CONFIG_DS2484_LOG_LEVEL);

#define DS2484_DEFAULT_7BIT_ADDR (0x18)
#define DS2844_ROM_MAX_SIZE (8)

struct ds2484_config {
	const struct device *bus;
	uint8_t addr;
};

struct ds2484_data {
	uint8_t _rom[DS2844_ROM_MAX_SIZE];
	uint8_t _last_discrepancy;
	bool _last_device_flag;
	uint8_t _last_family_discrepancy;
	uint8_t _config;
};

typedef enum {
	CMD_1WT = 0x78, // 1-Wire triplet
	CMD_1WSB = 0x87, // 1-Wire single bit
	CMD_1WRB = 0x96, // 1-Wire read byte
	CMD_1WWB = 0xA5, // 1-Wire write byte
	CMD_1WRS = 0xB4, // 1-Wire reset
	CMD_CHSL = 0xC3, // channel select
	CMD_WCFG = 0xD2, // write configuration
	CMD_SRP = 0xE1, // set read pointer
	CMD_DRST = 0xF0, // device reset
} ds248x_cmd_t;

typedef enum { POINTER_CONFIG = 0xC3, POINTER_DATA = 0xE1, POINTER_STATUS = 0xF0 } ds248x_pointer_t;

typedef enum {
	WIRE_COMMAND_SELECT = 0x55,
	WIRE_COMMAND_SKIP = 0xCC,
	WIRE_COMMAND_SEARCH = 0xF0
} ds248x_wire_cmd_t;

static struct ds2484_config m_ds2484_config;
static struct ds2484_data m_ds2484_data;

static int read_register(uint8_t *val)
{
	const struct ds2484_config *cfg = &m_ds2484_config;
	int rc = i2c_read(cfg->bus, val, 1, cfg->addr);
	return rc;
}

static int write_register(uint8_t addr, uint8_t val)
{
	const struct ds2484_config *cfg = &m_ds2484_config;
	int rc = 0;
	uint8_t tx_buf[2] = { addr, val };
	rc = i2c_write(cfg->bus, tx_buf, sizeof(tx_buf), cfg->addr);
	return rc;
}

static int write_command(uint8_t addr)
{
	const struct ds2484_config *cfg = &m_ds2484_config;
	int rc = 0;
	uint8_t tx_buf[1] = { addr };
	rc = i2c_write(cfg->bus, tx_buf, sizeof(tx_buf), cfg->addr);
	return rc;
}

static int ds2484_send_config(void)
{
	struct ds2484_data *data = &m_ds2484_data;
	uint8_t tx = data->_config | (~data->_config) << 4;
	int ret = write_register(CMD_WCFG, tx);
	if (ret != 0) {
		LOG_ERR("Failed to write_register error %d", ret);
		return ret;
	}

	return 0;
}

static int ds2484_set_read_pointer(ds248x_pointer_t address)
{
	return write_register(CMD_SRP, (uint8_t)address);
}

static int ds2484_check_error(uint8_t *status)
{
	static bool cb_sent[2] = { false, false };
	if (status[0] & DS248X_STATUS_SD) {
		if (!cb_sent[0]) {
			LOG_DBG("Short condition detected");
			cb_sent[0] = true;
		}
	} else {
		cb_sent[0] = false;
	}

	if (status[0] & DS248X_STATUS_RST) {
		if (!cb_sent[1]) {
			LOG_DBG("Reset condition detected");
			cb_sent[1] = true;
		}
	} else {
		cb_sent[1] = false;
	}
	return 0;
}

static int ds2484_wait_busy(uint8_t *status)
{
	const struct ds2484_config *cfg = &m_ds2484_config;
	uint8_t buf[1] = { 0x00 };
	int poll_count = 0;

	i2c_read(cfg->bus, buf, sizeof(buf), cfg->addr);

	while ((buf[0] & DS248X_STATUS_1WB) && ((poll_count++) < DS248X_POLL_LIMIT)) {
		i2c_read(cfg->bus, buf, sizeof(buf), cfg->addr);
		LOG_DBG("Status 0x%02x", buf[0]);
	}

	if (status) {
		memcpy(status, buf, sizeof(buf));
	}

	if (poll_count >= DS248X_POLL_LIMIT) {
		return -EINVAL;
	}

	ds2484_check_error(buf);
	return 0;
}

int ds2484_init(void)
{
	struct ds2484_config *config = &m_ds2484_config;
	struct ds2484_data *data = &m_ds2484_data;

	config->bus = (struct device *)device_get_binding("I2C_0");
	if (config->bus == NULL) {
		LOG_ERR("Failed to get device_get_binding I2C_0");
		return -EINVAL;
	}

	config->addr = DS2484_DEFAULT_7BIT_ADDR;
	memset(data->_rom, 0, sizeof(data->_rom));
	data->_last_family_discrepancy = 0;
	data->_last_device_flag = false;
	data->_last_family_discrepancy = 0;

	if (ds2484_load_config() != 0) {
		LOG_ERR("Failed to load configuration");
		return -EINVAL;
	}
	return 0;
}

int ds2484_set_config(ds248x_config_t config)
{
	struct ds2484_data *data = &m_ds2484_data;
	data->_config |= config;
	return ds2484_send_config();
}

int ds2484_clear_config(ds248x_config_t config)
{
	struct ds2484_data *data = &m_ds2484_data;
	data->_config &= ~(config);
	return ds2484_send_config();
}

int ds2484_load_config(void)
{
	struct ds2484_data *data = &m_ds2484_data;
	uint8_t buf[1] = { 0x00 };

	int ret = ds2484_set_read_pointer(POINTER_CONFIG);
	if (ret != 0) {
		LOG_ERR("Failed to ds2484_set_read_pointer error %d", ret);
		return ret;
	}

	ret = read_register(buf);
	data->_config = buf[0];
	return ret;
}

int ds2484_device_reset(void)
{
	uint8_t buf[1] = { 0x00 };

	int ret = write_command(CMD_DRST);
	if (ret != 0) {
		LOG_ERR("Failed to write_command error %d", ret);
		return ret;
	}

	ds2484_request_reset_search();

	ret = read_register(buf);
	if ((buf[0] & 0b11110111) != 0b10000) {
		LOG_ERR("Reset not successful");
		return -EINVAL;
	}

	return 0;
}

int ds2484_write_byte(uint8_t data)
{
	int ret = write_register(CMD_1WWB, data);
	if (ret != 0) {
		LOG_ERR("Failed to write_register error %d", ret);
		return ret;
	}

	return ds2484_wait_busy(NULL);
}

int ds2484_read_byte(uint8_t *data)
{
	int ret = write_command(CMD_1WRB);
	if (ret != 0) {
		LOG_ERR("Failed to write_command error %d", ret);
		return ret;
	}

	ret = ds2484_wait_busy(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to wait_busy error %d", ret);
		return ret;
	}

	ret = ds2484_set_read_pointer(POINTER_DATA);
	if (ret != 0) {
		LOG_ERR("Failed to set_read_pointer error %d", ret);
		return ret;
	}

	uint8_t buf[1] = { 0x00 };
	ret = read_register(buf);
	if (ret != 0) {
		LOG_ERR("Failed to read register error %d", ret);
		return ret;
	}

	*data = buf[0];
	return 0;
}

int ds2484_write_bytes(const uint8_t *data, size_t len)
{
	int ret = 0;
	for (int i = 0; i < len; i++) {
		ret = ds2484_write_byte(data[i]);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int ds2484_read_bytes(uint8_t *data, size_t len)
{
	char buf[1] = { 0x00 };
	int ret = 0;
	for (int i = 0; i < len; i++) {
		ret = ds2484_read_byte(buf);
		if (ret != 0) {
			return ret;
		}

		data[i] = buf[0];
	}

	return 0;
}

int ds2484_write_bit(bool bit)
{
	uint8_t buf[1] = { 0x00 };
	buf[0] = bit ? 0x80 : 0x00;

	return ds2484_write_byte(buf[0]);
}

int ds2484_read_bit(bool *data)
{
	int ret = 0;

	ret = ds2484_write_bit(true);
	if (ret != 0) {
		LOG_ERR("Failed to write bit error %d", ret);
		return ret;
	}

	uint8_t buf[1] = { 0x00 };
	ret = ds2484_wait_busy(buf);
	if (ret != 0) {
		LOG_ERR("Failed to wait_busy error %d", ret);
		return ret;
	}

	return buf[0] & DS248X_STATUS_SBR;
}

int ds2484_request_reset(void)
{
	uint8_t buf[1] = { 0 };
	int ret = 0;
	struct ds2484_data *data = &m_ds2484_data;

	bool spu = data->_config & DS248X_CONFIG_SPU;

	if (spu && (ds2484_clear_config(strong_pull_up) != 0)) {
		return -EINVAL;
	}

	ret = write_command(CMD_1WRS);
	if (ret != 0) {
		LOG_ERR("Failed to write_command error %d", ret);
		return ret;
	}

	ret = ds2484_wait_busy(buf);
	if (ret != 0) {
		LOG_ERR("Failed to wait_busy error %d", ret);
		return ret;
	}

	if (spu & (ds2484_set_config(strong_pull_up) != 0)) {
		return -EINVAL;
		;
	}

	return (buf[0] & DS248X_STATUS_PPD);
}

int ds2484_request_skip(void)
{
	return ds2484_write_byte(WIRE_COMMAND_SKIP);
}

int ds2484_request_select(const char *rom)
{
	struct ds2484_data *data = &m_ds2484_data;
	int ret = ds2484_write_byte(WIRE_COMMAND_SKIP);
	if (ret != 0) {
		LOG_ERR("Failed to ds2484_write_byte error %d", ret);
		return ret;
	}

	return ds2484_write_bytes(data->_rom, DS2844_ROM_MAX_SIZE);
}

int ds2484_request_search(char *rom)
{
	struct ds2484_data *data = &m_ds2484_data;
	uint8_t rom_byte_counter = 0;
	uint8_t id_bit_counter = 1;
	uint8_t last_zero = 0;
	uint8_t rom_byte_mask = 1;
	uint8_t buf[2] = { 0x00 };
	bool id_bit = false;
	bool cmp_id_bit = false;
	bool search_direction = false;

	if (data->_last_device_flag || (ds2484_request_reset() != 0)) {
		LOG_ERR("No %s devices on the bus", data->_last_device_flag ? "more" : "");
		return -EINVAL;
	}

	if (ds2484_write_byte(WIRE_COMMAND_SEARCH) != 0) {
		goto done;
	}

	while (rom_byte_counter < 8) {
		if (id_bit_counter < data->_last_discrepancy) {
			search_direction = ((data->_rom[rom_byte_counter] & rom_byte_mask) > 0);
		} else {
			search_direction = (id_bit_counter == data->_last_discrepancy);
		}

		buf[0] = CMD_1WT;
		buf[1] = search_direction ? 0x80 : 0x00;

		if (write_register(CMD_1WT, search_direction ? 0x80 : 0x00) != 0) {
			goto done;
		}

		if (ds2484_wait_busy(&buf[0]) != 0) {
			goto done;
		}

		id_bit = (buf[0] & DS248X_STATUS_SBR);
		cmp_id_bit = (buf[0] & DS248X_STATUS_TSB);
		search_direction = (buf[0] & DS248X_STATUS_DIR);

		ds2484_check_error(buf);

		if ((id_bit && cmp_id_bit) || (buf[0] & DS248X_STATUS_SD)) {
			LOG_ERR("No devices or SHORT on the bus");
			goto done;
		}

		if (!id_bit && !cmp_id_bit && !search_direction) {
			last_zero = id_bit_counter;

			if (last_zero < 9) {
				data->_last_family_discrepancy = last_zero;
			}
		}

		if (search_direction) {
			data->_rom[rom_byte_counter] |= rom_byte_mask;
		} else {
			data->_rom[rom_byte_counter] &= (uint8_t)~rom_byte_mask;
		}

		id_bit_counter++;
		rom_byte_mask <<= 1;

		if (rom_byte_mask == 0) {
			rom_byte_counter++;
			rom_byte_mask = 1;
		}
	}

	if (id_bit_counter < 65) {
		goto done;
	}

	data->_last_discrepancy = last_zero;

	if (data->_last_discrepancy == 0) {
		data->_last_device_flag = true;
	}

	if (data->_rom[0] == 0) {
		goto done;
	}

	// compare CRC
	if (!crc8(data->_rom, 8, 0x31, 0, false)) {
		return -1;
	}

	if (rom) {
		memcpy(rom, data->_rom, sizeof(data->_rom));
	}
done:
	return ds2484_request_reset_search();
}

int ds2484_request_reset_search(void)
{
	struct ds2484_data *data = &m_ds2484_data;
	data->_last_discrepancy = 0;
	data->_last_family_discrepancy = 0;
	data->_last_device_flag = false;

	memset(data->_rom, 0, sizeof(data->_rom));
	return 0;
}

int ds2484_request_search_family(uint8_t family_code)
{
	struct ds2484_data *data = &m_ds2484_data;
	memset(data->_rom, 0, sizeof(data->_rom));

	data->_rom[0] = family_code;
	data->_last_discrepancy = 64;
	data->_last_family_discrepancy = 0;
	data->_last_device_flag = false;
	return 0;
}

int ds2484_crc_validate(uint8_t *data, size_t len)
{
	uint8_t crc = crc8(data, len - 1, 0x31, 0, false);
	if (crc != data[len - 1]) {
		return -1;
	}

	return 0;
}