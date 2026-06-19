#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/crc.h>
#include <zephyr/drivers/retained_mem.h>
LOG_MODULE_REGISTER(etc_device_record, CONFIG_ETC_APP_LOG_LEVEL);

#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include "cloud/cloud_codec/data_codec.h"
#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_memfault.h"

#define ETC_DEVICE_RECORD_PREFIX     "etc:record"
#define ETC_DEVICE_RECORD_NODE_LABEL etc_records_storage

struct etc_device_reclaim_info {
	int16_t current_index;
	int16_t start_index;
	int16_t stop_index;
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

static atomic_t etc_reclaim_status = ATOMIC_INIT(false);

struct etc_device_record_data etc_device_record;
struct etc_device_record_data *pRecord = &etc_device_record;
struct etc_device_record_table *p_etc_device_record_table = &etc_device_record.record_stat;

static struct etc_device_record_backup_data etc_device_record_backup;

#define RECORD_ARRAY_OFFSET(x)	(offsetof(struct etc_device_record_data, record_bits) + x)
#define RECORD_FLAG_OFFSET	(offsetof(struct etc_device_record_data, record_sync_flag))
#define RECORD_STAT_OFFSET	(offsetof(struct etc_device_record_data, record_stat))
#define RECORD_BACKUP_OFFSET	(ETC_RECORD_BACKUP_OFFSET_IN_RAM)
#define RECORD_CRC_OFFSET	(retained_mem_size(retained_ram_dev) - sizeof(uint32_t))

static int etc_device_record_get_ack_status(int record_id)
{
	uint16_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t byte_status = pRecord->record_bits[byte_pos];
	return (byte_status >> bit_pos) & 0x01;
}

static void etc_device_record_set_ack_status(int record_id, int status)
{
	etc_device_need_save = true;
	uint16_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t *byte_status = &pRecord->record_bits[byte_pos];
	if (status == 0) {
		*byte_status &= ~(1 << bit_pos);
	} else {
		*byte_status |= (1 << bit_pos);
	}
}

static void etc_device_record_reset_ack(void) 
{
	uint16_t oldest_id = etc_device_record_get_oldest_id();
	uint16_t newest_id = etc_device_record_get_latest_id();
	/* Reset all recort_bits */
	memset(pRecord->record_bits, 0, sizeof(pRecord->record_bits));
	uint16_t index = oldest_id;
	uint16_t total_id = 0;
	do {
		etc_device_record_set_ack(index);
		total_id += 1;
		// Wrap around using modulo
		index = (index + 1) % (MAX_RECORD_NO_OFFSET_ID + 1); 
	} while (index != (newest_id + 1) % (MAX_RECORD_NO_OFFSET_ID + 1)); // Stop after reaching newest+1
	pRecord->record_stat.total = total_id;
}


static void etc_device_record_reset_stat(void)
{
	pRecord->record_stat.newest.sector_idx = 0;
	pRecord->record_stat.oldest.sector_idx = 0;
	pRecord->record_stat.newest.element_idx = 0;
	pRecord->record_stat.oldest.element_idx = 0;
	pRecord->record_stat.total = 0;
}

static void etc_device_record_reset_reclaim(void) {
	etc_reclaim_info.start_index = -1;
	etc_reclaim_info.stop_index = -1;
	etc_reclaim_info.current_index = 0;
	etc_reclaim_info.flag_in_process = 0;
}

static int erase_all_record_data(void)
{
	off_t addr = record_fs.offset;
	size_t size = record_fs.sector_size * record_fs.sector_count;
	LOG_WRN("Erasing all data from 0x%08x to 0x%08x on %s", (uint32_t)addr,
		(uint32_t)(addr + size), record_fs.flash_device->name);
	int rc = flash_erase(record_fs.flash_device, addr, size);
	if (rc != 0) {
		LOG_ERR("Error in erasing flash err %d", rc);
		return rc;
	}
	return 0;
}

/**
 * Wipe all record data in flash if not wiped previously.
 */
static int etc_device_record_erase_all_data(void)
{
	bool erased_after_upgrade = false;
	int ret;
	ret = etc_device_read_setting(ETC_RECORDS_ERASED_AFTER_UPGRADE, &erased_after_upgrade, sizeof(erased_after_upgrade));
	if (ret < 0 && ret != -ENOENT) {
		LOG_ERR("Failed to read setting err %d", ret);
		return ret;
	}
	if (erased_after_upgrade) {
		LOG_INF("Not wiping records. Already wiped previously");
		ETC_MEMFAULT_TRACE_EVENT_WITH_STATUS(wipe_record_data, 0);
		return 0;
	}
	erase_all_record_data();
	ETC_MEMFAULT_TRACE_EVENT_WITH_STATUS(wipe_record_data, 1);
	erased_after_upgrade = true;
	ret = etc_device_write_setting(ETC_RECORDS_ERASED_AFTER_UPGRADE, &erased_after_upgrade, sizeof(erased_after_upgrade));
	if (ret != 0) {
		LOG_ERR("Failed to write setting err %d", ret);
		return ret;
	}
	return 0;
}

static int etc_device_on_set(const char *key, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
	if (!key) {
		LOG_WRN("Record is not found in setting");
		return -ENOENT;
	}

	if (etc_device_record.record_sync_flag == ETC_DEVICE_RECORD_FLAG) {
		/* Loaded */
		LOG_WRN("Sync Flag is available");
		return 0;
	}

	uint16_t len;
	struct etc_device_record_data record_data = {0};
	len = read_cb(cb_arg, &record_data, sizeof(record_data));
	if (len <= 0) {
		LOG_ERR("No data to read");
		return -ENODATA;
	}

	if ((len != sizeof(record_data)) && (len != sizeof(struct etc_device_record_data_old))) {
		LOG_ERR("Invalid length for device record %d", len);
		return -EINVAL;
	}

#if ETC_DEVICE_RECORD_BUF_SIZE == ETC_DEVICE_RECORD_BUF_SIZE_NEW
	if (len == sizeof(struct etc_device_record_data_old)) {
		LOG_WRN("Need to upgrade the record data");
		pRecord->record_stat = record_data.record_stat;
		memset(pRecord->record_bits, 0, sizeof(pRecord->record_bits));
		memcpy(pRecord->record_bits, record_data.record_bits, ETC_DEVICE_RECORD_BUF_SIZE_OLD);
		pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
	} else {
		memcpy(pRecord, &record_data, sizeof(record_data));
	}
#else
	memcpy(pRecord, &record_data, sizeof(record_data));
#endif
	if (pRecord->record_sync_flag != ETC_DEVICE_RECORD_FLAG) {
		LOG_WRN("Sync flag does not match, reset and wipe all record data");
		memset(pRecord, 0, sizeof(struct etc_device_record_data));
		etc_device_record_reset_stat();
		etc_device_record_erase_all_data();
		return -EINVAL;
	}
	
	LOG_DBG("\tNewest record (%d,%d)", p_etc_device_record_table->newest.sector_idx,
		p_etc_device_record_table->newest.element_idx);
	LOG_DBG("\tOldest record (%d,%d)", p_etc_device_record_table->oldest.sector_idx,
		p_etc_device_record_table->oldest.element_idx);
	LOG_DBG("\tTotal record %d", p_etc_device_record_table->total);
	etc_device_record_reset_ack();
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

static void etc_device_set_status(int total, int start, int end, int ack)
{
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

static int etc_device_export_old_structure(void)
{
	int rc = etc_device_read_setting(ETC_RECORD_STAT, &old_etc_device_record_table,
					 sizeof(old_etc_device_record_table));
	if (rc == -ENOENT) {
		/* No old structure here */
		LOG_DBG("No need to export old structure");
		return -ENOENT;
	}

	LOG_DBG("Export from old structure");
	memcpy(p_etc_device_record_table, &old_etc_device_record_table,
	       sizeof(old_etc_device_record_table));
	uint16_t oldest_id = etc_device_record_get_oldest_id() + ETC_RECORD_HEADER;
	uint16_t newest_id = etc_device_record_get_latest_id() + ETC_RECORD_HEADER;
	uint16_t total_record = etc_device_record_get_total_record();
	uint16_t last_ack_record = old_etc_device_record_table.last_ack_record_id;
	uint16_t count = 0;
	LOG_INF("Old structure %d %d %d %d", oldest_id, newest_id, total_record, last_ack_record);
	/* If last ack record is zero, set all data to NACK */
	if (last_ack_record == 0) {
		etc_device_set_status(total_record, oldest_id, newest_id, 0);
	}
	/* Last ack record is latest ID, set all data to ACK */
	else if (last_ack_record == newest_id) {
		etc_device_set_status(total_record, oldest_id, newest_id, 1);
	} else {
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
	return 0;
}

static void etc_device_sync_crc(void)
{
	uint32_t current_crc =
		crc32_ieee((const uint8_t *)pRecord, sizeof(struct etc_device_record_data));
	LOG_DBG("Saving CRC 0x%08x", current_crc);
	retained_mem_write(retained_ram_dev, RECORD_CRC_OFFSET, (uint8_t *)&current_crc,
			   sizeof(current_crc));
}

static int write_record_data_to_retained_mem(void)
{
	int rc;
	/* Set sync flag */
	pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
	/* Save it */
	rc = retained_mem_write(retained_ram_dev, 0, (uint8_t *)pRecord,
				sizeof(struct etc_device_record_data));
	if (rc) {
		LOG_ERR("Failed to write data to retained mem - err %d", rc);
	}
	etc_device_sync_crc();
	return rc;
}

static void etc_device_helpers_sync_record_from_ram(void) {
	int rc = retained_mem_read(retained_ram_dev, 0, (uint8_t *)pRecord,
				   sizeof(struct etc_device_record_data));
	if ((rc != 0) || (pRecord->record_sync_flag != ETC_DEVICE_RECORD_FLAG)) {
		LOG_WRN("No valid retained RAM memory here");
		/* Clean up the record in app RAM */
		memset(pRecord, 0, sizeof(struct etc_device_record_data));
		/* Reset stat record */
		etc_device_record_reset_stat();
		/* Force reload record */
		etc_device_record_load();
		rc = write_record_data_to_retained_mem();
		if (rc == 0) {
			LOG_DBG("Export record success");
		}
	} else {
		uint32_t current_crc =
			crc32_ieee((const uint8_t *)pRecord, sizeof(struct etc_device_record_data));
		uint32_t saved_crc = {0};
		rc = retained_mem_read(retained_ram_dev, RECORD_CRC_OFFSET, (uint8_t *)&saved_crc,
				       sizeof(saved_crc));
		if (rc || ((current_crc != saved_crc) && (saved_crc != 0))) {
			LOG_DBG("Retained data is not valid (0x%08x - 0x%08x)."
				"Reload record setting backup",
				current_crc, saved_crc);
			/* Reset the sync flag */
			etc_device_record.record_sync_flag = 0;
			etc_device_record_load();
			write_record_data_to_retained_mem();
		} else {
			LOG_DBG("Record is ready");
		}
	}

	rc = retained_mem_read(retained_ram_dev, ETC_RECORD_BACKUP_OFFSET_IN_RAM, 
			       (uint8_t *)&etc_device_record_backup,
			       sizeof(etc_device_record_backup));
	if ((rc) || (etc_device_record_backup.record_sync_flag != ETC_DEVICE_RECORD_FLAG)) {
		/* Clean up the record in app RAM */
		memset(&etc_device_record_backup, 0, sizeof(etc_device_record_backup));
		etc_device_record_backup.record_sync_flag = ETC_DEVICE_RECORD_FLAG;
		rc = retained_mem_write(retained_ram_dev, ETC_RECORD_BACKUP_OFFSET_IN_RAM,
					(uint8_t *)&etc_device_record_backup,
					sizeof(etc_device_record_backup));
		if (rc) {
			LOG_ERR("Failed to write data - err %d", rc);
		}
	} else {
		LOG_DBG("Record back-up is available space");
	}
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
	if (rc && rc != -EEXIST) {
		LOG_ERR("Failed to register settings, %d", rc);
		return;
	}

	record_fs.flash_device = FLASH_AREA_DEVICE(ETC_DEVICE_RECORD_NODE_LABEL);
	if (!device_is_ready(record_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", record_fs.flash_device->name);
		return;
	}

	record_fs.offset = FIXED_PARTITION_OFFSET(ETC_DEVICE_RECORD_NODE_LABEL);
	rc = flash_get_page_info_by_offs(record_fs.flash_device, record_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}
	record_fs.sector_size = info.size;
	record_fs.sector_count = (FIXED_PARTITION_SIZE(ETC_DEVICE_RECORD_NODE_LABEL) / info.size);

	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)record_fs.offset,
		FIXED_PARTITION_SIZE(ETC_DEVICE_RECORD_NODE_LABEL), info.size, record_fs.sector_count);
	/* Sync record from RAM if available */
	etc_device_helpers_sync_record_from_ram();

	rc = etc_device_read_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
				     sizeof(etc_reclaim_info));
	if (rc != 0) {
		etc_device_record_reset_reclaim();
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

void etc_device_record_erase_record_flash(void) {
	flash_erase(record_fs.flash_device, record_fs.offset, FIXED_PARTITION_SIZE(ETC_DEVICE_RECORD_NODE_LABEL));
}

int etc_device_record_get_ack(int record_id)
{
	return etc_device_record_get_ack_status(record_id);
}

void etc_device_record_set_ack(int record_id)
{
	etc_device_record_set_ack_status(record_id, 1);
	uint16_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position),
				    (uint8_t *)&pRecord->record_bits[byte_position],
				    sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
	etc_device_sync_crc();
}

void etc_device_record_set_nack(int record_id)
{
	etc_device_record_set_ack_status(record_id, 0);
	uint16_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position),
				    (uint8_t *)&pRecord->record_bits[byte_position],
				    sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
	etc_device_sync_crc();
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
	/* Do we need to sync when success ? */
	etc_device_sync_crc();
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

off_t etc_device_record_get_addr_offset_by_index(struct etc_device_record_index *index)
{
	return (record_fs.offset) + index->sector_idx * record_fs.sector_size +
	       index->element_idx * ETC_DEVICE_RECORD_SIZE;
}

off_t etc_device_record_get_addr_offset_by_id(uint16_t id) 
{
	struct etc_device_record_index record_index = etc_device_get_index_by_id(id);
	return etc_device_record_get_addr_offset_by_index(&record_index);
}

uint16_t etc_device_record_get_id_by_index(struct etc_device_record_index *index)
{
	return index->sector_idx * ETC_RECORD_MAX_PER_SECTOR + index->element_idx;
}

uint16_t etc_device_record_get_id_by_addr(off_t *addr) 
{
	struct etc_device_record_index offset_index = etc_device_get_index_by_addr_offset(addr);
	return etc_device_record_get_id_by_index(&offset_index);
}

struct etc_device_record_index etc_device_get_index_by_id(uint16_t record_id)
{
	struct etc_device_record_index index;
	index.sector_idx = (record_id) / ETC_RECORD_MAX_PER_SECTOR;
	index.element_idx = record_id - index.sector_idx * ETC_RECORD_MAX_PER_SECTOR;
	return index;
}

struct etc_device_record_index etc_device_get_index_by_addr_offset(off_t *offset)
{
	off_t start_addr = *offset - record_fs.offset;
	__ASSERT_NO_MSG(start_addr >= 0);
	struct etc_device_record_index index;

	index.sector_idx = (start_addr) / record_fs.sector_size;
	index.element_idx = ((start_addr) % record_fs.sector_size) / ETC_DEVICE_RECORD_SIZE;

	return index;
}

struct etc_device_record_index etc_device_get_next_index_byte_addr_offset(off_t *offset)
{
	struct etc_device_record_index offset_index = etc_device_get_index_by_addr_offset(offset);

	if (++offset_index.element_idx >= ETC_RECORD_MAX_PER_SECTOR) {
		offset_index.element_idx = 0;
		offset_index.sector_idx = (offset_index.sector_idx + 1) % ETC_RECORD_MAX_SECTOR;
	}

	return offset_index;
}

struct etc_device_record_index etc_device_get_previous_index_by_addr_offset(off_t *offset)
{
	struct etc_device_record_index offset_index = etc_device_get_index_by_addr_offset(offset);

	if (offset_index.element_idx-- == 0) {
		offset_index.element_idx = ETC_RECORD_MAX_PER_SECTOR - 1;
		offset_index.sector_idx = (offset_index.sector_idx == 0)
						  ? (ETC_RECORD_MAX_SECTOR - 1)
						  : (offset_index.sector_idx - 1);
	}

	return offset_index;
}

uint16_t etc_device_get_next_id_by_index(struct etc_device_record_index *current_index)
{
	uint16_t current_id = etc_device_record_get_id_by_index(current_index);
	uint16_t next_id = (current_id + 1) % MAX_RECORD_ID;
	return next_id;
}

static off_t etc_device_get_next_addr_byte_addr_offset(off_t *offset)
{
	struct etc_device_record_index next_index =
		etc_device_get_next_index_byte_addr_offset(offset);
	return etc_device_record_get_addr_offset_by_index(&next_index);
}

static off_t etc_device_get_previous_addr_byte_addr_offset(off_t *offset)
{
	struct etc_device_record_index previous_index =
		etc_device_get_previous_index_by_addr_offset(offset);
	return etc_device_record_get_addr_offset_by_index(&previous_index);
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

static int etc_device_record_validate_record(union etc_device_record previous,
					     union etc_device_record current)
{
	if (previous.timestamp == -1 || current.timestamp == -1) {
		/* TODO: Need to find better solution for this case */
		return -EINVAL;
	}

	if (current.timestamp < previous.timestamp) {
		/* If the previous record is newer than check record, erase it.*/
		LOG_DBG("%d", __LINE__);
		return -EINVAL;
	}

	return 0;
}

static int etc_device_record_recover_data(off_t *addr)
{
	union etc_device_record previous_record;
	union etc_device_record check_record;
	off_t offset_addr = *addr;
	off_t previous_offset = etc_device_get_previous_addr_byte_addr_offset(addr);

	int rc = flash_read(record_fs.flash_device, previous_offset, previous_record.data,
			    ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in reading previous record 0x%08x - err %d",
			(uint32_t)previous_offset, rc);
		return rc;
	}

	rc = flash_read(record_fs.flash_device, offset_addr, check_record.data,
			ETC_DEVICE_RECORD_SIZE);
	if (rc != 0) {
		LOG_ERR("Error in reading current record 0x%08x - err %d", (uint32_t)offset_addr,
			rc);
		return rc;
	}

	if (!(offset_addr == record_fs.offset && previous_record.timestamp == -1)) {
		rc = etc_device_record_validate_record(previous_record, check_record);
		if (rc) {
			*addr = offset_addr;
			return 0;
		}
	}

	do {
		previous_record = check_record;
		previous_offset = offset_addr;
		offset_addr = etc_device_get_next_addr_byte_addr_offset(&previous_offset);
		rc = flash_read(record_fs.flash_device, offset_addr, check_record.data,
				ETC_DEVICE_RECORD_SIZE);
		if (rc != 0) {
			LOG_ERR("Error in reading flash err %d", rc);
			return rc;
		}

		if (etc_device_record_buffer_is_erased(check_record.data, ETC_DEVICE_RECORD_SIZE)) {
			*addr = offset_addr;
			return 1;
		} else {
			rc = etc_device_record_validate_record(previous_record, check_record);
			if (rc) {
				LOG_DBG("%d", __LINE__);
				*addr = offset_addr;
				return 0;
			} else {
				/* Continue to check */
			}
		}
	} while (1);
	return -EINVAL;
}

static void etc_device_record_update_index(off_t *old_addr, off_t *new_addr) 
{
	uint16_t old_index = etc_device_record_get_id_by_addr(old_addr);
	uint16_t new_index = etc_device_record_get_id_by_addr(new_addr);
	uint16_t total = etc_device_record_get_total_record();
	uint16_t off_index = (new_index >= old_index) ? 
			     (new_index - old_index) : 
			     (MAX_RECORD_NO_OFFSET_ID - old_index + new_index);
	uint16_t oldest_index = etc_device_record_get_oldest_id();
	uint16_t newest_index = (etc_device_record_get_latest_id() + off_index) % MAX_RECORD_NO_OFFSET_ID;

	total += off_index;

	if (total > ETC_RECORD_MAX_RECORD) {
		total = ETC_RECORD_MAX_RECORD;
	}
	 
	p_etc_device_record_table->total = total;
	if (total == ETC_RECORD_MAX_RECORD) {
		oldest_index = (oldest_index + off_index) % MAX_RECORD_NO_OFFSET_ID;
	}
	
	p_etc_device_record_table->oldest = etc_device_get_index_by_id(oldest_index);
	p_etc_device_record_table->newest = etc_device_get_index_by_id(newest_index);
}

/* 
 * @brief This API will scan a suitable addr for new record
 * @return 0 is found a good data
 * @return 1 is no any freespace data.
 * 
 */
static int etc_device_record_find_available_addr(off_t *addr) 
{
	union etc_device_record previous_record;
	union etc_device_record check_record;
	off_t offset_addr = *addr;
	off_t previous_offset = 0;
	off_t oldest_addr = etc_device_record_get_addr_offset_by_index(&pRecord->record_stat.oldest);
	if (offset_addr == oldest_addr) {
		/* No free space */
		*addr = offset_addr;
		LOG_WRN("Reach oldest address 0x%08x", (unsigned int)offset_addr);
		return 1;
	}
	int rc = 0;
	do {
		previous_offset = offset_addr;
		offset_addr = etc_device_get_next_addr_byte_addr_offset(&previous_offset);
		if (offset_addr == oldest_addr) {
			/* No free space */
			*addr = offset_addr;
			LOG_WRN("Reach oldest address 0x%08x", (unsigned int)offset_addr);
			return 1;
		}
		rc = flash_read(record_fs.flash_device, offset_addr, check_record.data,
				ETC_DEVICE_RECORD_SIZE);
		if (rc != 0) {
			LOG_ERR("Error in reading flash err %d", rc);
			return rc;
		}

		if (etc_device_record_buffer_is_erased(check_record.data, ETC_DEVICE_RECORD_SIZE)) {
			LOG_WRN("Found erased address 0x%08x", (unsigned int)offset_addr);
			*addr = offset_addr;
			return 0;
		}
	} while (1);
	return -EINVAL;
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
		off_t new_addr = addr;
		rc = etc_device_record_find_available_addr(&new_addr);
		if (rc == 0) {
			/* Found erased address */
			etc_device_record_update_index(&addr, &new_addr);
			addr = new_addr;
		} else {
			off_t erase_addr = addr;
			/* If start sector */
			if ((erase_addr % record_fs.sector_size) == 0) {
				erase_addr = addr;
			} else {
				erase_addr = (addr + record_fs.sector_size) & ~(record_fs.sector_size - 1);
			}
			LOG_WRN("Erasing address 0x%08x", (unsigned int)erase_addr);
			rc = flash_erase(record_fs.flash_device, erase_addr, record_fs.sector_size);
			__ASSERT_NO_MSG(rc == 0);
			if (erase_addr != addr) {
				etc_device_record_update_index(&addr, &erase_addr);
			}
			addr = erase_addr;
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
	etc_device_sync_crc();
}

int etc_device_record_reading(uint16_t record_id, void *data)
{
	uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
	union etc_device_record *record = (union etc_device_record *)data;
	struct etc_device_record_index index = etc_device_get_index_by_id(record_id);
	uint32_t record_addr = (uint32_t)etc_device_record_get_addr_offset_by_index(&index);
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
	etc_device_sync_crc();
}

static int etc_device_update_reclaim(uint16_t record_id, int start_time, int stop_time)
{
	int rc = 0;
	union etc_device_record record;
	rc = etc_device_record_reading(record_id, record.data);
	if (rc == record_id) {
		rc = etc_device_unpack_sensor_data(&record);
		if (rc < 0) {
			LOG_WRN("Failed to unpack sensor data %d", rc);
			return 0;
		}
		etc_device_map_embeddable_sensor_data(&record);
		LOG_DBG("Record %d Time %d", record_id, record.timestamp);
		if ((start_time <= record.timestamp) && (record.timestamp <= stop_time)) {
			if (etc_reclaim_info.start_index == -1) {
				etc_reclaim_info.start_index = record_id;
				etc_reclaim_info.stop_index = record_id;
			} else {
				etc_reclaim_info.stop_index = record_id;
			}
		} else if ((record.timestamp > stop_time) && (etc_reclaim_info.stop_index != -1)) {
			return 1;
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

static int etc_device_record_get_num_reclaim_records(void)
{
	int num_records = 0;
	if (etc_reclaim_info.start_index <= etc_reclaim_info.stop_index) {
		num_records = etc_reclaim_info.stop_index - etc_reclaim_info.start_index + 1;
	} else {
		num_records = MAX_RECORD_NO_OFFSET_ID - etc_reclaim_info.start_index;
		num_records += etc_reclaim_info.stop_index + 1;
	}
	return num_records;
}

int etc_device_record_reclaim(int start_time, int stop_time, bool dry_run)
{
	if (!atomic_cas(&etc_reclaim_status, false, true)) {
		LOG_ERR("Reclaim already in progress");
		return -EINPROGRESS;
	}

	int rc = 0;
	if (start_time > stop_time) {
		rc = -EINVAL;
		goto done;
	}

	/* Reset the reclaim */
	etc_device_record_reset_reclaim();

	uint16_t oldest_id = etc_device_record_get_oldest_id();
	uint16_t newest_id = etc_device_record_get_latest_id();

	LOG_DBG("Request to reclaim %d %d %d %d", start_time, stop_time, oldest_id, newest_id);

	if (oldest_id < newest_id) {
		for (int i = oldest_id; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc < 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				goto done;
			} else if (rc == 1) {
				LOG_DBG("Found the range data for current request");
				goto update;
			}
		}
	} else {
		for (int i = oldest_id; i < MAX_RECORD_NO_OFFSET_ID; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc < 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				goto done;
			} else if (rc == 1) {
				LOG_DBG("Found the range data for current request");
				goto update;
			}
		}
		for (int i = MIN_RECORD_NO_OFFSET_ID; i < newest_id; i++) {
			rc = etc_device_update_reclaim(i, start_time, stop_time);
			if (rc < 0) {
				LOG_ERR("Failed to find and update ACK based on reclaim "
					"information");
				goto done;
			} else if (rc == 1) {
				LOG_DBG("Found the range data for current request");
				goto update;
			}
		}
	}
	
update:
	if ((rc >= 0) && (etc_reclaim_info.start_index != -1) &&
	    (etc_reclaim_info.stop_index != -1)) {
		LOG_INF("Reclaim info successful %d %d",
			etc_reclaim_info.start_index, etc_reclaim_info.stop_index);
		if (!dry_run) {
			etc_reclaim_info.flag_in_process = 1U;
			etc_reclaim_info.current_index = etc_reclaim_info.start_index;
			rc = etc_device_write_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
						sizeof(etc_reclaim_info));
			if (rc != 0) {
				LOG_ERR("Failed to write reclaim info");
				goto done;
			} else {
				LOG_INF("Updated the reclaim info successful");
			}
		} else {
			int num_records = etc_device_record_get_num_reclaim_records();
			
			/* Reset start/stop index */
			etc_device_record_reset_reclaim();
			LOG_INF("Total number of record <%d, %d>: %d", start_time, stop_time, num_records);
			atomic_set(&etc_reclaim_status, false);
			return num_records;
		}
	}

done:
	atomic_set(&etc_reclaim_status, false);
	return rc;
}

int etc_device_record_reclaim_cancel(void)
{
	/* Clear in-progress reclaim state so etc_device_reclaim_data() stops
	 * re-emitting historical records, then persist the cleared state.
	 * Called from the data_module thread, which also drains records, so no
	 * additional locking is needed against the drain. */
	etc_device_record_reset_reclaim();
	atomic_set(&etc_reclaim_status, false);

	int rc = etc_device_write_setting(ETC_RECORD_RECLAIM, &etc_reclaim_info,
					  sizeof(etc_reclaim_info));
	if (rc != 0) {
		LOG_ERR("Failed to write reclaim info on cancel");
		return rc;
	}

	LOG_INF("Reclaim cancelled");
	return 0;
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
				if (rc >= 0) { 
					/* Return the record id */
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
				if (rc >= 0) { 
					/* Return the record id */
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
				if (rc >= 0) {
					/* Return the record id */
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

	return -ENOENT;
}

int etc_device_record_find_nack(etc_device_record_reading_callback reading_callback, void *data,
				bool *active_reclaim)
{
	int rc = 0;
	int newest_id = etc_device_record_get_latest_id();
	int oldest_id = etc_device_record_get_oldest_id();

	/* FW-954: current (non-ack'd) readings always take precedence over an
	 * active reclaim. The reclaim is paused while new readings are pending
	 * and resumes once they are drained. The reclaim status reported back
	 * tracks whether a reclaim is still in process (not the type of the
	 * record served), so the cloud stays IN_PROGRESS across the preemption
	 * and only sees SUCCESS once the whole reclaim range completes.
	 */
	if (active_reclaim != NULL) {
		*active_reclaim = (etc_reclaim_info.flag_in_process == 1);
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
		return rc;
	}

	/* No new readings pending: serve the next reclaim record, if any. */
	if (etc_reclaim_info.flag_in_process == 1) {
		rc = etc_device_reclaim_data(reading_callback, data);
		if (rc >= 0) {
			/* Return the record ID with offset to follow old structure */
			return ETC_RECORD_ID_HEADER(rc);
		} else {
			/* Otherwise, return error */
			return rc;
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
	return FIXED_PARTITION_OFFSET(ETC_DEVICE_RECORD_NODE_LABEL);
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

int etc_device_record_num_reclaim_records(void)
{
	if (etc_reclaim_info.flag_in_process) {
		return etc_device_record_get_num_reclaim_records();
	}
	return 0;
}

struct etc_device_record_backup_data* etc_device_record_backup_get_object(void){
	return &etc_device_record_backup;
}

void etc_device_record_backup_sync(void) {
	int rc = retained_mem_write(retained_ram_dev, ETC_RECORD_BACKUP_OFFSET_IN_RAM,
				(uint8_t *)&etc_device_record_backup,
				sizeof(etc_device_record_backup));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
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
			if (sensor_temperature_is_valid(record.sensor[i])) {
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
		int rc = etc_device_record_reclaim(start_time, stop_time, false);
		if (rc != 0) {
			shell_error(shell, "Failed to reclaim record");
		} else {
			shell_info(shell, "Reclaimed recordsuccess");
			shell_info(shell, "Start ID %d - Stop %d", etc_reclaim_info.start_index,
				   etc_reclaim_info.stop_index);
		}
	} else {
		shell_error(shell, "Invalid input parameter for reclaim record");
	}

	return 0;
}

static int cmd_save_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_save();
	shell_info(shell, "Save done");
	return 0;
}

static int cmd_reload_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_load();
	shell_info(shell, "Reload done");
	return 0;
}

static int cmd_reset_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_reset_stat();
	etc_device_record.record_sync_flag = 0;
	shell_info(shell, "Reset done");
	return 0;
}

static int cmd_erase_record(const struct shell *shell, size_t argc, char **argv)
{
	int rc = flash_erase(record_fs.flash_device, record_fs.offset,
			     record_fs.sector_count * record_fs.sector_size);
	shell_info(shell, "Erase done %d", rc);
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

static int cmd_generate_at_record(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 3) {
		p_etc_device_record_table->newest.sector_idx = atoi(argv[1]);
		p_etc_device_record_table->newest.element_idx = atoi(argv[2]);
		extern void ui_module_test_data_request(int num_of_sample);
		ui_module_test_data_request(1);
	} else {
		shell_error(shell, "Invalid input parameter for generating record");
	}

	return 0;
}

static int cmd_dump_record(const struct shell *shell, size_t argc, char **argv)
{
	int offset = 0;
	int offset_max = sizeof(struct etc_device_record_data);
	do {
		int print_len = MIN(offset_max - offset, 128);
		shell_hexdump(shell, (uint8_t *)pRecord + offset, print_len);
		offset += print_len;
		k_sleep(K_MSEC(100));
	} while (offset < offset_max);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_record,
	SHELL_CMD(report, NULL, "Report number record (total/ack/nack)", cmd_num_report_record),
	SHELL_CMD(nack_dump, NULL, "Dump nack list", cmd_get_nack_id_list),
	SHELL_CMD(clean, NULL, "Clean the records", cmd_clean_records),
	SHELL_CMD(parser, NULL, "Parser the hex record", cmd_parser_hex_record),
	SHELL_CMD(reclaim, NULL, "Reclaim ", cmd_reclaim_record),
	SHELL_CMD(save, NULL, "Save record stat ", cmd_save_record),
	SHELL_CMD(reload, NULL, "Reload record stat ", etc_device_record_load),
	SHELL_CMD(reset, NULL, "Reset record stat ", cmd_reset_record),
	SHELL_CMD(erase, NULL, "Erase record ", cmd_erase_record),
	SHELL_CMD(generate, NULL, "Generate a certain number of samples to fill up the flash ",
		  cmd_generate_record),
	SHELL_CMD(generate_at, NULL, "Generate a record at <sector,index> ",
		  cmd_generate_at_record),
	SHELL_CMD(dump, NULL, "Hexdump current record data in retained RAM", cmd_dump_record),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(record, &sub_record, "ETC Record Management", NULL);
#endif /* CONFIG_SHELL */