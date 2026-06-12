/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "etc_device.h"
#include "etc_relay_reclaim.h"
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

/* --- FW-991: reclaim buffer query / clear / iterate APIs ---------------- */

static void reclaim_reset(void)
{
	(void)etc_relay_reclaim_clear_all();
}

ZTEST(etc_device_relay_test, test_reclaim_count_empty)
{
	reclaim_reset();
	zassert_equal(etc_relay_reclaim_count(), 0);
}

ZTEST(etc_device_relay_test, test_reclaim_add_and_count)
{
	reclaim_reset();
	char id_a[] = "10000100";
	char id_b[] = "10000200";
	zassert_ok(etc_relay_reclaim_set(id_a, 1000, 2000));
	zassert_ok(etc_relay_reclaim_set(id_b, 3000, 4000));
	zassert_equal(etc_relay_reclaim_count(), 2);
}

ZTEST(etc_device_relay_test, test_reclaim_get_by_index_bounds)
{
	reclaim_reset();
	struct etc_device_reclaim_request out;
	zassert_equal(etc_relay_reclaim_get_by_index(-1, &out), -EINVAL);
	zassert_equal(etc_relay_reclaim_get_by_index(ETC_RECLAIM_RELAY_MAX_ELEMENT, &out), -EINVAL);
	zassert_equal(etc_relay_reclaim_get_by_index(0, &out), -ENOENT);
}

ZTEST(etc_device_relay_test, test_reclaim_get_by_index_active)
{
	reclaim_reset();
	char id[] = "10000101";
	zassert_ok(etc_relay_reclaim_set(id, 1111, 2222));

	struct etc_device_reclaim_request out;
	zassert_ok(etc_relay_reclaim_get_by_index(0, &out));
	zassert_equal(out.start_time, 1111);
	zassert_equal(out.stop_time, 2222);
	zassert_ok(strcmp(out.logger_id, id));
}

ZTEST(etc_device_relay_test, test_reclaim_clear_all_returns_prior_count)
{
	reclaim_reset();
	char id[] = "10000102";
	zassert_ok(etc_relay_reclaim_set(id, 1, 2));
	zassert_ok(etc_relay_reclaim_set(id, 3, 4));
	zassert_ok(etc_relay_reclaim_set(id, 5, 6));
	zassert_equal(etc_relay_reclaim_clear_all(), 3);
	zassert_equal(etc_relay_reclaim_count(), 0);
}

struct logger_match_ctx {
	const char *target;
	int matches;
};

static void count_logger_matches(int idx, const struct etc_device_reclaim_request *req, void *ctx_v)
{
	struct logger_match_ctx *ctx = ctx_v;
	ARG_UNUSED(idx);
	if (strcmp(req->logger_id, ctx->target) == 0) {
		ctx->matches++;
	}
}

ZTEST(etc_device_relay_test, test_reclaim_for_each_filters_logger)
{
	reclaim_reset();
	char id_a[] = "10000301";
	char id_b[] = "10000302";
	zassert_ok(etc_relay_reclaim_set(id_a, 1, 2));
	zassert_ok(etc_relay_reclaim_set(id_b, 3, 4));
	zassert_ok(etc_relay_reclaim_set(id_a, 5, 6));

	struct logger_match_ctx ctx = {.target = id_a, .matches = 0};
	int visited = etc_relay_reclaim_for_each(count_logger_matches, &ctx);
	zassert_equal(visited, 3);
	zassert_equal(ctx.matches, 2);
}

ZTEST(etc_device_relay_test, test_reclaim_full_returns_enomem)
{
	reclaim_reset();
	char id[] = "10000400";
	/* Fill every slot with fresh requests so no stale eviction is possible. */
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		zassert_ok(etc_relay_reclaim_set(id, i * 10, i * 10 + 5));
	}
	zassert_equal(etc_relay_reclaim_count(), ETC_RECLAIM_RELAY_MAX_ELEMENT);

	int rc = etc_relay_reclaim_set(id, 9999, 10000);
	zassert_equal(rc, -ENOMEM,
		      "Expected -ENOMEM when buffer full with no stale entries, got %d", rc);
}

ZTEST_SUITE(etc_device_relay_test, NULL, test_setup, NULL, NULL, NULL);