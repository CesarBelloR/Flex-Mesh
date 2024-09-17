/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <zephyr/ztest.h>
#include <stdbool.h>
#include <string.h>

#include "etc_util.h"
#include "etc_device.h"
#include "etc_device_record.h"
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(etc_device_record_test, CONFIG_ETC_APP_LOG_LEVEL);

const static struct device *retained_ram_dev = DEVICE_DT_GET(DT_ALIAS(record_header_ram));
struct etc_device_record_table last_current_record;

#if CONFIG_BOARD_NATIVE_SIM
#define RECORD_ARRAY_SIZE (ETC_RECORD_MAX_RECORD)
#elif CONFIG_BOARD_ETC
#define RECORD_ARRAY_SIZE 6000
#else
#error "No support other SoC"
#endif

const static union etc_device_record record_sample = {
	.battery = 4.1,
	.sensor[0] = 12.1,
	.sensor[1] = 87.1,
	.sensor[2] = -6.2,
	.sensor[3] = 32.6,
	.sensor[4] = 0.2,
	.sensor[5] = 99.9
};

union etc_device_record records[RECORD_ARRAY_SIZE];

static void *test_setup(void)
{
	int index; 
	etc_device_nvs_init();
	/* Make sure the record is fresh */
	etc_device_record_clean_up();
	etc_device_record_erase_record_flash();
	/* Populate records */
	for (int time = 1; time <= ARRAY_SIZE(records); time++) {
		index = time - 1;
		memcpy(&records[index], &record_sample, sizeof(record_sample));
		records[index].timestamp = time;
	}

	return NULL;
}

static void test_teardown(void *)
{
	/* Allow ack/nack status to be saved to flash */
	k_sleep(K_SECONDS(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC + 1));
}

static void on_write_num_records(uint16_t num_records, bool check_ack, bool check_index, uint16_t index) 
{
	int ret = 0;
	int last_ack = 0;
	int last_nack = 0;
	int total_record = etc_device_record_get_total_record();
	uint16_t last_newest_id = etc_device_record_get_latest_id();
	uint16_t last_oldest_id = etc_device_record_get_oldest_id();
	struct etc_device_record_table prev_status = {0};
	if (check_ack) {
		last_ack = etc_device_record_get_num_ack();
		last_nack = etc_device_nack_count();
		struct etc_device_record_table* status = etc_device_record_get_status();
		memcpy(&prev_status, status, sizeof(*status));
		zassert_equal(last_ack, total_record - last_nack, "ACK NOT OK %d %d", last_nack, total_record - last_nack);
	}

	for (int i = 0; i < num_records; i++) {
		ret = etc_device_write_record(&records[i]);
		if (check_index && index == i) {
			/* This record should be next of last current record */
			struct etc_device_record_table* status = etc_device_record_get_status();
			struct etc_device_record_table* pre_status = &last_current_record;
			uint16_t next_of_pre_record_id = etc_device_get_next_id_by_index(pre_status->newest);
			uint16_t current_id = etc_device_record_get_id_by_index(status->newest);
			zassert_equal(next_of_pre_record_id, current_id, "ID -> NOT OK %d %d", next_of_pre_record_id, current_id);
			if (check_ack) {
				/* Ignore current record. Only sync old records */
				total_record = etc_device_record_get_total_record();
				last_nack = etc_device_nack_count() - 1;
				last_ack = total_record - last_nack;
			}
		}
		zassert_ok(ret);
	}
	
	if (check_ack) {
		struct etc_device_record_table* status = etc_device_record_get_status();
		LOG_DBG("(%d,%d) - (%d,%d) %d %d", prev_status.newest.sector_idx, prev_status.newest.element_idx, 
			prev_status.oldest.sector_idx, prev_status.oldest.element_idx,
			last_newest_id, last_oldest_id);
		uint16_t newest_id = etc_device_record_get_latest_id();
		uint16_t oldest_id = etc_device_record_get_oldest_id();
		LOG_DBG("(%d,%d) - (%d,%d) %d %d", status->newest.sector_idx, status->newest.element_idx, 
			status->oldest.sector_idx, status->oldest.element_idx,
			newest_id, oldest_id);
		LOG_DBG("%d %d %d %d", total_record, num_records, last_ack, last_nack);
		uint16_t nack_count = 0;
		if (newest_id < last_newest_id) {
			nack_count = (newest_id + 1) + (MAX_RECORD_NO_OFFSET_ID + 1) - (last_newest_id + 1);
		} else {
			if (last_newest_id == 0) {
				nack_count = newest_id + 1 - (last_newest_id);
			} else {
				nack_count = newest_id - last_newest_id;
			}
		}
		nack_count = (nack_count >= ETC_RECORD_MAX_RECORD) ? ETC_RECORD_MAX_RECORD : nack_count;

		total_record = etc_device_record_get_total_record();
		uint16_t ack_count = total_record - nack_count;
		last_ack = etc_device_record_get_num_ack();
		last_nack = etc_device_nack_count();
		LOG_DBG("%d %d %d %d %d", total_record, num_records, last_ack, last_nack, nack_count);
		zassert_equal(ack_count, last_ack, "ack count %d %d", ack_count, last_ack);
		zassert_equal(nack_count, last_nack, "nack_count: %d %d", ret, num_records);
	}
}

static void on_read_num_records(uint16_t num_records)
{
	int ret;
	int ack_count = etc_device_record_get_num_ack();
	int nack_count = etc_device_nack_count();
	bool reclaim = false;
	int record_id;
	union etc_device_record read_record;

	for (int i = num_records - 1; i >= 0; i--) {
		record_id = etc_device_read_record(&read_record, &reclaim);
		zassert(record_id > 0, "record id");
		ret = memcmp(&read_record, &records[i], sizeof(read_record));
		zassert_ok(ret, "record %d %d", record_id, i);
		etc_device_set_ack_record(record_id);
		nack_count--;
		zassert_equal(nack_count, etc_device_nack_count());
		ack_count++;
		zassert_equal(ack_count, etc_device_record_get_num_ack(), "new_ack: %d", etc_device_record_get_num_ack());
	}
}

static void on_save_record_status(void) 
{
	struct etc_device_record_table* status = etc_device_record_get_status();
	/* Save last status before clear */
	memcpy(&last_current_record, status, sizeof(*status));
}

static void on_erase_ram(void) 
{
	retained_mem_clear(retained_ram_dev);
}

static void on_save_flash(void) 
{
	etc_device_record_save();
}

static void on_reinit_setting(void) 
{
	etc_device_nvs_init();
}

ZTEST(etc_device_record_test, test_01_write_few_records)
{
	on_write_num_records(10, true, false, 0);
}

ZTEST(etc_device_record_test, test_02_read_few_records)
{
	on_read_num_records(10);
}

ZTEST(etc_device_record_test, test_03_write_full_records)
{
	on_write_num_records(ARRAY_SIZE(records), true, false, 0);
}

ZTEST(etc_device_record_test, test_04_read_full_records)
{
	on_read_num_records(ARRAY_SIZE(records));
}

ZTEST(etc_device_record_test, test_05_write_dummy_and_reset_ram)
{
	/* Force to re-init to apply RAM */
	on_save_flash();
	/* Write 10 records */
	on_write_num_records(10, false, false, 0);
	/* Backup record */
	on_save_record_status();
	/* Erase RAM */
	on_erase_ram();
	/* Reload RAM */
	on_reinit_setting();
}

ZTEST(etc_device_record_test, test_06_write_records_after_losing_ram)
{
	on_write_num_records(ARRAY_SIZE(records), true, true, 0);
}

ZTEST(etc_device_record_test, test_07_read_records_after_losing_ram)
{
	on_read_num_records(ARRAY_SIZE(records));
}

ZTEST(etc_device_record_test, test_08_repeat_test)
{
	for (int t = 0; t < 5; t++) {
		on_reinit_setting();
		on_write_num_records(ARRAY_SIZE(records), true, false, 0);
		on_read_num_records(ARRAY_SIZE(records));
		/* Force to re-init to apply RAM */
		on_save_flash();
		/* Write 10 records */
		on_write_num_records(5 * (t + 1), false, false, 0);
		/* Backup record */
		on_save_record_status();
		/* Erase RAM */
		on_erase_ram();
		/* Reinit RAM*/
		on_reinit_setting();
		on_write_num_records(ARRAY_SIZE(records), true, true, 0);
		on_read_num_records(ARRAY_SIZE(records));
	}
}

ZTEST(etc_device_record_test, test_09_repeat_rand)
{
	on_save_flash();
	on_reinit_setting();
	for (int t = 0; t < 25; t++) {
		uint16_t requests = rand() % RECORD_ARRAY_SIZE;
		uint16_t lost_request = rand() % 64;
		on_write_num_records(requests, true, false, 0);
		on_read_num_records(requests);
	}
}

ZTEST_SUITE(etc_device_record_test, NULL, test_setup, NULL, NULL, test_teardown);