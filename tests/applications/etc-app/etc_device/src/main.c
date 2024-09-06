/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <stdbool.h>
#include <string.h>

#include "etc_util.h"
#include "etc_device.h"
#include "etc_device_record.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_device_record_test, CONFIG_ETC_APP_LOG_LEVEL);

#define RECORD_ARRAY_SIZE (ETC_RECORD_MAX_RECORD)

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

ZTEST(etc_device_record_test, test_01_write_records)
{
	int ret;
	
	for (int i = 0; i < ARRAY_SIZE(records); i++) {
		ret = etc_device_write_record(&records[i]);
		zassert_ok(ret);
	}

	int ack_count = etc_device_record_get_num_ack();
	zassert_equal(ack_count, 0);
	int nack_count = etc_device_nack_count();
	zassert_equal(nack_count, ARRAY_SIZE(records), "nack_count: %d %d", ret, ARRAY_SIZE(records));
}


ZTEST(etc_device_record_test, test_02_read_records)
{
	int ret;
	int ack_count = etc_device_record_get_num_ack();
	int nack_count = etc_device_nack_count();
	bool reclaim = false;
	int record_id;
	union etc_device_record read_record;


	for (int i = ARRAY_SIZE(records) - 1; i >= 0; i--) {
		record_id = etc_device_read_record(&read_record, &reclaim);
		zassert(record_id > 0, "record id");
		ret = memcmp(&read_record, &records[i], sizeof(read_record));
		LOG_HEXDUMP_INF(&read_record, sizeof(read_record), "read_record");
		LOG_HEXDUMP_INF(&records[i], sizeof(read_record), "expected_record");
		zassert_ok(ret, "record %d %d", record_id, i);
		etc_device_set_ack_record(record_id);
		nack_count--;
		zassert_equal(nack_count, etc_device_nack_count());
		ack_count++;
		zassert_equal(ack_count, etc_device_record_get_num_ack(), "new_ack: %d", etc_device_record_get_num_ack());
	}

}

ZTEST_SUITE(etc_device_record_test, NULL, test_setup, NULL, NULL, test_teardown);