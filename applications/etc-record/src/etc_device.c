#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
LOG_MODULE_REGISTER(etc_device, LOG_LEVEL_DBG);

#include "etc_device.h"
#include "etc_device_record.h"
#define OLD_RECORD 1

struct etc_device_record_table *etc_device_record_table = &etc_device_record.record_stat;

uint8_t record_header[MAX_RECORD_ID];
static uint16_t ram_nack_record_id;

void etc_device_init(void) {
	memset(record_header, 0, MAX_RECORD_ID);
	ram_nack_record_id = 0;
}

static struct etc_device_record_index etc_device_get_next_index(void)
{
	if (etc_device_record_table->newest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
		etc_device_record_table->newest.element_idx += 1;
	} else {
		etc_device_record_table->newest.element_idx = 0;
		if (etc_device_record_table->newest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
			etc_device_record_table->newest.sector_idx += 1;
		} else {
			etc_device_record_table->newest.sector_idx = 0;
		}
	}

	if (etc_device_record_table->total < ETC_RECORD_MAX_RECORD) {
		etc_device_record_table->oldest.element_idx = 0;
		etc_device_record_table->oldest.sector_idx = 0;
		etc_device_record_table->total += 1;
	} else {
		if (etc_device_record_table->oldest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
			etc_device_record_table->oldest.element_idx += 1;
		} else {
			etc_device_record_table->oldest.element_idx = 0;
			if (etc_device_record_table->oldest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
				etc_device_record_table->oldest.sector_idx += 1;
			} else {
				etc_device_record_table->oldest.sector_idx = 0;
			}
		}
	}

	return etc_device_record_table->newest;
}

struct etc_device_record_index etc_device_write(void) {
	struct etc_device_record_index record_index;
	if (etc_device_record_table->total == 0) {
		record_index = etc_device_record_table->newest;
		etc_device_record_table->total += 1;
	} else {
		record_index = etc_device_get_next_index();
	}

	uint16_t record_id = record_index.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			     record_index.element_idx;
	#if OLD_RECORD
	record_header[ETC_RECORD_ID_HEADER(record_id)] = 0U;
	#else
	etc_device_record_set_nack(record_id);
	#endif
	etc_device_record_save_stat();
	return record_index;
}

int etc_device_find_nack(void)
{
	int rc = 0;
	int newest_id = etc_device_record_table->newest.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			etc_device_record_table->newest.element_idx + ETC_RECORD_HEADER;
	int oldest_id = etc_device_record_table->oldest.sector_idx * ETC_RECORD_MAX_PER_SECTOR +
			etc_device_record_table->oldest.element_idx + ETC_RECORD_HEADER;
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER - 1;
	uint16_t min_id = ETC_RECORD_HEADER;
	uint16_t last_id = ram_nack_record_id;
	uint16_t check_id = 0;
	bool find_next = false;

	if (last_id == newest_id) {
		return 0;
	}

	check_id = last_id == 0 ? oldest_id : last_id + 1;
	if (check_id > max_id) {
		check_id = min_id;
	}

next_id:
	// LOG_DBG("Last ID %u - Check ID %d - New ID %d", last_id, check_id, newest_id);

	if (record_header[check_id] == 0) {
		rc = check_id;
	} else {
		find_next = true;
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

void etc_device_read(void) {
	int rc = etc_device_find_nack();
	if (rc == 0) {
		LOG_WRN("No more NACK");
		return;
	}
	// LOG_DBG("Record %d", rc);
	ram_nack_record_id = rc;
	record_header[rc] = 1U;
}

uint16_t etc_device_get_num_ack(void) {
	uint16_t oldest_id = ETC_RECORD_MAX_PER_SECTOR * etc_device_record_table->oldest.sector_idx +
			  etc_device_record_table->oldest.element_idx + ETC_RECORD_HEADER;
	uint16_t newest_id = ETC_RECORD_MAX_PER_SECTOR * etc_device_record_table->newest.sector_idx +
			  etc_device_record_table->newest.element_idx + ETC_RECORD_HEADER;
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER - 1;
	uint16_t min_id = ETC_RECORD_HEADER;
	uint16_t total_record = etc_device_record_table->total;
	uint16_t count = 0;
	uint16_t current_record = newest_id;
	uint16_t ack = 0;
	while (count < total_record) {
		if (record_header[current_record] == 1) {
			// LOG_DBG("Record %d", current_record);
			ack += 1;
		}

		if (current_record == oldest_id) {
			break;
		}

		current_record--;
		
		if (current_record < min_id) {
			current_record = max_id;
		}

		count++;
	}
	return ack;
}

void etc_device_report(const struct shell *shell) {
	uint16_t ack = etc_device_get_num_ack();
	uint16_t oldest_id = ETC_RECORD_MAX_PER_SECTOR  * etc_device_record_table->oldest.sector_idx +
			  etc_device_record_table->oldest.element_idx + ETC_RECORD_HEADER;
	uint16_t newest_id = ETC_RECORD_MAX_PER_SECTOR  * etc_device_record_table->newest.sector_idx +
			  etc_device_record_table->newest.element_idx + ETC_RECORD_HEADER;
	shell_print(shell, "%d %d %d %d %d", MAX_RECORD_ID, MIN_RECORD_ID, ETC_RECORD_MAX_SECTOR, ETC_RECORD_MAX_PER_SECTOR , ETC_RECORD_HEADER);
	shell_print(shell, "%d %d - %d %d", etc_device_record_table->oldest.sector_idx, etc_device_record_table->oldest.element_idx,
		etc_device_record_table->newest.sector_idx, etc_device_record_table->newest.element_idx);
	shell_print(shell, "Old ID %d (%d) - New %d (%d)", oldest_id, ETC_RECORD_ID(oldest_id), newest_id, ETC_RECORD_ID(newest_id));
	shell_print(shell, "Total %d - ACK %d - NACK %d", etc_device_record_table->total, ack, etc_device_record_table->total - ack);
}

void etc_device_export_old_structure(const struct shell *shell) {
	uint16_t oldest_id = ETC_RECORD_MAX_PER_SECTOR * etc_device_record_table->oldest.sector_idx +
			  etc_device_record_table->oldest.element_idx + ETC_RECORD_HEADER;
	uint16_t newest_id = ETC_RECORD_MAX_PER_SECTOR * etc_device_record_table->newest.sector_idx +
			  etc_device_record_table->newest.element_idx + ETC_RECORD_HEADER;
	uint16_t max_id = ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER - 1;
	uint16_t min_id = ETC_RECORD_HEADER;
	uint16_t total_record = etc_device_record_table->total;
	uint16_t count = 0;
	uint16_t current_record = newest_id;
	int rc = 0;
	uint16_t ack_cnt = 0;
	shell_print(shell, "Export from old structure");
	while (count < total_record) {
		if (record_header[current_record] == 1) {
			etc_device_record_set_ack(ETC_RECORD_ID(current_record));
			ack_cnt += 1;
		} else {
			etc_device_record_set_nack(ETC_RECORD_ID(current_record));
		}

		if (current_record == oldest_id) {
			break;
		}

		current_record--;
		
		if (current_record < min_id) {
			current_record = max_id;
		}

		count++;
	}

	shell_print(shell, "Export success %d %d %d %d %d", count, ETC_RECORD_ID(current_record), oldest_id, newest_id, ack_cnt);
}

uint8_t* etc_device_dump(void) {
	return &record_header[ETC_RECORD_HEADER];
}