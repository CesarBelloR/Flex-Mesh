/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Unit tests for the functional test result evaluation, focused on the
 * battery-connected check (FW-169).
 */
#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>

#include "etc_functional_test.h"
#include "etc_settings.h"

LOG_MODULE_REGISTER(functional_test_ut, CONFIG_ETC_APP_LOG_LEVEL);

/* Sensor reference value used by the functional test for the AMBIENT device's
 * single checked input (SENSOR_INPUT_IN1). */
#define FUNC_TEST_IN1_GOOD_VALUE (-4.39f)
#define FUNC_TEST_GOOD_RSRP (-100)
#define FUNC_TEST_GOOD_BAT_MV (4100)
#define FUNC_TEST_LOW_BAT_MV (3000)

static void noop_handler(const enum functional_test_evt evt)
{
	ARG_UNUSED(evt);
}

/* Seed a fully-passing functional test, overriding battery presence and
 * voltage, and return the evaluated result. */
static enum functional_test_result run_eval(bool battery_connected, uint16_t battery_mV)
{
	struct sensor_data sensor = {0};
	struct functional_test_data data = {0};
	int16_t rsrp = FUNC_TEST_GOOD_RSRP;
	bool device_id_default = false;

	sensor.sensor[SENSOR_INPUT_IN1] = FUNC_TEST_IN1_GOOD_VALUE;
	sensor.battery_mV = battery_mV;

	functional_test_start(noop_handler);

	track_functional_test(DATA_TYPE_SENSOR, &sensor);
	track_functional_test(DATA_TYPE_MODEM, &rsrp);
	track_functional_test(DATA_TYPE_DEVICE_ID_DEFAULT, &device_id_default);
	track_functional_test(DATA_TYPE_BATTERY_CONNECTED, &battery_connected);

	functional_test_get_data(&data);
	functional_test_stop();

	return data.result;
}

static void *test_setup(void)
{
	/* AMBIENT only checks SENSOR_INPUT_IN1, keeping the fixture small. */
	mock_settings_set_device_type(ETC_DEVICE_TYPE_AMBIENT);
	mock_settings_set_rsrp(-103);
	return NULL;
}

ZTEST_SUITE(functional_test, NULL, test_setup, NULL, NULL, NULL);

ZTEST(functional_test, test_battery_connected_passes)
{
	zassert_equal(run_eval(true, FUNC_TEST_GOOD_BAT_MV), FUNC_TEST_SUCCESS,
		      "Expected success when a battery is connected");
}

ZTEST(functional_test, test_battery_disconnected_fails)
{
	zassert_equal(run_eval(false, FUNC_TEST_GOOD_BAT_MV),
		      FUNC_TEST_FAIL_BAT_DISCONNECTED,
		      "Expected FUNC_TEST_FAIL_BAT_DISCONNECTED with no battery");
}

ZTEST(functional_test, test_disconnected_takes_precedence_over_low_voltage)
{
	/* A disconnected battery should be reported even if the (charger-held)
	 * voltage reading would otherwise look low. */
	zassert_equal(run_eval(false, FUNC_TEST_LOW_BAT_MV),
		      FUNC_TEST_FAIL_BAT_DISCONNECTED,
		      "Disconnected battery must outrank low-voltage failure");
}
