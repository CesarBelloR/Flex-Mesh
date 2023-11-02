#include "etc_device.h"
#include "etc_sensor.h"
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/random/rand32.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include "etc_settings.h"
#include "cloud/cloud_codec/data_codec.h"
LOG_MODULE_REGISTER(etc_device, CONFIG_ETC_APP_LOG_LEVEL);

#define ETC_SETTINGS_NODE_LABEL etc_settings_storage
#define ETC_RECORD_NODE_LABEL  etc_records_storage

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

#define ETC_RECORD_DEFAULT_RX_DURATION_SECONDS (5)
#define ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS (60)
#define ETC_RECORD_DEFAULT_TX_INTERVAL_SECONDS (300)
#define ETC_RECORD_DEFAULT_TX_PROBE_SECONDS (21600)
#define ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE (15)

struct etc_device_reclaim_info {
	uint16_t current_index;
	uint16_t start_index;
	uint16_t stop_index;
	uint16_t flag_in_process;
};

// Structure to represent a combination.
struct etc_device_power_matrix_element {
	enum etc_device_mode device_mode;
	enum etc_power_mode_e power_mode;
	enum etc_sensor_status probe_status;
	enum etc_device_transmit_type transmit_type;
};

// Array to hold all possible combinations.
struct etc_device_power_matrix_element cloud_matrix_combinations[] = {
	{ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_ALWAYS_ON, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_CONNECTED, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION, ETC_DEVICE_TRANSMIT_NO_PROBE},
	{ETC_DEVICE_MODE_RELAY, ETC_POWER_MODE_ALWAYS_ON, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_RELAY, ETC_POWER_MODE_INTERVAL, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_RELAY, ETC_POWER_MODE_PROBE, SENSOR_CONNECTED, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_RELAY, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION, ETC_DEVICE_TRANSMIT_NO_PROBE},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_ALWAYS_ON, SENSOR_NA, ETC_DEVICE_TRANSMIT_SYNC_LORA_LOGGER},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_NA, ETC_DEVICE_TRANSMIT_SYNC_LORA_LOGGER},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_CONNECTED, ETC_DEVICE_TRANSMIT_SYNC_LORA_LOGGER},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION, ETC_DEVICE_TRANSMIT_SYNC_LORA_LOGGER}
};

struct etc_device_power_matrix_element lora_matrix_combinations[] = {
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_ALWAYS_ON, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_NA, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_CONNECTED, ETC_DEVICE_TRANSMIT_NORMAL},
	{ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION, ETC_DEVICE_TRANSMIT_NO_PROBE},
};

static union etc_device_record_header etc_device_record_header;
static struct etc_device_record_table etc_device_record_table;
static int etc_nvs_write(uint16_t element_id, const void *data, size_t len);
static int etc_nvs_read(uint16_t element_id, void *data, size_t len);
static struct etc_device_record_index etc_device_get_next_index(void);
static struct nvs_fs etc_fs;
static struct nvs_fs record_fs;
static uint16_t ram_nack_record_id;
static enum etc_logger_job logger_job = ETC_LOGGER_JOB_LOG;
static uint16_t tx_logger_lora_offset_mins = 0;
static uint16_t tx_no_probe_offset_mins = 0;
static struct etc_device_reclaim_info etc_reclaim_info = {0x00};

void etc_device_nvs_init(void)
{
	int rc = 0;
	struct flash_pages_info info;
	etc_fs.flash_device = FLASH_AREA_DEVICE(ETC_SETTINGS_NODE_LABEL);
	if (!device_is_ready(etc_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", etc_fs.flash_device->name);
		return;
	}

	record_fs.flash_device = FLASH_AREA_DEVICE(ETC_RECORD_NODE_LABEL);
	if (!device_is_ready(record_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", record_fs.flash_device->name);
		return;
	}

	record_fs.offset = FLASH_AREA_OFFSET(ETC_RECORD_NODE_LABEL);
	etc_fs.offset = FLASH_AREA_OFFSET(ETC_SETTINGS_NODE_LABEL);
	rc = flash_get_page_info_by_offs(etc_fs.flash_device, etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}

	etc_fs.sector_size = info.size;
	record_fs.sector_size = info.size;

	etc_fs.sector_count = (FLASH_AREA_SIZE(ETC_SETTINGS_NODE_LABEL) / info.size);
	record_fs.sector_count = (FLASH_AREA_SIZE(ETC_RECORD_NODE_LABEL) / info.size);
	rc = nvs_mount(&etc_fs);
	if (rc) {
		LOG_ERR("Flash Init failed");
		return;
	}

	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)etc_fs.offset,
		FLASH_AREA_SIZE(ETC_SETTINGS_NODE_LABEL), info.size, etc_fs.sector_count);
	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)record_fs.offset,
		FLASH_AREA_SIZE(ETC_RECORD_NODE_LABEL), info.size, record_fs.sector_count);
	LOG_DBG("Initialised etc setting successfully");

	rc = etc_nvs_read(ETC_RECORD_STAT, &etc_device_record_table,
			  sizeof(etc_device_record_table));
	if (rc != 0) {
		etc_device_record_table.newest.sector_idx = 0;
		etc_device_record_table.oldest.sector_idx = 0;
		etc_device_record_table.newest.element_idx = 0;
		etc_device_record_table.oldest.element_idx = 0;
		etc_device_record_table.total = 0;
		etc_device_record_table.last_nack_record_id = 0;
		ram_nack_record_id = 0;
		rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table,
				   sizeof(etc_device_record_table));
		if (rc != 0) {
			LOG_ERR("Failed to write record stat");
		} else {
			LOG_INF("Initialized the table record successful");
		}
	} else {
		ram_nack_record_id = etc_device_record_table.last_nack_record_id;
	}

	rc = etc_nvs_read(ETC_RECORD_RECLAIM, &etc_reclaim_info, sizeof(etc_reclaim_info));
	if (rc != 0) {
		etc_reclaim_info.current_index = 0;
		etc_reclaim_info.start_index = 0;
		etc_reclaim_info.stop_index = 0;
		etc_reclaim_info.flag_in_process = 0;
		rc = etc_nvs_write(ETC_RECORD_RECLAIM, &etc_reclaim_info,
				   sizeof(etc_reclaim_info));
		if (rc != 0) {
			LOG_ERR("Failed to write reclaim info");
		} else {
			LOG_INF("Initialized the reclaim info successful");
		}
	}

	LOG_DBG("Last record stat as below: ");
	LOG_DBG("\tNewest record (%d,%d)", etc_device_record_table.newest.sector_idx,
		etc_device_record_table.newest.element_idx);
	LOG_DBG("\tOldest record (%d,%d)", etc_device_record_table.oldest.sector_idx,
		etc_device_record_table.oldest.element_idx);
	LOG_DBG("\tTotal record %d - last ack %d", etc_device_record_table.total,
		etc_device_record_table.last_nack_record_id);
	
	LOG_DBG("Reclaim information: %d %d", etc_reclaim_info.current_index, etc_reclaim_info.flag_in_process);
	LOG_DBG("\tStart index: %d", etc_reclaim_info.start_index);
	LOG_DBG("\tStop index %d", etc_reclaim_info.stop_index);
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
	ssize_t read_len = 0;
	read_len = nvs_read(&etc_fs, element_id, data, len);
	if (read_len < 0) {
		LOG_ERR("Failed in reading NVS %d", read_len);
		return read_len;
	}

	if (read_len > len) {
		LOG_ERR("Read length is higher than request read %d %d %d", element_id, len,
			read_len);
		return read_len;
	}

	if (read_len == len) {
		return 0;
	}

	return -EINVAL;
}

static int etc_nvs_read_with_len(uint16_t element_id, void *data, size_t len)
{
	ssize_t read_len = 0;
	read_len = nvs_read(&etc_fs, element_id, data, len);
	if (read_len < 0) {
		LOG_ERR("Failed in reading NVS %d", read_len);
	}

	return read_len;
}

void etc_device_init(void)
{
	char *dev_str = "Unknown";
	enum etc_device_mode dev_mode = etc_get_device_mode();
	logger_job = ETC_LOGGER_JOB_TX;
	/* Logger Lora mode will sync with interval quarter hour */
	tx_logger_lora_offset_mins = (uint16_t)((sys_rand32_get() % 4) *
		ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE);
	tx_no_probe_offset_mins = (uint16_t)(sys_rand32_get() % 60);
	if (dev_mode == ETC_DEVICE_MODE_RELAY) {
		dev_str = "Relay";
	} else if (dev_mode == ETC_DEVICE_MODE_LORA_LOGGER) {
		dev_str = "LoRa Logger";
	} else if (dev_mode == ETC_DEVICE_MODE_LTE_LOGGER) {
		dev_str = "LTE Logger";
	}
	LOG_INF("Device is %s", dev_str);
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
	record.flag = (uint32_t)(sensor->battery_status);
	record.timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		record.sensor[i] = sensor->sensor[i];
	}
	LOG_HEXDUMP_DBG((uint8_t *)&record, sizeof(record), "SAVE");

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

	uint32_t record_addr = (record_fs.offset) +
			       record_index.sector_idx * record_fs.sector_size +
			       record_index.element_idx * ETC_DEVICE_RECORD_SIZE;
	uint16_t record_id = record_index.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			     record_index.element_idx + ETC_RECORD_HEADER;
	LOG_DBG("Record to write data %d (0x%08x) (%d,%d)", record_id, record_addr,
		record_index.sector_idx, record_index.element_idx);
	int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in reading flash err %d", rc);
		return rc;
	}

	if (etc_device_buffer_is_erased(buf, ETC_DEVICE_RECORD_SIZE) == false) {
		/* Need to erase flash */
		LOG_WRN("Data in address is not empty 0x%08x", record_addr);
		LOG_HEXDUMP_DBG(buf, ETC_DEVICE_RECORD_SIZE, "DUMP");
		uint32_t offset_sector = record_addr - record_addr % record_fs.sector_size;
		rc = flash_erase(record_fs.flash_device, offset_sector, record_fs.sector_size);
		if (rc != 0) {
			LOG_ERR("Error in erasing flash err %d 0x%08x", rc, offset_sector);
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

static int etc_device_reclaim_data(etc_device_record_reading_callback reading_callback, void *data) {
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER;
	uint16_t min_id = ETC_RECORD_HEADER;
	int rc = 0;
	if (etc_reclaim_info.flag_in_process == 1) {
		if (etc_reclaim_info.start_index <= etc_reclaim_info.stop_index) {
			if ((etc_reclaim_info.current_index >= etc_reclaim_info.start_index) &&
			    (etc_reclaim_info.stop_index >= etc_reclaim_info.current_index)) {
				LOG_DBG("Reclaim at %d", etc_reclaim_info.current_index);
				rc = reading_callback(etc_reclaim_info.current_index, data);
				if (rc > 0) { // Return record_id;
					etc_reclaim_info.current_index += 1;
					return rc;
				} else {
					/* No action required */
				}
			} else {
				etc_reclaim_info.flag_in_process = 0;
				etc_reclaim_info.current_index = 0;
				rc = etc_nvs_write(ETC_RECORD_RECLAIM, &etc_reclaim_info,
						   sizeof(etc_reclaim_info));
				if (rc != 0) {
					LOG_ERR("Failed to write reclaim info");
				} else {
					LOG_INF("Initialized the reclaim info successful");
				}
			}
		} else {
			if (etc_reclaim_info.current_index >= etc_reclaim_info.start_index &&
			    etc_reclaim_info.current_index <= max_id) {
				rc = reading_callback(etc_reclaim_info.current_index, data);
				LOG_DBG("Reclaim at %d", etc_reclaim_info.current_index);
				if (rc > 0) { // Return record_id;
					if (etc_reclaim_info.current_index == max_id) {
						etc_reclaim_info.current_index = min_id;
					} else {
						etc_reclaim_info.current_index += 1;
					}
					return rc;
				} else {
					/* No action required */
				}
			}

			if (etc_reclaim_info.current_index >= min_id &&
			    etc_reclaim_info.current_index <= etc_reclaim_info.stop_index) {
				rc = reading_callback(etc_reclaim_info.current_index, data);
				LOG_DBG("Reclaim at %d", etc_reclaim_info.current_index);
				if (rc > 0) { // Return record_id;
					if (etc_reclaim_info.current_index ==
					    etc_reclaim_info.stop_index) {
						etc_reclaim_info.flag_in_process = 0;
						etc_reclaim_info.current_index = 0;
						rc = etc_nvs_write(ETC_RECORD_RECLAIM,
								   &etc_reclaim_info,
								   sizeof(etc_reclaim_info));
						if (rc != 0) {
							LOG_ERR("Failed to write reclaim info");
						} else {
							LOG_INF("Initialized the reclaim info "
								"successful");
						}
					} else {
						etc_reclaim_info.current_index += 1;
					}
					return rc;
				} else {
					/* No action required */
				}
			}
		}
	}

	if (rc == 0) {
		return rc;
	}
	return -ENOENT;
}

int etc_device_find_nack(etc_device_record_reading_callback reading_callback, void *data)
{
	int rc = 0;
	int newest_id = etc_device_record_table.newest.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			etc_device_record_table.newest.element_idx + ETC_RECORD_HEADER;
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER;
	uint16_t min_id = ETC_RECORD_HEADER;
	uint16_t last_id = ram_nack_record_id;
	uint16_t check_id = 0;
	bool find_next = false;
	LOG_INF("Reclaim is running %d", etc_reclaim_info.flag_in_process);

	if (etc_reclaim_info.flag_in_process == 1) {
		return etc_device_reclaim_data(reading_callback, data);
	}

	if (last_id == newest_id) {
		return 0;
	}

	check_id = last_id == 0 ? min_id : last_id + 1;
	if (check_id > max_id) {
		check_id = min_id;
	}

next_id:
	LOG_DBG("Last ID %u - Check ID %d - New ID %d", last_id, check_id, newest_id);

	rc = etc_nvs_read(check_id, &etc_device_record_header, sizeof(etc_device_record_header));
	if (rc == 0) {
		if (etc_device_record_header.ack == 0) {
			if (reading_callback) {
				rc = reading_callback(check_id, data);
			}
			if (rc <= 0) {
				find_next = true;
			}
		} else {
			find_next = true;
		}
	} else {
		LOG_WRN("Error id %d - error %d", check_id, rc);
		if (rc == -ENOENT) {
			find_next = true;
		}
	}

	if (find_next) {
		find_next = false;
		if (newest_id != check_id) {
			/* Increase the ram_nack_record_id */
			check_id += 1;
			if (check_id > max_id) {
				check_id = min_id;
			}
			goto next_id;
		} 
	}

	return rc;
}

static int etc_device_record_reading(uint16_t record_id, void *data)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	union etc_device_record *record = (union etc_device_record *)data;
	struct etc_device_record_index index;
	index.sector_idx = (record_id - ETC_RECORD_HEADER) / ETC_RECORD_MAX_PER_SECTOR;
	index.element_idx =
		(record_id - ETC_RECORD_HEADER) - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
	uint32_t record_addr = (record_fs.offset) + index.sector_idx * record_fs.sector_size +
			       index.element_idx * ETC_DEVICE_RECORD_SIZE;
	LOG_DBG("Record to read data %d (0x%08x) (%d,%d)", record_id, record_addr, index.sector_idx,
		index.element_idx);
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
	etc_device_record_table.last_nack_record_id = record_id;
	ram_nack_record_id = record_id;
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
		LOG_DBG("Updated the last NACK record id %d successful", record_id);
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

	if (etc_device_record_table.total < ETC_RECORD_MAX_RECORD) {
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

int etc_device_write_setting(uint16_t setting_id, const void *setting, int setting_size)
{
	return etc_nvs_write(setting_id, setting, setting_size);
}

int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size)
{
	LOG_DBG("Read setting ID %d", setting_id);
	return etc_nvs_read(setting_id, setting, setting_size);
}

int etc_device_read_setting_with_len(uint16_t setting_id, void *setting, int setting_size)
{
	LOG_DBG("Read setting ID %d", setting_id);
	return etc_nvs_read_with_len(setting_id, setting, setting_size);
}


enum etc_device_mode etc_device_get_mode(void)
{
	return etc_get_device_mode();
}

bool etc_device_is_logger_lora(void)
{
	return (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER);
}

int etc_device_get_rx_timeout(void)
{
	int rx_duration = etc_get_rx_duration_secs();
	return rx_duration == 0 ? ETC_RECORD_DEFAULT_RX_DURATION_SECONDS
				: rx_duration;
}

int etc_device_get_log_interval_second(void) 
{
	int second = etc_get_log_interval_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS
				: second;
}

int etc_device_get_tx_interval_second(void) 
{
	int second = etc_get_tx_interval_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_TX_INTERVAL_SECONDS : second;
}

int etc_device_get_tx_probe_second(void) 
{
	int second = etc_get_tx_probe_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_TX_PROBE_SECONDS : second;
}

void etc_device_set_job(enum etc_logger_job job) {
	logger_job = job;
}

enum etc_logger_job etc_device_get_job(void) {
	return logger_job;
}

const struct device* etc_device_get_record(void) {
	return record_fs.flash_device;
}

size_t etc_device_get_record_size(void) {
	return FLASH_AREA_SIZE(ETC_RECORD_NODE_LABEL);
}

off_t etc_device_get_record_offset(void) {
	return record_fs.offset;
}

size_t etc_device_get_record_max_element_index(void) {
	return ETC_RECORD_MAX_PER_SECTOR;
}

size_t etc_device_get_record_max_sector_index(void) {
	return ETC_RECORD_MAX_SECTOR;
}

size_t etc_device_get_record_element_size(void) {
	return sizeof(union etc_device_record);
}

struct etc_device_record_table etc_device_get_record_status(void) {
	return etc_device_record_table;
}

int etc_device_get_record_header(uint8_t element, uint8_t sector, union etc_device_record_header *header) {
	uint16_t record_id = sector * ETC_RECORD_MAX_PER_SECTOR + element + ETC_RECORD_HEADER;
	return etc_nvs_read(record_id, header, sizeof(union etc_device_record_header));
}

static int etc_device_update_reclaim(uint16_t record_id, int start_time, int stop_time) {
	int rc = 0;
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	memset(&etc_device_record_header, 0, sizeof(etc_device_record_header));
	union etc_device_record record;
	rc = etc_device_record_reading(record_id, buf);
	if (rc == record_id) {
		memcpy(record.data, buf, ETC_DEVICE_RECORD_SIZE);
		if ((start_time <= record.timestamp) && (record.timestamp <= stop_time)) {
			if (etc_reclaim_info.start_index == 0) {
				etc_reclaim_info.start_index = record_id;
				etc_reclaim_info.stop_index = record_id;
			} else {
				etc_reclaim_info.stop_index = record_id;
			}
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

int etc_device_reclaim_record(int start_time, int stop_time) {
	if (start_time > stop_time) {
		return -EINVAL;
	}

	int rc = 0;
	LOG_DBG("Request to reclaim %d %d", start_time, stop_time);
	etc_reclaim_info.start_index = 0;
	etc_reclaim_info.stop_index = 0;
	uint16_t oldest_id = ETC_RECORD_MAX_SECTOR * etc_device_record_table.oldest.sector_idx +
			  etc_device_record_table.oldest.element_idx + ETC_RECORD_HEADER;
	uint16_t newest_id = ETC_RECORD_MAX_SECTOR * etc_device_record_table.newest.sector_idx +
			  etc_device_record_table.newest.element_idx + ETC_RECORD_HEADER;
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER;
	uint16_t min_id = ETC_RECORD_HEADER;
	if (oldest_id < newest_id) {
		for (int i = oldest_id; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim information");
				return rc;
			}
		}
	} else {
		for (int i = oldest_id; i < max_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim information");
				return rc;
			}
		}
		for (int i = min_id; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim information");
				return rc;
			}
		}
	}

	if ((rc == 0) && (etc_reclaim_info.start_index != 0) && (etc_reclaim_info.stop_index != 0)) {
		etc_reclaim_info.flag_in_process = 1U;
		etc_reclaim_info.current_index = etc_reclaim_info.start_index;
		rc = etc_nvs_write(ETC_RECORD_RECLAIM, &etc_reclaim_info,
				   sizeof(etc_reclaim_info));
		if (rc != 0) {
			LOG_ERR("Failed to write reclaim info");
		} else {
			LOG_INF("Updated the reclaim info successful %d %d", etc_reclaim_info.start_index, etc_reclaim_info.stop_index);
		}
	}
	return rc;
}

int etc_device_reclaim_work(int start_time, int stop_time) {
	return 0;
}

int etc_device_erase_cfg(void) {
	int rc = 0;
	/* Erase config, record and reclaim */
	for (int id = ETC_CONFIG_ID; id <= ETC_RECORD_RECLAIM; id ++ ) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase setting */
	for (int id = ETC_SETTING_TIME_MEASURE_INTERVAL_ID; id <= ETC_SETTING_TX_PROBE_SEC_ID; id ++ ) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase record stat */
	etc_device_record_table.newest.sector_idx = 0;
	etc_device_record_table.oldest.sector_idx = 0;
	etc_device_record_table.newest.element_idx = 0;
	etc_device_record_table.oldest.element_idx = 0;
	etc_device_record_table.total = 0;
	etc_device_record_table.last_nack_record_id = 0;
	ram_nack_record_id = 0;
	rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table,
			       sizeof(etc_device_record_table));
	__ASSERT_NO_MSG(rc == 0);
	return 0;
}

uint16_t etc_device_get_tx_logger_lora_offset_mins(void) {
	return tx_logger_lora_offset_mins;
}

uint16_t etc_device_get_tx_no_probe_offset_mins(void) {
	return tx_no_probe_offset_mins;
}

enum etc_device_transmit_type etc_device_get_transmit_type_cloud(
	enum etc_sensor_status probeStatus)
{
	enum etc_device_mode deviceMode = etc_device_get_mode();
	enum etc_power_mode_e powerMode = etc_get_power_mode();
	for (int i = 0; i < ARRAY_SIZE(cloud_matrix_combinations); i++) {
		if (cloud_matrix_combinations[i].device_mode == deviceMode &&
			cloud_matrix_combinations[i].power_mode == powerMode) 
		{
			if (cloud_matrix_combinations[i].probe_status == SENSOR_NA) {
				return cloud_matrix_combinations[i].transmit_type;
			} else if (cloud_matrix_combinations[i].probe_status == probeStatus) {
				return cloud_matrix_combinations[i].transmit_type;
			}
		}
	}
	return ETC_DEVICE_TRANSMIT_INVALID; // Combination not found
}

enum etc_device_transmit_type etc_device_get_transmit_type_lora(
	enum etc_sensor_status probeStatus)
{
	enum etc_device_mode deviceMode = etc_device_get_mode();
	enum etc_power_mode_e powerMode = etc_get_power_mode();
	for (int i = 0; i < ARRAY_SIZE(lora_matrix_combinations); i++) {
		if (lora_matrix_combinations[i].device_mode == deviceMode &&
			lora_matrix_combinations[i].power_mode == powerMode) 
		{
			if (lora_matrix_combinations[i].probe_status == SENSOR_NA) {
				return lora_matrix_combinations[i].transmit_type;
			} else if (lora_matrix_combinations[i].probe_status == probeStatus) {
				return lora_matrix_combinations[i].transmit_type;
			}
		}
	}
	return ETC_DEVICE_TRANSMIT_INVALID; // Combination not found
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_get_record_reading(uint16_t record_id, void *data)
{
	uint16_t *nack_counter = (uint16_t *)data;
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

static int cmd_get_nack_reading(const struct shell *shell, uint16_t record_id)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	uint8_t msg[ETC_DEVICE_RECORD_SIZE * 2 + 1] = {0x00};
	struct etc_device_record_index index;
	index.sector_idx = (record_id - ETC_RECORD_HEADER) / ETC_RECORD_MAX_PER_SECTOR;
	index.element_idx =
		(record_id - ETC_RECORD_HEADER) - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
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

static int cmd_get_nack_id_list_reading(uint16_t record_id, void *data)
{
	const struct shell *shell = (void *)data;
	shell_print(shell, "%d", record_id - ETC_RECORD_HEADER);
	return 0;
}

static int cmd_get_nack_id_list(const struct shell *shell, size_t argc, char **argv)
{
	int rc = etc_device_find_nack(cmd_get_nack_id_list_reading, (void *)shell);
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
	etc_device_record_table.last_nack_record_id = 0;
	ram_nack_record_id = 0;
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
		int buf_len =
			snprintf(buf, sizeof(buf), "%u,%1.2f,", record.timestamp, record.battery);
		for (int i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
			if (data_codec_compare_temperature_is_valid(record.sensor[i])) {
				buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "%2.2f,",
						    record.sensor[i]);
			} else {
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

static int cmd_reclaim_record(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 3) {
		int start_time = atoi(argv[1]);
		int stop_time = atoi(argv[2]);
		int rc = etc_device_reclaim_record(start_time, stop_time);
		if (rc != 0) {
			shell_error(shell, "Failed to reclaim record");
		} else {
			shell_info(shell, "Reclaimed record success");
			shell_info(shell, "Start ID %d - Stop %d", etc_reclaim_info.start_index, etc_reclaim_info.stop_index);
		}
	} else {
		shell_error(shell, "Invalid input parameter for reclaim record");
	}

	return 0;
}

static int cmd_generate_record(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 2) {
		int num_of_sample = atoi(argv[1]);
		extern void ui_module_test_data_request(int num_of_sample);
		ui_module_test_data_request(num_of_sample);
	} else {
		shell_error(shell, "Invalid input parameter for generating record");
	}

	return 0;
}

static int cmd_erase_configuration(const struct shell *shell, size_t argc, char **argv)
{
	int rc = nvs_clear(&etc_fs);
	if (rc != 0) {
		shell_error(shell, "Failed to erase the configuration");
	} else {
		shell_print(shell, "Erased configuration successfully");
		shell_print(shell, "Please reboot the device after erasing the configuration");
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
	SHELL_CMD(reclaim, NULL, "Reclaim ", cmd_reclaim_record),
	SHELL_CMD(generate, NULL, "Generate a certain number of samples to fill up the flash ", cmd_generate_record),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(record, &sub_record, "ETC Record Management", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_config,
	SHELL_CMD(erase, NULL, "Erase all configuration - development only", cmd_erase_configuration),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(config, &sub_config, "ETC Configuration Management", NULL);

#endif
