/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <time.h>
#include <app_version.h>
#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_settings.h"
#include "etc_util.h"
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include "app_module_helper.h"

LOG_MODULE_REGISTER(etc_interval_time, CONFIG_ETC_APP_LOG_LEVEL);

uint16_t tx_offset_logger_lora_mins = 45;
int tx_normal_secs = 0;
int hour_offset = 0;
uint16_t tx_offset_no_probe_mins = 0;
uint16_t tx_delay_sec = 0;
uint16_t tx_cloud_sync_hour = 0;

static void *test_setup(void)
{
	etc_device_nvs_init();
	etc_settings_init();
	tx_offset_no_probe_mins = etc_device_get_tx_no_probe_offset_mins();
	tx_delay_sec = etc_get_tx_delay_msec() / 1000;
	hour_offset = etc_device_get_tx_probe_second() / 3600;
	tx_normal_secs = etc_device_get_tx_interval_second();
	tx_cloud_sync_hour = etc_device_get_tx_lora_cloud_sync_hour();
	LOG_INF("Probe secs %d", tx_offset_no_probe_mins * 60);
	LOG_INF("Logger Lora sec: %d", tx_offset_logger_lora_mins * 60);
	LOG_INF("Delay sec: %d", tx_delay_sec);
	LOG_INF("Normal tx sec: %d", tx_normal_secs);
	LOG_INF("Tx Probe hour: %d", hour_offset);
	LOG_INF("Cloud sync hour: %d", tx_cloud_sync_hour);
	return NULL;
}

struct transit_time {
	time_t normal;
	time_t lora_sync_cloud;
	time_t no_probe;
};

static struct transit_time tx_time = {0};

const char *get_device_mode_string(enum etc_device_mode device_mode)
{
	switch (device_mode) {
	case ETC_DEVICE_MODE_RELAY:
		return "Relay";
	case ETC_DEVICE_MODE_LORA_LOGGER:
		return "Logger Lora";
	case ETC_DEVICE_MODE_LTE_LOGGER:
		return "Logger LTE";
	case ETC_DEVICE_MODE_BLE:
		return "BLE";
	}
	return "NA";
}

const char *get_power_mode_string(enum etc_power_mode_e mode)
{
	switch (mode) {
	case ETC_POWER_MODE_ALWAYS_ON:
		return "AO";
	case ETC_POWER_MODE_INTERVAL:
		return "Interval";
	case ETC_POWER_MODE_PROBE:
		return "Probe";
	}
	return "NA";
}

const char *get_sensor_status_string(enum etc_sensor_status status)
{
	switch (status) {
	case SENSOR_NO_CONNECTION:
		return "No Sensor";
	case SENSOR_CONNECTED:
		return "Sensor";
	case SENSOR_NA:
		return "NA";
	}
	return "NA";
}

time_t etc_interval_test_calculate_max_time_normal(time_t input_time)
{
	struct tm *time_info = gmtime(&input_time);
	int default_interval_mins = DEFAULT_PUBLISH_INTERVAL_S / 60;
	int new_min = ((time_info->tm_min / default_interval_mins) + 1) * default_interval_mins;
	int offset = new_min - time_info->tm_min;
	return input_time + offset * 60 + tx_delay_sec;
}

time_t etc_interval_test_calculate_max_time_no_probe(time_t input_time)
{
	struct tm *time_info = gmtime(&input_time);
	int new_hour = ((time_info->tm_hour / hour_offset) + 1) * hour_offset;
	int offset_sec = (new_hour - time_info->tm_hour) * 3600 + tx_offset_no_probe_mins * 60;
	if (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) {
		offset_sec = ((offset_sec / DEFAULT_PUBLISH_INTERVAL_S) + 1) *
			     DEFAULT_PUBLISH_INTERVAL_S;
	}
	return input_time + offset_sec + tx_delay_sec;
}

bool etc_interval_test_verify_normal_transmit(time_t now, time_t normal_tx)
{
	time_t max_time = etc_interval_test_calculate_max_time_normal(now);
	app_module_print_time_debug(max_time, "Normal max transmit");
	if (normal_tx >= now + tx_delay_sec && normal_tx <= max_time) {
		return true;
	}
	return false;
}

bool etc_interval_test_verify_no_proble(time_t now, time_t no_probe_tx)
{
	time_t max_time = etc_interval_test_calculate_max_time_no_probe(now);
	app_module_print_time_debug(max_time, "No Probe max transmit");
	if (no_probe_tx >= now + tx_delay_sec && no_probe_tx <= max_time &&
	    (no_probe_tx % DEFAULT_PUBLISH_INTERVAL_S) <= 60) {
		return true;
	}
	return false;
}

bool etc_interval_test_verify_lora_sync(time_t now, time_t lora_sync_tx)
{
	return true;
}

struct transit_time etc_interval_time_get(time_t now, enum etc_device_mode device_mode,
					  enum etc_power_mode_e power_mode,
					  enum etc_sensor_status sensor_status)
{
	LOG_INF("Device %s - Power %s - Sensor %s", get_device_mode_string(device_mode),
		get_power_mode_string(power_mode), get_sensor_status_string(sensor_status));

	etc_set_device_mode(device_mode);
	etc_set_power_mode(power_mode);
	zassert_equal(etc_get_device_mode(), device_mode);
	zassert_equal(etc_get_power_mode(), power_mode);
	app_module_print_time_debug(now, "NOW");
	/* Get next transmit in normal case */
	tx_time.normal = app_module_get_next_transmit_for_interval_or_probe(now, tx_normal_secs,
										sensor_status);
	/* Get next transmit in logger lora case (-1 is no plan for next transmit) */
	tx_time.lora_sync_cloud = app_module_get_next_transmit_lora_sync_cloud(
		now, tx_offset_logger_lora_mins, sensor_status);
	/* Get next transmit in no probe case (-1 is no plan for next transmit) */
	tx_time.no_probe = app_module_get_next_transmit_no_probe(now, tx_offset_no_probe_mins,
								     sensor_status);
	app_module_print_time_debug(tx_time.normal, "Normal with PROBE");
	app_module_print_time_debug(tx_time.lora_sync_cloud, "Lora Sync");
	app_module_print_time_debug(tx_time.no_probe, "No probe");
	return tx_time;
}

ZTEST(etc_interval_time_test, test_logger_lte_interval_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_CONNECTED);
	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.lora_sync_cloud, -1);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

ZTEST(etc_interval_time_test, test_logger_lte_probe_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(now, ETC_DEVICE_MODE_LTE_LOGGER,
							    ETC_POWER_MODE_PROBE, SENSOR_CONNECTED);

	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.lora_sync_cloud, -1);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

ZTEST(etc_interval_time_test, test_logger_lte_interval_no_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_NO_CONNECTION);
	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.lora_sync_cloud, -1);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

ZTEST(etc_interval_time_test, test_logger_lte_probe_no_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LTE_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION);
	bool ret = etc_interval_test_verify_no_proble(now, tx_time.no_probe);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.lora_sync_cloud, -1);
}

ZTEST(etc_interval_time_test, test_logger_lora_interval_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_CONNECTED);
	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	ret = etc_interval_test_verify_lora_sync(now, tx_time.lora_sync_cloud);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

ZTEST(etc_interval_time_test, test_logger_lora_probe_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(now, ETC_DEVICE_MODE_LORA_LOGGER,
							    ETC_POWER_MODE_PROBE, SENSOR_CONNECTED);
	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	ret = etc_interval_test_verify_lora_sync(now, tx_time.lora_sync_cloud);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

ZTEST(etc_interval_time_test, test_logger_lora_interval_no_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_INTERVAL, SENSOR_NO_CONNECTION);
	bool ret = etc_interval_test_verify_normal_transmit(now, tx_time.normal);
	zassert_equal(ret, true);
	ret = etc_interval_test_verify_lora_sync(now, tx_time.lora_sync_cloud);
	zassert_equal(ret, true);
	/* Must be -1 */
	zassert_equal(tx_time.no_probe, -1);
}

void on_test_logger_lora_probe_no_sensor(time_t now)
{
	struct transit_time tx_time = etc_interval_time_get(
		now, ETC_DEVICE_MODE_LORA_LOGGER, ETC_POWER_MODE_PROBE, SENSOR_NO_CONNECTION);
	bool ret = etc_interval_test_verify_no_proble(now, tx_time.no_probe);
	zassert_equal(ret, true);
}

ZTEST(etc_interval_time_test, test_logger_lora_probe_no_sensor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	time_t next_now = now + 86400;
	do {
		on_test_logger_lora_probe_no_sensor(now);
		now = (now + sys_rand32_get() % 900);
	} while (now < next_now);
}
/* FW-965: the regular transmit window spans [interval boundary, boundary +
 * rx_duration]. Inside it the standard tx delay applies; outside it (with a
 * reclaim active) a shorter reclaim-burst delay may be used. */
ZTEST(etc_interval_time_test, test_in_regular_tx_window)
{
	int interval = etc_device_get_tx_interval_second();
	int duration = etc_device_get_rx_duration();

	/* The default interval is aligned to an absolute boundary. */
	zassert_true(app_module_is_aligned_interval(interval));

	time_t boundary = (APP_UNIT_TIMESTAMP / interval) * interval;

	/* At and just inside the window edge -> in window. */
	zassert_true(app_module_in_regular_tx_window(boundary));
	zassert_true(app_module_in_regular_tx_window(boundary + duration));
	/* Just past the window and right before the next boundary -> outside. */
	zassert_false(app_module_in_regular_tx_window(boundary + duration + 1));
	zassert_false(app_module_in_regular_tx_window(boundary + interval - 1));
	/* The next boundary opens a fresh window again. */
	zassert_true(app_module_in_regular_tx_window(boundary + interval));
}

/* FW-788/789: opportunistic LTE upload policy. The backoff window is
 * tx_interval x CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE (the floor), doubled
 * once per consecutive failure and clamped to CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S. */

/* FW-788: only strictly more than the threshold triggers the LTE upload. */
ZTEST(etc_interval_time_test, test_lte_sync_threshold)
{
	etc_set_device_mode(ETC_DEVICE_MODE_LORA_LOGGER);
	zassert_false(app_module_lte_sync_over_threshold(0));
	zassert_false(app_module_lte_sync_over_threshold(1));
	zassert_false(app_module_lte_sync_over_threshold(2));
	zassert_false(app_module_lte_sync_over_threshold(CONFIG_ETC_APP_LTE_SYNC_NACK_THRESHOLD));
	zassert_true(
		app_module_lte_sync_over_threshold(CONFIG_ETC_APP_LTE_SYNC_NACK_THRESHOLD + 1));
	zassert_true(app_module_lte_sync_over_threshold(100));
}

/* FW-788: the policy applies only to LoRa loggers. */
ZTEST(etc_interval_time_test, test_lte_sync_wrong_mode)
{
	etc_set_device_mode(ETC_DEVICE_MODE_LTE_LOGGER);
	zassert_false(app_module_lte_sync_over_threshold(10));
	etc_set_device_mode(ETC_DEVICE_MODE_RELAY);
	zassert_false(app_module_lte_sync_over_threshold(10));
	etc_set_device_mode(ETC_DEVICE_MODE_BLE);
	zassert_false(app_module_lte_sync_over_threshold(10));
}

/* FW-789: the very first bring-up (no prior attempt) is always due. */
ZTEST(etc_interval_time_test, test_lte_sync_first_attempt)
{
	time_t now = APP_UNIT_TIMESTAMP;
	zassert_true(app_module_lte_sync_due(now, 0, 0));
	zassert_true(app_module_lte_sync_due(now, 0, 5));
}

/* FW-789 floor: with no failures the window is 4x tx_interval. */
ZTEST(etc_interval_time_test, test_lte_sync_floor)
{
	time_t now = APP_UNIT_TIMESTAMP;
	int interval = etc_device_get_tx_interval_second();
	time_t base = (time_t)interval * CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE;

	zassert_equal(app_module_lte_sync_backoff_s(0), (uint32_t)base);
	zassert_false(app_module_lte_sync_due(now, now - interval, 0));
	zassert_false(app_module_lte_sync_due(now, now - 2 * interval, 0));
	zassert_false(app_module_lte_sync_due(now, now - (base - 60), 0));
	zassert_true(app_module_lte_sync_due(now, now - base, 0));
}

/* FW-789: doubling begins on the first failure ("double immediately"). */
ZTEST(etc_interval_time_test, test_lte_sync_exponential)
{
	int interval = etc_device_get_tx_interval_second();
	uint32_t base = (uint32_t)interval * CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE;

	zassert_equal(app_module_lte_sync_backoff_s(0), base);
	zassert_equal(app_module_lte_sync_backoff_s(1), base * 2);
	zassert_equal(app_module_lte_sync_backoff_s(2), base * 4);
	zassert_equal(app_module_lte_sync_backoff_s(3), base * 8);
}

/* FW-789 ceiling: the window never exceeds the configured maximum (once a day). */
ZTEST(etc_interval_time_test, test_lte_sync_ceiling)
{
	etc_set_tx_interval_secs(60);
	/* 60 x 4 = 240 s base; doubling many times must clamp, not overflow. */
	zassert_equal(app_module_lte_sync_backoff_s(31),
		      (uint32_t)CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S);
	zassert_equal(app_module_lte_sync_backoff_s(100),
		      (uint32_t)CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S);
	etc_set_tx_interval_secs(tx_normal_secs);
}

/* FW-789: the window scales with tx_interval, not a hardcoded hour. */
ZTEST(etc_interval_time_test, test_lte_sync_scales_with_tx_interval)
{
	etc_set_tx_interval_secs(300);
	zassert_equal(app_module_lte_sync_backoff_s(0),
		      300U * CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE);
	etc_set_tx_interval_secs(900);
	zassert_equal(app_module_lte_sync_backoff_s(0),
		      900U * CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE);
	etc_set_tx_interval_secs(tx_normal_secs);
}

/* FW-789: the slack absorbs RTC rounding at the window boundary. */
ZTEST(etc_interval_time_test, test_lte_sync_slack_boundary)
{
	time_t now = APP_UNIT_TIMESTAMP;
	time_t base = (time_t)etc_device_get_tx_interval_second() *
		      CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE;

	zassert_true(app_module_lte_sync_due(now, now - (base - APP_LTE_SYNC_SLACK_S), 0));
	zassert_false(app_module_lte_sync_due(now, now - (base - APP_LTE_SYNC_SLACK_S - 1), 0));
}

/* FW-789: an invalid or backwards clock never blocks an upload for a whole window. */
ZTEST(etc_interval_time_test, test_lte_sync_invalid_clock)
{
	time_t now = APP_UNIT_TIMESTAMP;
	zassert_false(app_module_lte_sync_due(0, now - 100000, 0));
	zassert_false(app_module_lte_sync_due(-1, now - 100000, 0));
	/* last_attempt in the future (clock stepped back) -> allow and re-stamp. */
	zassert_true(app_module_lte_sync_due(now, now + 3600, 0));
}

ZTEST_SUITE(etc_interval_time_test, NULL, test_setup, NULL, NULL, NULL);