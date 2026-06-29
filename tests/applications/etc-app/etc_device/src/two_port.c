/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "etc_device.h"
#include "etc_device_helper.h"

/* Provided by the mocked etc_settings so each test can pick the device type
 * that etc_device_map_two_port_sensor_data() reads via etc_get_device_type().
 */
void mock_set_device_type(enum etc_device_type type);

/* IN1=10, IN2=11, IN3=12, IN4=13, IN5(1.B)=20, IN6(2.B)=21, IN7/IN8 unused,
 * ambient=23.5, humidity invalid.
 */
static void seed_sensors(float *sensor)
{
	sensor[SENSOR_INPUT_IN1] = 10.0f;
	sensor[SENSOR_INPUT_IN2] = 11.0f;
	sensor[SENSOR_INPUT_IN3] = 12.0f;
	sensor[SENSOR_INPUT_IN4] = 13.0f;
	sensor[SENSOR_INPUT_IN5] = 20.0f;
	sensor[SENSOR_INPUT_IN6] = 21.0f;
	sensor[SENSOR_INPUT_IN7] = 30.0f;
	sensor[SENSOR_INPUT_IN8] = 31.0f;
	sensor[SENSOR_INPUT_AMBIENT] = 23.5f;
	sensor[SENSOR_INPUT_HUMID] = SENSOR_HUMID_NO_CONNECTED;
}

/* The 2-way splitter sub-ports (IN5/IN6) must land in IN3/IN4 and be cleared. */
static void assert_two_port_mapped(const float *sensor)
{
	zassert_equal(sensor[SENSOR_INPUT_IN1], 10.0f);
	zassert_equal(sensor[SENSOR_INPUT_IN2], 11.0f);
	zassert_equal(sensor[SENSOR_INPUT_IN3], 20.0f, "1.B must map to IN3");
	zassert_equal(sensor[SENSOR_INPUT_IN4], 21.0f, "2.B must map to IN4");
	zassert_equal(sensor[SENSOR_INPUT_IN5], (float)SENSOR_TEMP_NO_CONNECTED);
	zassert_equal(sensor[SENSOR_INPUT_IN6], (float)SENSOR_TEMP_NO_CONNECTED);
	/* A 2-port device has no 3.B/4.B; IN7/IN8 must be cleared too. */
	zassert_equal(sensor[SENSOR_INPUT_IN7], (float)SENSOR_TEMP_NO_CONNECTED);
	zassert_equal(sensor[SENSOR_INPUT_IN8], (float)SENSOR_TEMP_NO_CONNECTED);
}

ZTEST(etc_device_two_port_test, test_embeddable_maps_splitter)
{
	float sensor[SENSOR_INPUT_MAX];

	seed_sensors(sensor);
	mock_set_device_type(ETC_DEVICE_TYPE_EMBEDDABLE);
	etc_device_map_two_port_sensor_data(sensor);
	assert_two_port_mapped(sensor);
}

ZTEST(etc_device_two_port_test, test_ambient_maps_splitter)
{
	float sensor[SENSOR_INPUT_MAX];

	seed_sensors(sensor);
	mock_set_device_type(ETC_DEVICE_TYPE_AMBIENT);
	etc_device_map_two_port_sensor_data(sensor);
	assert_two_port_mapped(sensor);
}

ZTEST(etc_device_two_port_test, test_logger_unchanged)
{
	float sensor[SENSOR_INPUT_MAX];

	seed_sensors(sensor);
	mock_set_device_type(ETC_DEVICE_TYPE_LOGGER);
	etc_device_map_two_port_sensor_data(sensor);

	zassert_equal(sensor[SENSOR_INPUT_IN3], 12.0f, "logger IN3 must be untouched");
	zassert_equal(sensor[SENSOR_INPUT_IN4], 13.0f, "logger IN4 must be untouched");
	zassert_equal(sensor[SENSOR_INPUT_IN5], 20.0f, "logger IN5 must be untouched");
	zassert_equal(sensor[SENSOR_INPUT_IN6], 21.0f, "logger IN6 must be untouched");
	zassert_equal(sensor[SENSOR_INPUT_IN7], 30.0f, "logger IN7 must be untouched");
	zassert_equal(sensor[SENSOR_INPUT_IN8], 31.0f, "logger IN8 must be untouched");
}

static void test_two_port_after(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Restore the default so other suites in this binary are unaffected. */
	mock_set_device_type(ETC_DEVICE_TYPE_LOGGER);
}

ZTEST_SUITE(etc_device_two_port_test, NULL, NULL, NULL, test_two_port_after, NULL);
