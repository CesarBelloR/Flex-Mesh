#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/retained_mem.h>
LOG_MODULE_REGISTER(etc_device_record, CONFIG_ETC_APP_LOG_LEVEL);

#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include "etc_device.h"
#include "etc_device_record.h"
#include "cloud/cloud_codec/data_codec.h"

#define ETC_DEVICE_RECORD_PREFIX     "etc:record"
#define ETC_DEVICE_RECORD_NODE_LABEL etc_records_storage

struct etc_device_reclaim_info {
	uint16_t current_index;
	uint16_t start_index;
	uint16_t stop_index;
	uint16_t flag_in_process;
};

const static struct device *retained_ram_dev = DEVICE_DT_GET(DT_ALIAS(record_header_ram));
static void etc_device_save_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(etc_device_save_work, etc_device_save_work_handler);

static struct etc_device_reclaim_info etc_reclaim_info = {0x00};
static union etc_device_record_header etc_device_record_header;
static struct etc_device_record_table old_etc_device_record_table;
static struct nvs_fs record_fs;
static bool etc_device_need_save;

struct etc_device_record_data etc_device_record;
struct etc_device_record_data *pRecord = &etc_device_record;
struct etc_device_record_table *p_etc_device_record_table = &etc_device_record.record_stat;

#define RECORD_ARRAY_OFFSET(x) (offsetof(struct etc_device_record_data, record_bits) + x)
#define RECORD_FLAG_OFFSET     offsetof(struct etc_device_record_data, record_sync_flag)
#define RECORD_STAT_OFFSET     offsetof(struct etc_device_record_data, record_stat)

static int etc_device_record_get_ack_status(int record_id)
{
	uint8_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t byte_status = pRecord->record_bits[byte_pos];
	return (byte_status >> bit_pos) & 0x01;
}

static void etc_device_record_set_ack_status(int record_id, int status)
{
	etc_device_need_save = true;
	uint8_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t *byte_status = &pRecord->record_bits[byte_pos];
	if (status == 0) {
		*byte_status &= ~(1 << bit_pos);
	} else {
		*byte_status |= (1 << bit_pos);
	}
}

static int etc_device_on_set(const char *key, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
	if (!key) {
		return -ENOENT;
	}

	uint16_t len;
	len = read_cb(cb_arg, &etc_device_record, sizeof(etc_device_record));
	if (len <= 0) {
		LOG_ERR("No data to read");
		return -ENODATA;
	}

	if (len != sizeof(etc_device_record)) {
		LOG_ERR("Invalid length for device record");
		return -EINVAL;
	}

	return 0;
}

static void etc_device_record_load(void)
{
	char path[sizeof(ETC_DEVICE_RECORD_PREFIX "/acks") + 1];
	snprintk(path, sizeof(path), ETC_DEVICE_RECORD_PREFIX "/acks");
	settings_load_subtree(path);
}

static void etc_device_record_delete(void)
{
	char path[sizeof(ETC_DEVICE_RECORD_PREFIX "/acks") + 1];
	snprintk(path, sizeof(path), ETC_DEVICE_RECORD_PREFIX "/acks");
	settings_delete(path);
}

static struct settings_handler etc_device_record_settings = {
	.name = ETC_DEVICE_RECORD_PREFIX,
	.h_set = etc_device_on_set,
};

static void etc_device_set_status(int total, int start, int end, int ack) {
	uint16_t count = 0;
	uint16_t current_record = end;
	while (count < total) {
		if (ack) {
			etc_device_record_set_ack(current_record);
		} else {
			etc_device_record_set_nack(current_record);
		}

		if (current_record == start) {
			break;
		}

		current_record--;

		if (current_record < MIN_RECORD_ID) {
			current_record = MAX_RECORD_ID;
		}

		count++;
	}
}

static void etc_device_export_old_structure(void)
{
	int rc = etc_device_read_setting(ETC_RECORD_STAT, &old_etc_device_record_table,
		sizeof(old_etc_device_record_table));
	if (rc == -ENOENT) {
		/* No old structure here */
		return;
	}

	LOG_DBG("Export from old structure");
	memcpy(p_etc_device_record_table, &old_etc_device_record_table, sizeof(old_etc_device_record_table));
	uint16_t oldest_id = etc_device_record_get_oldest_id() + ETC_RECORD_HEADER;
	uint16_t newest_id = etc_device_record_get_latest_id() + ETC_RECORD_HEADER;
	uint16_t total_record = etc_device_record_get_total_record();
	uint16_t last_ack_record = old_etc_device_record_table.last_ack_record_id;
	uint16_t count = 0;
	LOG_INF("Old structure %d %d %d %d", oldest_id, newest_id, total_record, last_ack_record);
	/* If last ack record is zero, set all data to NACK */
	if (last_ack_record == 0) 
	{
		etc_device_set_status(total_record, oldest_id, newest_id, 0);
	} 
	/* Last ack record is latest ID, set all data to ACK */
	else if (last_ack_record == newest_id) 
	{
		etc_device_set_status(total_record, oldest_id, newest_id, 1);
	} 
	else 
	{
		uint16_t total_ack = 0;
		if (oldest_id <= last_ack_record) {
			total_ack = last_ack_record - oldest_id + 1;
		} else {
			total_ack = MAX_RECORD_ID - oldest_id + last_ack_record - MIN_RECORD_ID + 1;
		}
		LOG_INF("Ack %d - NACk %d", total_ack, total_record - total_ack);
		etc_device_set_status(total_ack, oldest_id, last_ack_record, 1);
		etc_device_set_status(total_record - total_ack, last_ack_record + 1, newest_id, 0);
	}

	LOG_DBG("Export success");
	/* Remove STAT */
	etc_device_delete_setting(ETC_RECORD_STAT);
}

static void etc_device_record_reset_stat(void) {
	pRecord->record_stat.newest.sector_idx = 0;
	pRecord->record_stat.oldest.sector_idx = 0;
	pRecord->record_stat.newest.element_idx = 0;
	pRecord->record_stat.oldest.element_idx = 0;
	pRecord->record_stat.total = 0;
}

void etc_device_record_init(void)
{
	int rc = 0;
	struct flash_pages_info info;
	rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("Failed to initialize settings subsystem, %d", rc);
		return;
	}

	rc = settings_register(&etc_device_record_settings);
	if (rc) {
		LOG_ERR("Failed to register settings, %d", rc);
		return;
	}

	record_fs.flash_device = FLASH_AREA_DEVICE(ETC_DEVICE_RECORD_NODE_LABEL);
	if (!device_is_ready(record_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", record_fs.flash_device->name);
		return;
	}

	record_fs.offset = FLASH_AREA_OFFSET(ETC_DEVICE_RECORD_NODE_LABEL);
	rc = flash_get_page_info_by_offs(record_fs.flash_device, record_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}
	record_fs.sector_size = info.size;
	record_fs.sector_count = (FLASH_AREA_SIZE(ETC_DEVICE_RECORD_NODE_LABEL) / info.size);

	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)record_fs.offset,
		FLASH_AREA_SIZE(ETC_DEVICE_RECORD_NODE_LABEL), info.size, record_fs.sector_count);
	rc = retained_mem_read(retained_ram_dev, 0, (uint8_t *)pRecord,
				   sizeof(struct etc_device_record_data));

	if ((rc) || (pRecord->record_sync_flag != ETC_DEVICE_RECORD_FLAG)) {
		/* Clean up the memory RAM in no-init region */
		retained_mem_clear(retained_ram_dev);
		/* Clean up the record in app RAM */
		memset(pRecord, 0, sizeof(struct etc_device_record_data));
		/* Reload from setting subsys - Don't need to check the return here */
		etc_device_record_load();
		pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
		/* Reset stat record */
		etc_device_record_reset_stat();
		/* Export old structure if it is available */
		etc_device_export_old_structure();
		/* Save it */
		rc = retained_mem_write(retained_ram_dev, RECORD_FLAG_OFFSET,
					(uint8_t *)&pRecord->record_sync_flag,
					sizeof(pRecord->record_sync_flag));
		if (rc) {
			LOG_ERR("Failed to write data - err %d", rc);
		}
	} else {
		LOG_DBG("Record is ready");
	}

	rc = etc_device_read_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
					 sizeof(etc_reclaim_info));
	if (rc != 0) {
		etc_reclaim_info.current_index = 0;
		etc_reclaim_info.start_index = 0;
		etc_reclaim_info.stop_index = 0;
		etc_reclaim_info.flag_in_process = 0;
		rc = etc_device_write_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
						  sizeof(etc_reclaim_info));
		if (rc != 0) {
			LOG_ERR("Failed to write reclaim info");
		} else {
			LOG_INF("Initialized the reclaim info successful");
		}
	}

	LOG_DBG("Last record stat as below: ");
	LOG_DBG("\tNewest record (%d,%d)", p_etc_device_record_table->newest.sector_idx,
		p_etc_device_record_table->newest.element_idx);
	LOG_DBG("\tOldest record (%d,%d)", p_etc_device_record_table->oldest.sector_idx,
		p_etc_device_record_table->oldest.element_idx);
	LOG_DBG("\tTotal record %d", p_etc_device_record_table->total);

	LOG_DBG("Reclaim information: %d %d", etc_reclaim_info.current_index,
		etc_reclaim_info.flag_in_process);
	LOG_DBG("\tStart index: %d", etc_reclaim_info.start_index);
	LOG_DBG("\tStop index %d", etc_reclaim_info.stop_index);
	k_work_schedule(&etc_device_save_work, K_NO_WAIT);
}

int etc_device_record_get_ack(int record_id)
{
	return etc_device_record_get_ack_status(record_id);
}

void etc_device_record_set_ack(int record_id)
{
	etc_device_record_set_ack_status(record_id, 1);
	uint8_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position),
					(uint8_t *)&pRecord->record_bits[byte_position],
					sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

void etc_device_record_set_nack(int record_id)
{
	etc_device_record_set_ack_status(record_id, 0);
	uint8_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position),
					(uint8_t *)&pRecord->record_bits[byte_position],
					sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

int etc_device_record_get_nack(int new_record, int old_record, int num_record)
{
	// Validate the parameters
	if (num_record <= 0) {
		return -1; // invalid parameters
	}

	int count = 0;
	int current_record = new_record;

	while (count < num_record) {
		if (!etc_device_record_get_ack_status(current_record)) { // Check for NACK
			return current_record;
		}

		if (current_record == old_record) {
			break;
		}

		current_record--;

		if (current_record < 0) {
			current_record = MAX_RECORD_NO_OFFSET_ID;
		}

		count++;
	}

	return -1; // No NACK found within the specified range
}

static uint8_t *etc_device_record_get_array_ack(void)
{
	return pRecord->record_bits;
}

void etc_device_record_save(void)
{
	char path[sizeof(ETC_DEVICE_RECORD_PREFIX "/acks") + 1];
	snprintk(path, sizeof(path), ETC_DEVICE_RECORD_PREFIX "/acks");
	if (settings_save_one(path, &etc_device_record, sizeof(etc_device_record))) {
		LOG_ERR("Failed to store %s", path);
	} else {
		LOG_DBG("Permanently stored %s", path);
	}
}

static void etc_device_save_work_handler(struct k_work *work)
{
	if (etc_device_need_save) {
		etc_device_need_save = false;
		etc_device_record_save();
	}
	k_work_reschedule(&etc_device_save_work, K_SECONDS(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC));
}

uint16_t etc_device_record_get_latest_id(void)
{
	return ETC_RECORD_MAX_PER_SECTOR * pRecord->record_stat.newest.sector_idx +
		   pRecord->record_stat.newest.element_idx;
}

uint16_t etc_device_record_get_oldest_id(void)
{
	return ETC_RECORD_MAX_PER_SECTOR * pRecord->record_stat.oldest.sector_idx +
		   pRecord->record_stat.oldest.element_idx;
}

uint16_t etc_device_record_get_total_record(void)
{
	return pRecord->record_stat.total;
}

off_t etc_deviced_record_get_addr_offset_by_index(struct etc_device_record_index index)
{
	return (record_fs.offset) + index.sector_idx * record_fs.sector_size +
		   index.element_idx * ETC_DEVICE_RECORD_SIZE;
}

uint16_t etc_device_record_get_id_by_index(struct etc_device_record_index index)
{
	return index.sector_idx * ETC_RECORD_MAX_PER_SECTOR + index.element_idx;
}

struct etc_device_record_index etc_device_get_index_by_id(uint16_t record_id)
{
	struct etc_device_record_index index;
	index.sector_idx = (record_id) / ETC_RECORD_MAX_PER_SECTOR;
	index.element_idx = record_id - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
	return index;
}

static bool etc_device_record_buffer_is_erased(uint8_t *buf, uint8_t length)
{
	for (int i = 0; i < length; i++) {
		if (buf[i] != 0xFF) {
			return false;
		}
	}

	return true;
}

int etc_device_record_write_data(off_t addr, void *data, int data_len)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	__ASSERT_NO_MSG(data != NULL);
	int rc = flash_read(record_fs.flash_device, addr, buf, data_len);
	if (rc != 0) {
		LOG_ERR("Error in reading flash err %d", rc);
		return rc;
	}

	if (etc_device_record_buffer_is_erased(buf, ETC_DEVICE_RECORD_SIZE) == false) {
		/* Need to erase flash */
		LOG_WRN("Data in address is not empty 0x%08x", (uint32_t)addr);
		LOG_HEXDUMP_DBG(buf, ETC_DEVICE_RECORD_SIZE, "DUMP");
		uint32_t offset_sector = addr - addr % record_fs.sector_size;
		rc = flash_erase(record_fs.flash_device, offset_sector, record_fs.sector_size);
		if (rc != 0) {
			LOG_ERR("Error in erasing flash err %d 0x%08x", rc, offset_sector);
			return rc;
		}
	}

	rc = flash_write(record_fs.flash_device, addr, data, data_len);
	if (rc != 0) {
		LOG_ERR("Error in writing flash err %d", rc);
		return rc;
	}
	LOG_DBG("Write data success");
	return 0;
}

int etc_device_record_read_data(off_t addr, void *data, int data_len)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	int rc = flash_read(record_fs.flash_device, addr, data, data_len);
	if (rc != 0) {
		LOG_ERR("Error in reading flash err %d", rc);
		return rc;
	}

	return 0;
}

uint16_t etc_device_record_get_num_ack(void)
{
	uint16_t oldest_id = etc_device_record_get_oldest_id();
	uint16_t total_record = etc_device_record_get_total_record();
	uint16_t count = 0;
	uint16_t ack = 0;
	int current_record = etc_device_record_get_latest_id();
	while (count < total_record) {
		if (etc_device_record_get_ack_status(current_record) == 1) {
			ack += 1;
		}

		if (current_record == oldest_id) {
			break;
		}

		current_record--;

		if (current_record < MIN_RECORD_NO_OFFSET_ID) {
			current_record = MAX_RECORD_NO_OFFSET_ID;
		}

		count++;
	}
	return ack;
}

void etc_device_record_save_stat(void)
{
	int rc = retained_mem_write(retained_ram_dev, RECORD_STAT_OFFSET,
					(uint8_t *)&pRecord->record_stat, sizeof(pRecord->record_stat));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

int etc_device_record_reading(uint16_t record_id, void *data)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	union etc_device_record *record = (union etc_device_record *)data;
	struct etc_device_record_index index = etc_device_get_index_by_id(record_id);
	uint32_t record_addr = (uint32_t)etc_deviced_record_get_addr_offset_by_index(index);
	LOG_DBG("Record to read data %d (0x%08x) (%d,%d)", record_id, record_addr, index.sector_idx,
		index.element_idx);
	int rc = etc_device_record_read_data(record_addr, buf, ETC_DEVICE_RECORD_SIZE);
	if (rc) {
		LOG_ERR("Can't read record data err %d", rc);
		return rc;
	}
	memcpy(record->data, buf, ETC_DEVICE_RECORD_SIZE);
	return record_id;
}

void etc_device_record_clean_up(void)
{
	retained_mem_clear(retained_ram_dev);
	etc_device_record_delete();
	memset(pRecord, 0, sizeof(struct etc_device_record_data));
	pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
	etc_device_record_reset_stat();
	int rc = retained_mem_write(retained_ram_dev, 0, (uint8_t *)&etc_device_record,
					sizeof(etc_device_record));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

static int etc_device_update_reclaim(uint16_t record_id, int start_time, int stop_time)
{
	int rc = 0;
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
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

int etc_device_record_reclaim(int start_time, int stop_time)
{
	if (start_time > stop_time) {
		return -EINVAL;
	}

	int rc = 0;
	LOG_DBG("Request to reclaim %d %d", start_time, stop_time);
	etc_reclaim_info.start_index = 0;
	etc_reclaim_info.stop_index = 0;
	uint16_t oldest_id = etc_device_record_get_oldest_id();
	uint16_t newest_id = etc_device_record_get_latest_id();

	if (oldest_id < newest_id) {
		for (int i = oldest_id; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				return rc;
			}
		}
	} else {
		for (int i = oldest_id; i < MAX_RECORD_NO_OFFSET_ID; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				return rc;
			}
		}
		for (int i = MIN_RECORD_NO_OFFSET_ID; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc != 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				return rc;
			}
		}
	}

	if ((rc == 0) && (etc_reclaim_info.start_index != 0) &&
		(etc_reclaim_info.stop_index != 0)) {
		etc_reclaim_info.flag_in_process = 1U;
		etc_reclaim_info.current_index = etc_reclaim_info.start_index;
		rc = etc_device_write_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
						  sizeof(etc_reclaim_info));
		if (rc != 0) {
			LOG_ERR("Failed to write reclaim info");
		} else {
			LOG_INF("Updated the reclaim info successful %d %d",
				etc_reclaim_info.start_index, etc_reclaim_info.stop_index);
		}
	}
	return rc;
}

struct etc_device_record_table *etc_device_record_get_status(void)
{
	return p_etc_device_record_table;
}

int etc_device_record_get_header(uint8_t element, uint8_t sector,
				 union etc_device_record_header *header)
{
	__ASSERT_NO_MSG(header != NULL);
	uint16_t record_id = sector * ETC_RECORD_MAX_PER_SECTOR + element;
	int ack = etc_device_record_get_ack(record_id);
	header->ack = ack;
	return 0;
}

struct etc_device_record_index etc_device_record_get_next_index(void)
{
	if (p_etc_device_record_table->total == 0) {
		p_etc_device_record_table->total += 1;
		return p_etc_device_record_table->newest;
	}

	if (p_etc_device_record_table->newest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
		p_etc_device_record_table->newest.element_idx += 1;
	} else {
		p_etc_device_record_table->newest.element_idx = 0;
		if (p_etc_device_record_table->newest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
			p_etc_device_record_table->newest.sector_idx += 1;
		} else {
			p_etc_device_record_table->newest.sector_idx = 0;
		}
	}

	if (p_etc_device_record_table->total < ETC_RECORD_MAX_RECORD) {
		p_etc_device_record_table->oldest.element_idx = 0;
		p_etc_device_record_table->oldest.sector_idx = 0;
		p_etc_device_record_table->total += 1;
	} else {
		if (p_etc_device_record_table->oldest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
			p_etc_device_record_table->oldest.element_idx += 1;
		} else {
			p_etc_device_record_table->oldest.element_idx = 0;
			if (p_etc_device_record_table->oldest.sector_idx <
				ETC_RECORD_MAX_SECTOR - 1) {
				p_etc_device_record_table->oldest.sector_idx += 1;
			} else {
				p_etc_device_record_table->oldest.sector_idx = 0;
			}
		}
	}

	return p_etc_device_record_table->newest;
}

static int etc_device_reclaim_data(etc_device_record_reading_callback reading_callback, void *data)
{
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
				rc = etc_device_write_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
								  sizeof(etc_reclaim_info));
				if (rc != 0) {
					LOG_ERR("Failed to write reclaim info");
				} else {
					LOG_INF("Initialized the reclaim info successful");
				}
			}
		} else {
			if (etc_reclaim_info.current_index >= etc_reclaim_info.start_index &&
				etc_reclaim_info.current_index <= MAX_RECORD_NO_OFFSET_ID) {
				rc = reading_callback(etc_reclaim_info.current_index, data);
				LOG_DBG("Reclaim at %d", etc_reclaim_info.current_index);
				if (rc > 0) { // Return record_id;
					if (etc_reclaim_info.current_index ==
						MAX_RECORD_NO_OFFSET_ID) {
						etc_reclaim_info.current_index =
							MIN_RECORD_NO_OFFSET_ID;
					} else {
						etc_reclaim_info.current_index += 1;
					}
					return rc;
				} else {
					/* No action required */
				}
			}

			if (etc_reclaim_info.current_index >= MIN_RECORD_NO_OFFSET_ID &&
				etc_reclaim_info.current_index <= etc_reclaim_info.stop_index) {
				rc = reading_callback(etc_reclaim_info.current_index, data);
				LOG_DBG("Reclaim at %d", etc_reclaim_info.current_index);
				if (rc > 0) { // Return record_id;
					if (etc_reclaim_info.current_index ==
						etc_reclaim_info.stop_index) {
						etc_reclaim_info.flag_in_process = 0;
						etc_reclaim_info.current_index = 0;
						rc = etc_device_write_setting(
							ETC_RECORD_RECLAIM, &etc_reclaim_info,
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

int etc_device_record_find_nack(etc_device_record_reading_callback reading_callback, void *data,
				bool *active_reclaim)
{
	int rc = 0;
	int newest_id = etc_device_record_get_latest_id();
	int oldest_id = etc_device_record_get_oldest_id();

	if (etc_reclaim_info.flag_in_process == 1) {
		if (active_reclaim != NULL) {
			*active_reclaim = true;
		}
		return etc_device_reclaim_data(reading_callback, data);
	}

	if (active_reclaim != NULL) {
		*active_reclaim = false;
	}

	rc = etc_device_record_get_nack(newest_id, oldest_id, p_etc_device_record_table->total);
	if (rc >= 0) {
		LOG_DBG("Get NACK %d", rc);
		if (reading_callback) {
			rc = reading_callback(rc, data);
			LOG_DBG("Read data %d", rc);
			if (rc >= 0) {
				return ETC_RECORD_ID_HEADER(rc);
			}
		}
	}
	return rc;
}

const struct device *etc_device_record_get(void)
{
	return record_fs.flash_device;
}

size_t etc_device_record_get_size(void)
{
	return FLASH_AREA_SIZE(ETC_DEVICE_RECORD_NODE_LABEL);
}

off_t etc_device_record_get_offset(void)
{
	return record_fs.offset;
}

size_t etc_device_record_get_max_element_index(void)
{
	return ETC_RECORD_MAX_PER_SECTOR;
}

size_t etc_device_record_get_max_sector_index(void)
{
	return ETC_RECORD_MAX_SECTOR;
}

size_t etc_device_record_get_element_size(void)
{
	return sizeof(union etc_device_record);
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
	uint16_t total = p_etc_device_record_table->total;
	uint16_t nack = 0;
	int rc = etc_device_record_find_nack(cmd_get_record_reading, &nack, NULL);
	if (rc != 0) {
		shell_error(shell, "Can't query NACK record");
		return 0;
	}

	shell_print(shell, "Number of total records: %d", total);
	shell_print(shell, "Number of ack records: %d", total - nack);
	shell_print(shell, "Number of nack records: %d", nack);
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
	uint8_t *array_list = etc_device_record_get_array_ack();
	shell_hexdump(shell, array_list, ETC_DEVICE_RECORD_BUF_SIZE);
	return 0;
}

static int cmd_clean_records(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_clean_up();
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
		int rc = etc_device_record_reclaim(start_time, stop_time);
		if (rc != 0) {
			shell_error(shell, "Failed to reclaim record");
		} else {
			shell_info(shell, "Reclaimed record success");
			shell_info(shell, "Start ID %d - Stop %d", etc_reclaim_info.start_index,
				   etc_reclaim_info.stop_index);
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

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_record,
	SHELL_CMD(report, NULL, "Report number record (total/ack/nack)", cmd_num_report_record),
	SHELL_CMD(nack_dump, NULL, "Dump nack list", cmd_get_nack_id_list),
	SHELL_CMD(clean, NULL, "Clean the records", cmd_clean_records),
	SHELL_CMD(parser, NULL, "Parser the hex record", cmd_parser_hex_record),
	SHELL_CMD(reclaim, NULL, "Reclaim ", cmd_reclaim_record),
	SHELL_CMD(generate, NULL, "Generate a certain number of samples to fill up the flash ",
		  cmd_generate_record),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(record, &sub_record, "ETC Record Management", NULL);
#endif /* CONFIG_SHELL */