/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <math.h>
#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_util.h"
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(etc_device_record_test, CONFIG_ETC_APP_LOG_LEVEL);

#define FLOAT_TOLERANCE 0.5f

const static struct device *retained_ram_dev = DEVICE_DT_GET(DT_ALIAS(record_header_ram));
struct etc_device_record_table last_current_record;

#if CONFIG_BOARD_NATIVE_SIM
#define RECORD_ARRAY_SIZE (ETC_RECORD_MAX_RECORD)
#elif CONFIG_BOARD_ETC
#define RECORD_ARRAY_SIZE 6000
#else
#error "No support other SoC"
#endif

#if defined(CONFIG_ETC_RECORD_CBOR)
int8_t etc_sensor_get_probe_humid_index(void)
{
	return 5;
}
#endif

const struct sensor_data record_sample = {
	.timestamp = 1234567890,
	.battery_mV = 3800,
	.sensor = {12.1f, 87.1f, -6.2f, 32.6f, 0.2f, 0.3f, 0.3f, 0.3f, 0.3f, 99.9f},
	.battery_status = 1};

struct sensor_data records[RECORD_ARRAY_SIZE];

bool compare_sensor_records(const union etc_device_record *record, const struct sensor_data *sensor)
{
	bool is_equal = true;
	float sensor_battery_volts = sensor->battery_mV / 1000.0f;
	if (fabsf(sensor_battery_volts - record->battery) > FLOAT_TOLERANCE) {
		is_equal = false;
	}

	for (size_t i = 0; i < ETC_DEVICE_NUM_SENSOR; i++) {
		if (fabsf(sensor->sensor[i] - record->sensor[i]) > FLOAT_TOLERANCE) {
			LOG_ERR("Sensor %zu: %f != %f", i, sensor->sensor[i], record->sensor[i]);
			is_equal = false;
		}
	}

	if (sensor->timestamp != (int64_t)record->timestamp) {
		LOG_ERR("Sensor %zu: %f != %f", sensor->timestamp, record->timestamp);
		is_equal = false;
	}

	return is_equal;
}

static void *test_setup(void)
{
	int64_t index;
	etc_device_nvs_init();
	/* Make sure the record is fresh */
	etc_device_record_clean_up();
	etc_device_record_erase_record_flash();
	/* Populate records */
	for (int i = 0; i < ARRAY_SIZE(records); i++) {
		records[i].timestamp = record_sample.timestamp + i;
		records[i].battery_mV = record_sample.battery_mV;
		for (int j = 0; j < SENSOR_EVENT_NUM_DEV_MAX; j++) {
			records[i].sensor[j] = record_sample.sensor[j];
		}
		records[i].battery_status = 1;
	}

	return NULL;
}

static void test_teardown(void *)
{
	/* Allow ack/nack status to be saved to flash */
	k_sleep(K_SECONDS(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC + 1));
}

static void on_write_num_records(uint16_t num_records, bool check_ack, bool check_index,
				 uint16_t index)
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
		struct etc_device_record_table *status = etc_device_record_get_status();
		memcpy(&prev_status, status, sizeof(*status));
		zassert_equal(last_ack, total_record - last_nack, "ACK NOT OK %d %d", last_nack,
			      total_record - last_nack);
	}

	for (int i = 0; i < num_records; i++) {
		ret = etc_device_write_record_sensor(&records[i]);
		if (check_index && index == i) {
			/* This record should be next of last current record */
			struct etc_device_record_table *status = etc_device_record_get_status();
			struct etc_device_record_table *pre_status = &last_current_record;
			uint16_t next_of_pre_record_id =
				etc_device_get_next_id_by_index(&pre_status->newest);
			uint16_t current_id = etc_device_record_get_id_by_index(&status->newest);
			zassert_equal(next_of_pre_record_id, current_id, "ID -> NOT OK %d %d",
				      next_of_pre_record_id, current_id);
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
		struct etc_device_record_table *status = etc_device_record_get_status();
		LOG_DBG("(%d,%d) - (%d,%d) %d %d", prev_status.newest.sector_idx,
			prev_status.newest.element_idx, prev_status.oldest.sector_idx,
			prev_status.oldest.element_idx, last_newest_id, last_oldest_id);
		uint16_t newest_id = etc_device_record_get_latest_id();
		uint16_t oldest_id = etc_device_record_get_oldest_id();
		LOG_DBG("(%d,%d) - (%d,%d) %d %d", status->newest.sector_idx,
			status->newest.element_idx, status->oldest.sector_idx,
			status->oldest.element_idx, newest_id, oldest_id);
		LOG_DBG("%d %d %d %d", total_record, num_records, last_ack, last_nack);
		uint16_t nack_count = 0;
		if (newest_id < last_newest_id) {
			nack_count = (newest_id + 1) + (MAX_RECORD_NO_OFFSET_ID + 1) -
				     (last_newest_id + 1);
		} else {
			if (last_newest_id == 0) {
				nack_count = newest_id + 1 - (last_newest_id);
			} else {
				nack_count = newest_id - last_newest_id;
			}
		}
		nack_count =
			(nack_count >= ETC_RECORD_MAX_RECORD) ? ETC_RECORD_MAX_RECORD : nack_count;

		total_record = etc_device_record_get_total_record();
		uint16_t ack_count = total_record - nack_count;
		last_ack = etc_device_record_get_num_ack();
		last_nack = etc_device_nack_count();
		LOG_DBG("%d %d %d %d %d", total_record, num_records, last_ack, last_nack,
			nack_count);
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
		ret = compare_sensor_records(&read_record, &records[i]);
		zassert_true(ret, "record %d %d", record_id, i);
		etc_device_set_ack_record(record_id);
		nack_count--;
		zassert_equal(nack_count, etc_device_nack_count());
		ack_count++;
		zassert_equal(ack_count, etc_device_record_get_num_ack(), "new_ack: %d",
			      etc_device_record_get_num_ack());
	}
}

static void on_save_record_status(void)
{
	struct etc_device_record_table *status = etc_device_record_get_status();
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
		on_write_num_records(requests, true, false, 0);
		on_read_num_records(requests);
	}
}

/*
 * FW-954: current (non-ack'd) readings must always take precedence over an
 * active reclaim. The helpers below build a store of historical, already-sent
 * (ACK'd) records, then a reclaim is scheduled over a sub-window of them while
 * fresh readings are injected.
 */
#define RECLAIM_HIST_COUNT 20 /* historical records to seed and ACK */
#define RECLAIM_WIN_START  5  /* reclaim window, by record offset (inclusive) */
#define RECLAIM_WIN_STOP   14

static int64_t reclaim_ts_offset(const union etc_device_record *rec)
{
	return (int64_t)rec->timestamp - (int64_t)record_sample.timestamp;
}

/* Read one record (NACK or reclaim), ACK it as the TX path would, and report
 * its timestamp offset and the reclaim-in-progress flag. Returns the record id.
 */
static int reclaim_read_one(int64_t *offset, bool *reclaim)
{
	union etc_device_record rec;
	bool r = false;
	int id = etc_device_read_record(&rec, &r);
	if (id > 0) {
		if (offset != NULL) {
			*offset = reclaim_ts_offset(&rec);
		}
		etc_device_set_ack_record(id);
	}
	if (reclaim != NULL) {
		*reclaim = r;
	}
	return id;
}

/* Reset to a clean store holding `count` historical records, all ACK'd. */
static void reclaim_seed_history(uint16_t count)
{
	etc_device_nvs_init();
	etc_device_record_clean_up();
	etc_device_record_erase_record_flash();

	for (uint16_t i = 0; i < count; i++) {
		zassert_ok(etc_device_write_record_sensor(&records[i]));
	}

	/* Drain and ACK everything so no NACK records remain. */
	while (reclaim_read_one(NULL, NULL) > 0) {
		/* keep draining */
	}
	zassert_equal(0, etc_device_nack_count(), "history not fully drained");
}

/* New readings queued before the reclaim starts serving must all be sent
 * before any reclaim record, and no readings may be lost.
 */
ZTEST(etc_device_record_test, test_10_new_readings_precede_reclaim)
{
	const int kNew = 6;
	const int new_base = RECLAIM_HIST_COUNT; /* offsets 20..25 */

	reclaim_seed_history(RECLAIM_HIST_COUNT);

	int rc = etc_device_record_reclaim(record_sample.timestamp + RECLAIM_WIN_START,
					   record_sample.timestamp + RECLAIM_WIN_STOP, false);
	zassert_true(rc >= 0, "reclaim schedule failed %d", rc);
	int win = etc_device_record_num_reclaim_records();
	zassert_true(win > 0, "no reclaim window scheduled");

	/* Fresh readings arrive while the reclaim is pending. */
	for (int i = 0; i < kNew; i++) {
		zassert_ok(etc_device_write_record_sensor(&records[new_base + i]));
	}

	bool seen_reclaim_record = false;
	int new_count = 0;
	int reclaim_count = 0;
	int64_t off;
	bool reclaim;

	while (reclaim_read_one(&off, &reclaim) > 0) {
		zassert_true(reclaim, "reclaim should stay in progress (offset %lld)", off);
		if (off >= new_base) {
			zassert_false(seen_reclaim_record,
				      "new reading %lld served after a reclaim record", off);
			new_count++;
		} else {
			zassert_true(off >= RECLAIM_WIN_START && off <= RECLAIM_WIN_STOP,
				     "unexpected record offset %lld", off);
			seen_reclaim_record = true;
			reclaim_count++;
		}
	}

	zassert_equal(kNew, new_count, "served new readings %d", new_count);
	zassert_equal(win, reclaim_count, "served reclaim records %d/%d", reclaim_count, win);
	zassert_equal(0, etc_device_nack_count(), "NACK remained after drain");

	/* The record that completes the reclaim still reports in-progress (the
	 * cloud sees SUCCESS on the following cycle); the next read confirms the
	 * reclaim is finished.
	 */
	zassert_true(reclaim_read_one(&off, &reclaim) <= 0, "store should be empty");
	zassert_false(reclaim, "reclaim should be inactive after completion");
}

/* A reading that arrives mid-reclaim must preempt the remaining reclaim
 * records, and the reclaim must resume and complete afterwards.
 */
ZTEST(etc_device_record_test, test_11_new_reading_preempts_active_reclaim)
{
	const int new_off = RECLAIM_HIST_COUNT; /* offset 20 */

	reclaim_seed_history(RECLAIM_HIST_COUNT);

	int rc = etc_device_record_reclaim(record_sample.timestamp + RECLAIM_WIN_START,
					   record_sample.timestamp + RECLAIM_WIN_STOP, false);
	zassert_true(rc >= 0, "reclaim schedule failed %d", rc);
	int win = etc_device_record_num_reclaim_records();
	zassert_true(win >= 2, "need a multi-record window, got %d", win);

	int64_t off;
	bool reclaim;

	/* No new data pending yet: the first record served is a reclaim record. */
	zassert_true(reclaim_read_one(&off, &reclaim) > 0, "expected a reclaim record");
	zassert_true(reclaim, "reclaim flag should be set");
	zassert_true(off >= RECLAIM_WIN_START && off <= RECLAIM_WIN_STOP,
		     "expected reclaim-window record, got %lld", off);
	int reclaim_count = 1;

	/* A fresh reading arrives in the middle of the reclaim... */
	zassert_ok(etc_device_write_record_sensor(&records[new_off]));

	/* ...and must be served before the rest of the reclaim window. */
	zassert_true(reclaim_read_one(&off, &reclaim) > 0, "expected the new reading");
	zassert_true(reclaim, "reclaim still in progress");
	zassert_equal(new_off, off, "new reading did not preempt reclaim (got %lld)", off);

	/* Drain the remainder: only reclaim-window records should follow. */
	int new_count = 1;
	while (reclaim_read_one(&off, &reclaim) > 0) {
		zassert_true(reclaim, "reclaim active until fully drained");
		if (off == new_off) {
			new_count++;
		} else {
			zassert_true(off >= RECLAIM_WIN_START && off <= RECLAIM_WIN_STOP,
				     "unexpected record offset %lld", off);
			reclaim_count++;
		}
	}

	zassert_equal(1, new_count, "new reading served exactly once");
	zassert_equal(win, reclaim_count, "all reclaim records served once (%d/%d)", reclaim_count,
		      win);
	zassert_equal(0, etc_device_nack_count(), "NACK remained after drain");

	zassert_true(reclaim_read_one(&off, &reclaim) <= 0, "store should be empty");
	zassert_false(reclaim, "reclaim should be inactive after completion");
}

/* FW-966: read-only existence check used by the LwM2M reclaim Execute handler to
 * decide whether any record falls within the requested period.
 */
ZTEST(etc_device_record_test, test_12_reclaim_available)
{
	const int64_t base = record_sample.timestamp;

	reclaim_seed_history(RECLAIM_HIST_COUNT); /* timestamps base+0 .. base+19 */

	/* Window covering seeded records -> available. */
	zassert_equal(1,
		      etc_device_record_reclaim_available(base + RECLAIM_WIN_START,
							  base + RECLAIM_WIN_STOP),
		      "records in window should be available");

	/* A single in-range instant (the oldest record). */
	zassert_equal(1, etc_device_record_reclaim_available(base, base),
		      "first record should be available");

	/* Window entirely before the seeded records -> none. */
	zassert_equal(0, etc_device_record_reclaim_available(base - 100, base - 1),
		      "window before records should be empty");

	/* Window entirely after the seeded records -> none. */
	zassert_equal(0,
		      etc_device_record_reclaim_available(base + RECLAIM_HIST_COUNT,
							  base + RECLAIM_HIST_COUNT + 100),
		      "window after records should be empty");

	/* Invalid range (start > stop) -> error. */
	zassert_equal(-EINVAL,
		      etc_device_record_reclaim_available(base + RECLAIM_WIN_STOP,
							  base + RECLAIM_WIN_START),
		      "start > stop should be rejected");

	/* The check is side-effect free: a real reclaim still schedules afterwards. */
	int rc =
		etc_device_record_reclaim(base + RECLAIM_WIN_START, base + RECLAIM_WIN_STOP, false);
	zassert_true(rc >= 0, "reclaim schedule failed %d", rc);
	zassert_true(etc_device_record_num_reclaim_records() > 0,
		     "reclaim window not scheduled after availability check");
}

ZTEST_SUITE(etc_device_record_test, NULL, test_setup, NULL, NULL, test_teardown);