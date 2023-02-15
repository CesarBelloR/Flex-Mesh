#include "etc_device.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

#include <fs/nvs.h>
#include <logging/log.h>
#include <sys/reboot.h>
#include <zephyr.h>

#include "data/data_codec.h"
LOG_MODULE_REGISTER(etc_device, CONFIG_ETC_APP_LOG_LEVEL);

#define STORAGE_NODE_LABEL storage
#define RECORD_NODE_LABEL  record_storage

#if TEST
#define ETC_RECORD_MAX_SECTOR	  (8)
#define ETC_RECORD_MAX_PER_SECTOR (7)
#define ETC_RECORD_MAX_RECORD	  38
#else
/**
 * @brief Define a flash sector size based on HW
 *
 */
#define ETC_FLASH_SECTOR_SIZE	  (4096)
/**
 * @brief Define maximum record per sector
 */
#define ETC_RECORD_MAX_PER_SECTOR ((int)(ETC_FLASH_SECTOR_SIZE) / (ETC_DEVICE_RECORD_SIZE))
/**
 * @brief Maximum record in requirement
 */
#define ETC_RECORD_MAX_RECORD	  (90 * 24 * 4)
/**
 * @brief Max sector = round(fit sector + 1 free sector for swap) (ETC_RECORD_MAX_RECORD /
 * ETC_RECORD_MAX_PER_SECTOR) + 1
 */
#define ETC_RECORD_MAX_SECTOR	  ((int)((ETC_RECORD_MAX_RECORD) / (ETC_RECORD_MAX_PER_SECTOR)) + 1)
#endif

struct config {
	union etc_config etc_config;
	bool is_loaded;
};

union etc_device_record_header { // It will always change  NVS
	uint8_t header;
	struct {
		uint8_t ready : 1;
		uint8_t ack : 1;
		uint8_t wait : 1;
		uint8_t unused : 3;
	};
};

struct etc_device_record_index { // Constant in flash until the index is override (exflash)
	int8_t sector_idx;
	int8_t element_idx;
};

struct etc_device_record_table {
	struct etc_device_record_index oldest;
	struct etc_device_record_index newest;
	uint16_t total;
};

static union etc_device_record_header etc_device_record_header;
static struct etc_device_record_table etc_device_record_table;
static int etc_nvs_write(uint16_t element_id, const void *data, size_t len);
static int etc_nvs_read(uint16_t element_id, void *data, size_t len);
static struct etc_device_record_index etc_device_get_next_index(void);
static struct config etc_config;
static struct nvs_fs etc_fs;
static struct nvs_fs record_fs;

union etc_config *p_etc_config = &etc_config.etc_config;

static void etc_nvs_init(void)
{
	int rc = 0;
	struct flash_pages_info info;
	etc_fs.flash_device = FLASH_AREA_DEVICE(STORAGE_NODE_LABEL);
	if (!device_is_ready(etc_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", etc_fs.flash_device->name);
		return;
	}

	record_fs.flash_device = FLASH_AREA_DEVICE(RECORD_NODE_LABEL);
	if (!device_is_ready(record_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", record_fs.flash_device->name);
		return;
	}

	record_fs.offset = FLASH_AREA_OFFSET(RECORD_NODE_LABEL);
	etc_fs.offset = FLASH_AREA_OFFSET(STORAGE_NODE_LABEL);
	rc = flash_get_page_info_by_offs(etc_fs.flash_device, etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}

	etc_fs.sector_size = info.size;
	record_fs.sector_size = info.size;

	etc_fs.sector_count = (FLASH_AREA_SIZE(STORAGE_NODE_LABEL) / info.size);
	record_fs.sector_count = (FLASH_AREA_SIZE(RECORD_NODE_LABEL) / info.size);
	LOG_INF("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)etc_fs.offset,
		FLASH_AREA_SIZE(STORAGE_NODE_LABEL), info.size, etc_fs.sector_count);
	LOG_INF("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)record_fs.offset,
		FLASH_AREA_SIZE(RECORD_NODE_LABEL), info.size, record_fs.sector_count);
	rc = nvs_mount(&etc_fs);
	if (rc) {
		LOG_ERR("Flash Init failed");
		return;
	}

	LOG_INF("Initialised etc setting successfully");

	rc = etc_nvs_read(ETC_RECORD_STAT, &etc_device_record_table,
			  sizeof(etc_device_record_table));
	if (rc != 0) {
		etc_device_record_table.newest.sector_idx = 0;
		etc_device_record_table.oldest.sector_idx = 0;
		etc_device_record_table.newest.element_idx = 0;
		etc_device_record_table.oldest.element_idx = 0;
		etc_device_record_table.total = 0;
		rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table,
				   sizeof(etc_device_record_table));
		if (rc != 0) {
			LOG_ERR("Failed to write record stat");
		} else {
			LOG_INF("Initialized the table record successful");
		}
	}

	LOG_INF("Last record stat as below: ");
	LOG_INF("\tNewest record (%d,%d)", etc_device_record_table.newest.sector_idx,
		etc_device_record_table.newest.element_idx);
	LOG_INF("\tOldest record (%d,%d)", etc_device_record_table.oldest.sector_idx,
		etc_device_record_table.oldest.element_idx);
	LOG_INF("\tTotal record %d", etc_device_record_table.total);
}

static int etc_nvs_write(uint16_t element_id, const void *data, size_t len)
{
	size_t write_len = 0;
	write_len = nvs_write(&etc_fs, element_id, data, len);
	if (write_len != len && write_len != 0) {
		LOG_ERR("Failed in write data %d %d", len, write_len);
		return -EINVAL;
	}
	return 0;
}

static int etc_nvs_read(uint16_t element_id, void *data, size_t len)
{
	size_t read_len = 0;
	read_len = nvs_read(&etc_fs, element_id, data, len);
	if (read_len < 0) {
		LOG_ERR("Failed in reading NVS %d", read_len);
		return -EINVAL;
	}

	if (read_len > len) {
		LOG_ERR("Read length is higher than request read %d %d", len, read_len);
		return -EINVAL;
	}

	if (read_len == len) {
		return 0;
	}

	return -EINVAL;
}

void etc_device_init(void)
{
	p_etc_config->device_mode = (enum etc_device_mode)CONFIG_ETC_DEVICE_MODE;
	LOG_INF("Device is %s",
		p_etc_config->device_mode == ETC_DEVICE_MODE_RELAY ? "Relay" : "Logger");
	etc_nvs_init();
}

int etc_device_get_config(union etc_config *config)
{
	if (etc_config.is_loaded) {
		memcpy(config, &etc_config.etc_config, sizeof(etc_config.etc_config));
		return 0;
	}

	memset(&etc_config.etc_config, 0, sizeof(etc_config.etc_config));
	int read_len =
		etc_nvs_read(ETC_CONFIG_ID, &etc_config.etc_config, sizeof(etc_config.etc_config));
	if (read_len < 0) {
		LOG_ERR("Failed to read ETC Config");
		return -EINVAL;
	}
	memcpy(config, &etc_config.etc_config, sizeof(etc_config.etc_config));
	etc_config.is_loaded = true;
	return 0;
}

int etc_device_set_config(union etc_config *config)
{
	etc_config.is_loaded = true;
	memcpy(&etc_config.etc_config, config, sizeof(etc_config.etc_config));
	int rc =
		etc_nvs_write(ETC_CONFIG_ID, &etc_config.etc_config, sizeof(etc_config.etc_config));
	if (rc != 0) {
		LOG_ERR("Failed to write ETC Config");
		return -EINVAL;
	} else {
		LOG_DBG("Wrote successful ETC Config");
	}

	return 0;
}

bool etc_device_buffer_is_erased(uint8_t *buf, uint8_t length)
{
	for (int i = 0; i < length; i++) {
		if (buf[i] != 0xFF) {
			return false;
		}
	}

	return true;
}

int etc_device_write_record_sensor(struct sensor_data *sensor)
{
	union etc_device_record record;
	record.battery = (float)sensor->battery_mV / 1000.0;
	record.flag = 0;
	record.timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		record.sensor[i] = sensor->temperature[i];
	}

	record.sensor[SENSOR_EVENT_NUM_DEV_MAX] = 0.0;
	return etc_device_write_record(&record);
}

int etc_device_write_record(union etc_device_record *record)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	struct etc_device_record_index record_index;
	if (etc_device_record_table.total == 0) {
		record_index = etc_device_record_table.newest;
		etc_device_record_table.total += 1;
	} else {
		record_index = etc_device_get_next_index();
	}

	LOG_DBG("Record to write data (%d,%d)", record_index.sector_idx, record_index.element_idx);
	uint32_t record_addr = (record_fs.offset) +
			       record_index.sector_idx * record_fs.sector_size +
			       record_index.element_idx * ETC_DEVICE_RECORD_SIZE;
	uint16_t record_id = record_index.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			     record_index.element_idx + ETC_RECORD_HEADER;
	int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in reading flash err %d", rc);
		return rc;
	}

	if (etc_device_buffer_is_erased(buf, ETC_DEVICE_RECORD_SIZE) == false) {
		/* Need to erase flash */
		LOG_WRN("Data in address is not empty");
		LOG_HEXDUMP_DBG(buf, ETC_DEVICE_RECORD_SIZE, "DUMP");
		rc = flash_erase(record_fs.flash_device, record_addr, record_fs.sector_size);
		if (rc != 0) {
			LOG_ERR("Error in erasing flash err %d", rc);
			return rc;
		}
	}

	rc = flash_write(record_fs.flash_device, record_addr, record->data, ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in writing flash err %d", rc);
		return rc;
	}
	LOG_DBG("Write data success");

	etc_device_record_header.ack = 0;
	etc_device_record_header.ready = 1;
	etc_device_record_header.wait = 0;
	etc_device_record_header.unused = 0;
	rc = etc_nvs_write(record_id, &etc_device_record_header, sizeof(etc_device_record_header));
	if (rc != 0) {
		LOG_ERR("Failed to write record id %d error %d", record_id, rc);
		return rc;
	}

	rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table,
			   sizeof(etc_device_record_table));
	if (rc != 0) {
		LOG_ERR("Failed to write record stat");
	} else {
		LOG_DBG("Updated the table record successful");
	}

	return 0;
}

int etc_device_find_nack(etc_device_record_reading_callback reading_callback, void* data)
{
	int rc = 0;
	int oldest_id = etc_device_record_table.oldest.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			etc_device_record_table.oldest.element_idx + ETC_RECORD_HEADER;
	int newest_id = etc_device_record_table.newest.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			etc_device_record_table.newest.element_idx + ETC_RECORD_HEADER;
	int max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_MAX_PER_SECTOR +
		     ETC_RECORD_HEADER;
	int min_id = ETC_RECORD_HEADER;
	if (oldest_id == newest_id) {
		if (etc_device_record_table.total == 1) {
			if (reading_callback) {
				rc = reading_callback(min_id, data);
				if (rc > 0) {// Return record_id;
					return rc;
				} else if (rc == 0) {
					/* Continue reading*/
				} else {
					/* No action required */
				}
			}
			return 0;
		}
		return -ENOENT;
	}
	/* Sector newest is higher than oldest */
	if (oldest_id < newest_id) {
		for (int id = oldest_id; id <= newest_id; id++) {
			rc = etc_nvs_read(id, &etc_device_record_header,
					  sizeof(etc_device_record_header));
			if (rc == 0) {
				if (etc_device_record_header.ack == 0) {
					if (reading_callback) {
						rc = reading_callback(id, data);
						if (rc > 0) {// Return record_id;
							return rc;
						} else if (rc == 0) {
							/* Continue reading*/
						} else {
							/* No action required */
						}
					}
				}
			} else {
				LOG_WRN("Error id %d - error %d", id, rc);
			}
		}
	} else {
		for (int id = oldest_id; id <= max_id; id++) {
			rc = etc_nvs_read(id, &etc_device_record_header,
					  sizeof(etc_device_record_header));
			if (rc == 0) {
				if (etc_device_record_header.ack == 0) {
					if (reading_callback) {
						rc = reading_callback(id, data);
						if (rc > 0) {// Return record_id;
							return rc;
						} else if (rc == 0) {
							/* Continue reading*/
						} else {
							/* No action required */
						}
					}
				}
			}
		}

		for (int id = min_id; id <= newest_id; id++) {
			rc = etc_nvs_read(id, &etc_device_record_header,
					  sizeof(etc_device_record_header));
			if (rc == 0) {
				if (etc_device_record_header.ack == 0) {
					if (reading_callback) {
						rc = reading_callback(id, data);
						if (rc > 0) {// Return record_id;
							return rc;
						} else if (rc == 0) {
							/* Continue reading*/
						} else {
							/* No action required */
						}
					}
				}
			}
		}
	}

	if (rc == 0) {
		return rc;
	}
	return -ENOENT;
}

static int etc_device_record_reading(uint16_t record_id, void* data) {
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	union etc_device_record *record = (union etc_device_record *)data;
	struct etc_device_record_index index;
	index.sector_idx = (record_id - ETC_RECORD_HEADER) / ETC_RECORD_MAX_SECTOR;
	index.element_idx = (record_id - ETC_RECORD_HEADER) - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
	uint32_t record_addr = (record_fs.offset) + index.sector_idx * record_fs.sector_size +
			       index.element_idx * ETC_DEVICE_RECORD_SIZE;
	int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in reading flash err %d", rc);
		return rc;
	}

	memcpy(record->data, buf, ETC_DEVICE_RECORD_SIZE);
	return record_id;
}

int etc_device_read_record(union etc_device_record *record)
{
	int rc = etc_device_find_nack(etc_device_record_reading, record);
	if (rc < 0) {
		LOG_WRN("Don't have NACK record");
		return 0;
	}

	LOG_DBG("Record ID %d", rc);
	return rc;
}

int etc_device_set_ack_record(int record_id)
{
	int rc = etc_nvs_read(record_id, &etc_device_record_header,
			      sizeof(etc_device_record_header));
	if (rc != 0) {
		LOG_ERR("Failed to read record id %d error %d", record_id, rc);
		return rc;
	}

	if (etc_device_record_header.ack == 1) {
		LOG_DBG("Record is ACK already");
		return 0;
	}

	etc_device_record_header.ack = 1;
	rc = etc_nvs_write(record_id, &etc_device_record_header, sizeof(etc_device_record_header));
	if (rc != 0) {
		LOG_ERR("Failed to write record id %d error %d", record_id, rc);
		return rc;
	}
	return rc;
}
static struct etc_device_record_index etc_device_get_next_index(void)
{
	if (etc_device_record_table.newest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
		etc_device_record_table.newest.element_idx += 1;
	} else {
		etc_device_record_table.newest.element_idx = 0;
		if (etc_device_record_table.newest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
			etc_device_record_table.newest.sector_idx += 1;
		} else {
			etc_device_record_table.newest.sector_idx = 0;
		}
	}

	if (etc_device_record_table.total < ETC_RECORD_MAX_RECORD - 1) {
		etc_device_record_table.oldest.element_idx = 0;
		etc_device_record_table.oldest.sector_idx = 0;
		etc_device_record_table.total += 1;
	} else {
		if (etc_device_record_table.oldest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
			etc_device_record_table.oldest.element_idx += 1;
		} else {
			etc_device_record_table.oldest.element_idx = 0;
			if (etc_device_record_table.oldest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
				etc_device_record_table.oldest.sector_idx += 1;
			} else {
				etc_device_record_table.oldest.sector_idx = 0;
			}
		}
	}

	return etc_device_record_table.newest;
}

int etc_device_write_setting(uint16_t setting_id, void *setting, int setting_size)
{
	LOG_DBG("Write setting ID %d", setting_id);
	return etc_nvs_write(setting_id, setting, setting_size);
}

int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size)
{
	LOG_DBG("Read setting ID %d", setting_id);
	return etc_nvs_read(setting_id, setting, setting_size);
}

enum etc_device_mode etc_device_get_mode(void)
{
	return p_etc_config->device_mode;
}
int etc_device_get_rx_timeout(void)
{
	return p_etc_config->rx_duration_secs;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_get_record_reading(uint16_t record_id, void* data) {
	uint16_t *nack_counter = (uint16_t*)data;
	*nack_counter += 1;
	return 0;
}

static int cmd_num_report_record(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t total = etc_device_record_table.total;
	uint16_t nack = 0;
	int rc = etc_device_find_nack(cmd_get_record_reading, &nack);
	if (rc != 0) {
		shell_error(shell, "Can't query NACK record");
		return 0;
	}

	shell_print(shell, "Number of total records: %d", total);
	shell_print(shell, "Number of ack records: %d", total - nack);
	shell_print(shell, "Number of nack records: %d", nack);
	return 0;
}

static int cmd_get_nack_reading(const struct shell *shell, uint16_t record_id) {
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	uint8_t msg[ETC_DEVICE_RECORD_SIZE * 2 + 1] = {0x00};
	struct etc_device_record_index index;
	index.sector_idx = (record_id - ETC_RECORD_HEADER) / ETC_RECORD_MAX_SECTOR;
	index.element_idx = (record_id - ETC_RECORD_HEADER) - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
	uint32_t record_addr = (record_fs.offset) + index.sector_idx * record_fs.sector_size +
			       index.element_idx * ETC_DEVICE_RECORD_SIZE;
	int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		shell_error(shell, "Error in reading flash err %d", rc);
		return rc;
	}
	size_t length = bin2hex(buf, ETC_DEVICE_RECORD_SIZE, msg, sizeof(msg));
	shell_print(shell, "%s", msg);
	return 0;
}

static int cmd_get_nack_id(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		uint16_t record_id = (uint16_t)strtol(argv[1], NULL, 10);
		return cmd_get_nack_reading(shell, record_id + ETC_RECORD_HEADER);
	} 
	shell_error(shell, "Invalid input record id");
	return 0;
}

static int cmd_get_nack_id_list_reading(uint16_t record_id, void* data) {
	const struct shell *shell = (void*)data;
	shell_print(shell, "%d", record_id - ETC_RECORD_HEADER);
	return 0;
}

static int cmd_get_nack_id_list(const struct shell *shell, size_t argc, char **argv)
{
	int rc = etc_device_find_nack(cmd_get_nack_id_list_reading, (void*)shell);
	if (rc < 0) {
		shell_error(shell, "Don't have NACK record");
		return 0;
	}
	return 0;
}

static int cmd_clean_records(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_table.newest.sector_idx = 0;
	etc_device_record_table.oldest.sector_idx = 0;
	etc_device_record_table.newest.element_idx = 0;
	etc_device_record_table.oldest.element_idx = 0;
	etc_device_record_table.total = 0;
	int rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table,
			   sizeof(etc_device_record_table));
	if (rc != 0) {
		shell_error(shell, "Failed to write reset record %d", rc);
	} else {
		shell_print(shell, "Reset the record successfully");
	}
	return 0;
}

static int cmd_parser_hex_record(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t msg[ETC_DEVICE_RECORD_SIZE] = {0x00};
	if ((argc == 2) && (strlen(argv[1]) == (ETC_DEVICE_RECORD_SIZE * 2))) {
		hex2bin(argv[1], strlen(argv[1]), msg, sizeof(msg));
		union etc_device_record record;
		memcpy(record.data, msg, ETC_DEVICE_RECORD_SIZE);
		char buf[128] = {0x00};
		int buf_len = snprintf(buf, sizeof(buf), "%u,%1.2f,", record.timestamp, record.battery); 
		for (int i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++)
		{
			if (data_codec_compare_temperature_is_valid(record.sensor[i]))
			{
				buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "%2.2f,", record.sensor[i]);
			}
			else
			{
				buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "*,");
			}
		}
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "*");
		shell_print(shell, "Record: %s", buf);
	} else {
		shell_print(shell, "Invalid input record");
	} 
	
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_record, 
	SHELL_CMD(report, NULL, "Report number record (total/ack/nack)", cmd_num_report_record),
	SHELL_CMD(nack_list, NULL, "Get nack list record", cmd_get_nack_id_list),
	SHELL_CMD(nack_id, NULL, "Get nack record by id", cmd_get_nack_id),
	SHELL_CMD(clean, NULL, "Clean the records", cmd_clean_records),
	SHELL_CMD(parser, NULL, "Parser the hex record", cmd_parser_hex_record),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(record, &sub_record, "ETC Record Management", NULL);
#endif
