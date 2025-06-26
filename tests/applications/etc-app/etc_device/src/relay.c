/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "etc_device.h"
#include "etc_util.h"
#include "common.h"
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(etc_device_relay_test, CONFIG_ETC_APP_LOG_LEVEL);

#define RELAY_LEGACY_DATA_STR                                                                      \
	"1.2.5,-99,10000199,4.12,*,3.99,*,0.0.0-twister,1,1738795004,41.2,41.6,35.3,*,23.5,*,1,0,"

static struct etc_device_relay_record record = {
	.battery = 4.12,
	.is_reclaim = false,
	.logger_rssi = -99,
	.packet_number = 1,
	.timestamp = 1738795004,
	.relay_id = "OPEN",
	.logger_ver = "1.2.5",
	.logger_id = "10000199",
	.sensor = {41.2, 41.6, 35.32, SENSOR_TEMP_NO_CONNECTED, SENSOR_TEMP_NO_CONNECTED,
		   SENSOR_TEMP_NO_CONNECTED, SENSOR_TEMP_NO_CONNECTED, SENSOR_TEMP_NO_CONNECTED,
		   23.5, SENSOR_HUMID_NO_CONNECTED},
	.data = {ETC_DEVICE_INVALID_VALUE_ELEMENT},
};

static struct etc_device_relay_packet packet;

#define BUF_SIZE 512
char buf[BUF_SIZE];

static void *test_setup(void)
{
	for (int i = 0; i < ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS; i++) {
		memcpy(&packet.records[i], &record, sizeof(record));
	}
	packet.num_records = ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS;

	return NULL;
}


ZTEST(etc_device_relay_test, test_single_legacy_data)
{
	int len;
	int ret;

	ret = etc_common_prepare_relay_legacy_data(&record, buf, &len, sizeof(buf));
	zassert_ok(ret);
	LOG_INF("%s", buf);
	zassert_equal(len, strlen(buf));
	zassert_mem_equal(buf, RELAY_LEGACY_DATA_STR, len);
}

ZTEST(etc_device_relay_test, test_legacy_data_packet)
{
	int len;
	int ret;

	ret = etc_common_prepare_relay_legacy_packet(&packet, buf, &len, sizeof(buf));
	zassert_ok(ret);
	LOG_INF("%s", buf);
	zassert_equal(len, strlen(buf));
	zassert_mem_equal(buf,
			  RELAY_LEGACY_DATA_STR "|" RELAY_LEGACY_DATA_STR "|" RELAY_LEGACY_DATA_STR
						"|" RELAY_LEGACY_DATA_STR "|" RELAY_LEGACY_DATA_STR,
			  len);
}

ZTEST(etc_device_relay_test, test_relay_read_init)
{
	struct etc_device_relay_packet p;
	int ret;

	ret = etc_device_read_relay_data_packet(&p);
	zassert_not_ok(ret);
}

ZTEST(etc_device_relay_test, test_relay_write_read_single)
{
	struct etc_device_relay_packet p;
	int ret;
	
	memset(&p, 0, sizeof(p));

	ret = etc_device_write_relay_data(&record);
	zassert_ok(ret);
	ret = etc_device_read_relay_data_packet(&p);
	zassert_ok(ret);
	zassert_equal(p.num_records, 1);
	zassert_mem_equal(&p.records[0], &record, sizeof(record));
	ret = etc_device_sync_relay_data();
	zassert_ok(ret);
}

ZTEST(etc_device_relay_test, test_relay_double_sync)
{
	struct etc_device_relay_packet p;
	int ret;
	
	memset(&p, 0, sizeof(p));

	ret = etc_device_write_relay_data(&record);
	zassert_ok(ret);
	ret = etc_device_read_relay_data_packet(&p);
	zassert_ok(ret);
	zassert_equal(p.num_records, 1);
	zassert_mem_equal(&p.records[0], &record, sizeof(record));
	ret = etc_device_sync_relay_data();
	zassert_ok(ret);
	ret = etc_device_sync_relay_data();
	zassert_not_ok(ret);
}

ZTEST(etc_device_relay_test, test_relay_write_read_multiple)
{
	struct etc_device_relay_record r;
	struct etc_device_relay_packet p;
	int ret;

	memcpy(&r, &record, sizeof(record));

	for (int i = 0; i < ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS; i++) {
		r.data[0] = i + 1;
		ret = etc_device_write_relay_data(&r);
		zassert_ok(ret);
	}
	ret = etc_device_read_relay_data_packet(&p);
	zassert_ok(ret);
	zassert_equal(p.num_records, ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS);
	
	for (int i = 0; i < ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS; i++) {
		r.data[0] = i + 1;
		zassert_mem_equal(&p.records[i], &r, sizeof(r));
	}
	ret = etc_device_sync_relay_data();
	zassert_ok(ret);
}

ZTEST(etc_device_relay_test, test_relay_write_full_read_empty)
{
	struct etc_device_relay_record r;
	struct etc_device_relay_packet p;
	int ret;
	int max_reads =
		ETC_RELAY_RECORD_MAX_ELEMENT / ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS +
		(ETC_RELAY_RECORD_MAX_ELEMENT % ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS == 0 ? 0 : 1);
	int num_records;

	memcpy(&r, &record, sizeof(record));

	for (int i = 0; i < ETC_RELAY_RECORD_MAX_ELEMENT; i++) {
		r.data[0] = i + 1;
		ret = etc_device_write_relay_data(&r);
		zassert_ok(ret);
	}
	ret = etc_device_write_relay_data(&r);
	zassert_not_ok(ret);

	for (int i = 0; i < max_reads; i++) {
		ret = etc_device_read_relay_data_packet(&p);
		zassert_ok(ret);
		ret = etc_device_sync_relay_data();
		zassert_ok(ret);
		num_records = i >= ETC_RELAY_RECORD_MAX_ELEMENT /
						      ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS
				      ? ETC_RELAY_RECORD_MAX_ELEMENT %
						ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS
				      : ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS;
		zassert_equal(p.num_records, num_records);
		for (int j = 0; j < num_records; j++) {
			r.data[0] = (ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS * i) + j + 1;
			zassert_mem_equal(&p.records[j], &r, sizeof(r),
					  "got: %d expected: %d, i: %d, j: %d",
					  p.records[j].data[0], r.data[0], i, j);
		}
	}
	ret = etc_device_read_relay_data_packet(&p);
	zassert_not_ok(ret);
}

ZTEST_SUITE(etc_device_relay_test, NULL, test_setup, NULL, NULL, NULL);