/*
 * Copyright (c) 2025 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <math.h>
#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_util.h"
#include "etc_device_helper.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(etc_device_record_test, CONFIG_ETC_APP_LOG_LEVEL);

#define EPSILON 0.01f

static void *test_setup(void)
{
	return NULL;
}

static void test_teardown(void *)
{
}

static void etc_device_helper_common_test(struct sensor_data *original)
{

	uint8_t buf[256];
	size_t buf_len = sizeof(buf);

	int ret = etc_device_encode_cbor_data(original, buf, &buf_len);
	zassert_equal(ret, 0);

	buf_len = sizeof(buf);
	struct sensor_data decoded = {0};
	ret = etc_device_decode_cbor_data(&decoded, buf, buf_len);
	zassert_equal(ret, 0);

	zassert_equal(original->timestamp, decoded.timestamp);
	zassert_equal(original->battery_mV, decoded.battery_mV);

	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++) {
		if (sensor_temperature_is_valid(original->sensor[i])) {
			ret = (fabsf((double)(original->sensor[i] - decoded.sensor[i])) < EPSILON);
			zassert_equal(ret, 1);
		}
	}

	if (sensor_humidity_is_valid(original->sensor[SENSOR_INPUT_HUMID])) {
		ret = (fabsf((double)(original->sensor[SENSOR_INPUT_HUMID] -
				      decoded.sensor[SENSOR_INPUT_HUMID])) < EPSILON);
		zassert_equal(ret, 1);
	}
}

ZTEST(etc_device_helper_test, test_case)
{
	struct sensor_data test_data_1 = {
		.timestamp = 1234567890,
		.battery_mV = 3800,
		.sensor = {25.5f, 26.0f, 26.5f, 27.0f, 27.5f, 27.5f, 27.5f, 27.5f, 27.5f, 80.0f},
		.battery_status = 1};

	struct sensor_data test_data_2 = {
		.timestamp = 1234567890,
		.battery_mV = 3800,
		.sensor = {25.5f, -31.0f, 120.5f, 27.0f, 27.5f, 27.5f, 27.5f, 27.5f, 27.5f, 80.0f},
		.battery_status = 1};

	struct sensor_data test_data_3 = {
		.timestamp = 1234567890,
		.battery_mV = 3800,
		.sensor = {25.5f, -31.0f, 120.5f, 27.0f, 27.5f, 27.5f, 27.5f, 27.5f, 27.5f, 110.0f},
		.battery_status = 1};

	struct sensor_data test_data_4 = {
		.timestamp = 1234567890,
		.battery_mV = 3800,
		.sensor = {25.5f, -31.0f, 120.5f, 27.0f, 27.5f, 27.5f, 27.5f, 27.5f, 27.5f, -1.0f},
		.battery_status = 1};

	struct sensor_data test_data_5 = {.timestamp = 1234567890,
					  .battery_mV = 3800,
					  .sensor = {-31.0f, -31.0f, -31.0f, -31.0f, -31.0f, 27.5f,
						     27.5f, 27.5f, 27.5f, -1.0f},
					  .battery_status = 1};

	etc_device_helper_common_test(&test_data_1);
	etc_device_helper_common_test(&test_data_2);
	etc_device_helper_common_test(&test_data_3);
	etc_device_helper_common_test(&test_data_4);
	etc_device_helper_common_test(&test_data_5);
}

ZTEST_SUITE(etc_device_helper_test, NULL, test_setup, NULL, NULL, test_teardown);