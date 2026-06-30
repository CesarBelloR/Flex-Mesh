#include <zephyr/ztest.h>
#include "events/sensor_event.h"
#include "etc_calibration.h"
#include "etc_device.h"
#include "etc_sensor_calibration_load.h"

/* Controls implemented by the mock settings store (src/mock/etc_device.c). */
void mock_etc_device_reset(void);
void mock_etc_device_set_float(uint16_t id, float value);

/* All chosen values are exactly representable as float, so == comparisons are
 * safe. The user and factory triples are distinct so a test can tell which one
 * was loaded. */
#define USER_OFFSET 10.0f
#define USER_HIGH   4000.0f
#define USER_REF    3948.75f
#define FACT_OFFSET 20.0f
#define FACT_HIGH   4100.0f
#define FACT_REF    3950.0f

static void seed_user(void)
{
	mock_etc_device_set_float(ETC_CALIBRATION_USER_OFFSET_ID, USER_OFFSET);
	mock_etc_device_set_float(ETC_CALIBRATION_USER_RAWHIGH_ID, USER_HIGH);
	mock_etc_device_set_float(ETC_CALIBRATION_USER_REF_ID, USER_REF);
}

static void seed_factory(void)
{
	mock_etc_device_set_float(ETC_CALIBRATION_OFFSET_ID, FACT_OFFSET);
	mock_etc_device_set_float(ETC_CALIBRATION_RAWHIGH_ID, FACT_HIGH);
	mock_etc_device_set_float(ETC_CALIBRATION_REF_ID, FACT_REF);
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	mock_etc_device_reset();
}

ZTEST_SUITE(etc_sensor_calibration_load, NULL, NULL, before_each, NULL, NULL);

/* FW-588: when both user and factory calibration are present, the user values
 * must win. (The original bug overwrote the user values with factory.) */
ZTEST(etc_sensor_calibration_load, test_user_takes_priority_over_factory)
{
	seed_user();
	seed_factory();

	struct etc_sensor_adc_calibration_info info = {0};
	int rc = etc_sensor_load_calibration_info(&info);

	zassert_ok(rc, "load failed (%d)", rc);
	zassert_true(info.loaded, "info not marked loaded");
	zassert_equal(info.offset, USER_OFFSET, "offset %f != user", (double)info.offset);
	zassert_equal(info.high, USER_HIGH, "high %f != user", (double)info.high);
	zassert_equal(info.ref, USER_REF, "ref %f != user", (double)info.ref);
}

/* With no user calibration stored, the factory calibration is used. */
ZTEST(etc_sensor_calibration_load, test_factory_fallback_when_user_absent)
{
	seed_factory();

	struct etc_sensor_adc_calibration_info info = {0};
	int rc = etc_sensor_load_calibration_info(&info);

	zassert_ok(rc, "load failed (%d)", rc);
	zassert_true(info.loaded, "info not marked loaded");
	zassert_equal(info.offset, FACT_OFFSET, "offset %f != factory", (double)info.offset);
	zassert_equal(info.high, FACT_HIGH, "high %f != factory", (double)info.high);
	zassert_equal(info.ref, FACT_REF, "ref %f != factory", (double)info.ref);
}

/* A partial user calibration (only some keys present) must fall back to factory
 * entirely rather than mixing user and factory coefficients. */
ZTEST(etc_sensor_calibration_load, test_partial_user_falls_back_to_factory)
{
	mock_etc_device_set_float(ETC_CALIBRATION_USER_OFFSET_ID, USER_OFFSET);
	/* USER_RAWHIGH and USER_REF intentionally absent. */
	seed_factory();

	struct etc_sensor_adc_calibration_info info = {0};
	int rc = etc_sensor_load_calibration_info(&info);

	zassert_ok(rc, "load failed (%d)", rc);
	zassert_true(info.loaded, "info not marked loaded");
	zassert_equal(info.offset, FACT_OFFSET, "offset %f != factory", (double)info.offset);
	zassert_equal(info.high, FACT_HIGH, "high %f != factory", (double)info.high);
	zassert_equal(info.ref, FACT_REF, "ref %f != factory", (double)info.ref);
}

/* When neither user nor factory calibration can be read, report failure and do
 * not mark the info loaded. */
ZTEST(etc_sensor_calibration_load, test_failure_when_no_calibration)
{
	struct etc_sensor_adc_calibration_info info = {0};
	int rc = etc_sensor_load_calibration_info(&info);

	zassert_not_equal(rc, 0, "expected failure with no calibration stored");
	zassert_false(info.loaded, "info should not be marked loaded on failure");
}
