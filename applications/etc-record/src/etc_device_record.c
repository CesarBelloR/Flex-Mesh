#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/shell/shell.h>
LOG_MODULE_REGISTER(etc_device_record, LOG_LEVEL_DBG);

#include "etc_device_record.h"

const static struct device *retained_ram_dev =	DEVICE_DT_GET(DT_ALIAS(record_header_ram));

struct etc_device_record_data etc_device_record;
struct etc_device_record_data* pRecord = &etc_device_record;

#define RECORD_ARRAY_OFFSET(x) (offsetof(struct etc_device_record_data, record_bits) + x)
#define RECORD_FLAG_OFFSET offsetof(struct etc_device_record_data, record_sync_flag)
#define RECORD_STAT_OFFSET offsetof(struct etc_device_record_data, record_stat)

static int etc_device_record_get_ack_status(int record_id) {
	uint16_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t byte_status = pRecord->record_bits[byte_pos];
	return (byte_status >> bit_pos) & 0x01;
}

static void etc_device_record_set_ack_status(int record_id, int status) {
	uint16_t byte_pos = record_id / 8;
	uint8_t bit_pos = record_id % 8;
	uint8_t* byte_status = &pRecord->record_bits[byte_pos];
	if (status == 0) {
		*byte_status &= ~(1 << bit_pos); 
	} else {
		*byte_status |= (1 << bit_pos);
	}
}

void etc_device_record_init(void) {
	int rc = retained_mem_read(retained_ram_dev, 0, (uint8_t*)pRecord, 
		sizeof(struct etc_device_record_data));

	if ((rc) || (pRecord->record_sync_flag != ETC_DEVICE_RECORD_FLAG)) {
		/* Clean up the memory RAM in no-init region */
		retained_mem_clear(retained_ram_dev);
		/* Clean up the record in app RAM */
		memset(pRecord, 0, sizeof(struct etc_device_record_data));
		pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
		pRecord->record_stat.newest.sector_idx = 0;
		pRecord->record_stat.oldest.sector_idx = 0;
		pRecord->record_stat.newest.element_idx = 0;
		pRecord->record_stat.oldest.element_idx = 0;
		pRecord->record_stat.total = 0;
		/* Save it */
		rc = retained_mem_write(retained_ram_dev, RECORD_FLAG_OFFSET, 
			(uint8_t*)&pRecord->record_sync_flag, sizeof(pRecord->record_sync_flag));
		if (rc) {
			printk("Failed to write data - err %d\n", rc);
		}
	} else {
		printk("Record is ready\n");
	}
}

int etc_device_record_get_ack(int record_id) {
	return etc_device_record_get_ack_status(record_id);
}

void etc_device_record_set_ack(int record_id) {
	etc_device_record_set_ack_status(record_id, 1);
	uint16_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position), 
		(uint8_t*)&pRecord->record_bits[byte_position], sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

void etc_device_record_set_nack(int record_id) {
	etc_device_record_set_ack_status(record_id, 0);
	uint16_t byte_position = record_id / 8;
	int rc = retained_mem_write(retained_ram_dev, RECORD_ARRAY_OFFSET(byte_position), 
		(uint8_t*)&pRecord->record_bits[byte_position], sizeof(uint8_t));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

uint8_t* etc_device_record_dump(void) {
	return pRecord->record_bits;
}

void etc_device_record_clean_up(void) {
	retained_mem_clear(retained_ram_dev);
	memset(pRecord, 0, sizeof(struct etc_device_record_data));
	pRecord->record_sync_flag = ETC_DEVICE_RECORD_FLAG;
	int rc = retained_mem_write(retained_ram_dev, RECORD_FLAG_OFFSET, 
		(uint8_t*)&pRecord->record_sync_flag, sizeof(pRecord->record_sync_flag));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}

void etc_device_record_save_stat(void) {
	int rc = retained_mem_write(retained_ram_dev, RECORD_STAT_OFFSET, 
		(uint8_t*)&pRecord->record_stat, sizeof(pRecord->record_stat));
	if (rc) {
		LOG_ERR("Failed to write data - err %d", rc);
	}
}