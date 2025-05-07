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

ZTEST(etc_device_helper_test, test_01)
{
	struct sensor_data original = {.timestamp = 1234567890,
				       .battery_mV = 3800,
				       .sensor = {25.5f, 26.0f, 26.5f, 27.0f, 27.5f, 28.0f},
				       .battery_status = 1};

	uint8_t buf[256];
	size_t buf_len = sizeof(buf);

	int ret = etc_common_encode_sensor_data(&original, buf, &buf_len);
	zassert_equal(ret, 0);

	struct sensor_data decoded = {0};
	ret = etc_common_decode_sensor_data(&decoded, buf, buf_len);
	zassert_equal(ret, 0);

	zassert_equal(original.timestamp, decoded.timestamp);
	zassert_equal(original.battery_mV, decoded.battery_mV);
	zassert_equal(original.battery_status, decoded.battery_status);

	for (int i = 0; i < ETC_DEVICE_NUM_SENSOR; i++) {
		ret = (fabsf((double)(original.sensor[i] - decoded.sensor[i])) < EPSILON);
		zassert_equal(ret, 1);
	}
}

ZTEST_SUITE(etc_device_helper_test, NULL, test_setup, NULL, NULL, test_teardown);