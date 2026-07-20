/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <math.h>

#include "events/sensor_event.h"
#include "etc_calibration.h"
#include "etc_sensor.h"
#include "mock/mock_deps.h"

#define HOLDER_STACK_SIZE 1024
#define HOLDER_PRIO       5

static K_THREAD_STACK_DEFINE(holder_stack, HOLDER_STACK_SIZE);
static struct k_thread holder_thread;

static K_SEM_DEFINE(holder_acquired, 0, 1);
static K_SEM_DEFINE(holder_release, 0, 1);

/* Holds the front-end lock the way a calibration run does: for a long time,
 * from another thread.
 */
static void holder_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	etc_calibration_lock();
	k_sem_give(&holder_acquired);
	k_sem_take(&holder_release, K_FOREVER);
	etc_calibration_unlock();
}

static void start_holder(void)
{
	k_thread_create(&holder_thread, holder_stack, K_THREAD_STACK_SIZEOF(holder_stack),
			holder_fn, NULL, NULL, NULL, HOLDER_PRIO, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&holder_acquired, K_SECONDS(1)),
		   "holder thread failed to take the front-end lock");
}

static void stop_holder(void)
{
	k_sem_give(&holder_release);
	zassert_ok(k_thread_join(&holder_thread, K_SECONDS(1)), "holder thread did not exit");
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	mock_reset();
	/* Nominal hardware: healthy battery and an ambient inside
	 * [CONFIG_TEMP_RANGE_SPECIFIC_MIN, CONFIG_TEMP_RANGE_SPECIFIC_MAX].
	 */
	mock.battery_mv = 4000;
	mock.ambient_temp = 20.0f;
	mock.device_ambient_temp = 20.0f;
	mock.adc_offset_value = 0;
	etc_calibration_init();
}

/* Reads the detail text recorded alongside the result code. */
static const char *current_error_detail(void)
{
	static struct etc_sensor_calibration_status_info info;

	etc_calibration_get_current_status(&info);
	return info.error_detail;
}

/* Status reads must not touch the front-end lock: a single mutex for both used
 * to queue a 4-byte read behind a multi-second hardware session, wedging the
 * data module thread.
 */
ZTEST(etc_calibration_session, test_status_read_does_not_block_on_front_end_lock)
{
	start_holder();

	/* Assert on values so a regression reports the cause rather than hanging. */
	int status = etc_calibration_get_calibration_status();
	int result = etc_calibration_get_calibration_result();

	zassert_equal(status, ETC_SENSOR_CALIB_IDLE, "unexpected status");
	zassert_equal(result, ETC_SENSOR_CALIB_NO_STATUS, "unexpected result");

	stop_holder();
}

/* etc_calibration_set_idle() is called from the cloud-ACK path, which must not
 * stall behind a hardware session either.
 */
ZTEST(etc_calibration_session, test_set_idle_does_not_block_on_front_end_lock)
{
	start_holder();

	etc_calibration_set_idle();
	zassert_equal(etc_calibration_get_calibration_status(), ETC_SENSOR_CALIB_IDLE,
		      "set_idle did not take effect");

	stop_holder();
}

/* etc_calibration_get_current_status() copies the whole struct, including the
 * report buffers, so it must also stay off the front-end lock.
 */
ZTEST(etc_calibration_session, test_get_current_status_does_not_block_on_front_end_lock)
{
	struct etc_sensor_calibration_status_info info = {0};

	start_holder();

	etc_calibration_get_current_status(&info);
	zassert_equal(info.status, ETC_SENSOR_CALIB_IDLE, "unexpected status");

	stop_holder();
}

/* A failed check must hand the front-end back so ordinary sampling resumes,
 * but leave the rail powered: a sample runs right after the swipe and toggling
 * VSENS_EN would corrupt it.
 */
ZTEST(etc_calibration_session, test_check_without_calibrator_reports_enoent)
{
	mock.scan_result = 0;

	zassert_equal(etc_calibration_check(), -ENOENT, "expected -ENOENT with no calibrator");
	zassert_equal(etc_calibration_get_calibration_status(), ETC_SENSOR_CALIB_IDLE,
		      "a failed check must leave the status IDLE");
	zassert_equal(mock.hw_owner, HW_OWNER_NONE,
		      "a failed check must release front-end ownership");
	zassert_true(mock.hw_powered, "a failed check must leave the rail powered");
}

/* FW-611: a completed result awaiting its cloud ACK must not be discarded by a
 * fresh magnet swipe, because its coefficients are already in NVS.
 */
ZTEST(etc_calibration_session, test_check_is_busy_while_result_awaits_upload)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;

	/* Drive the status to DATA_UPLOAD + SUCCESS via a run. */
	zassert_ok(etc_calibration_run_and_teardown(), "run failed");
	zassert_equal(etc_calibration_get_calibration_status(), ETC_SENSOR_CALIB_DATA_UPLOAD,
		      "run should leave the status at DATA_UPLOAD");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_SUCCESS,
		      "run should have succeeded");

	zassert_equal(etc_calibration_check(), -EBUSY,
		      "a pending successful upload must block a new calibration");
}

/* run_and_teardown() must power the hardware down before releasing the lock, or
 * a queued acquisition samples a still-powered calibrator.
 */
ZTEST(etc_calibration_session, test_run_and_teardown_powers_hw_down)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;

	zassert_ok(etc_calibration_run_and_teardown(), "run failed");

	zassert_false(mock.hw_powered, "calibration hardware must be powered down after teardown");
	zassert_true(mock.exit_calls > 0, "teardown must have run");
	/* The result survives teardown so it can still be uploaded. */
	zassert_equal(etc_calibration_get_calibration_status(), ETC_SENSOR_CALIB_DATA_UPLOAD,
		      "teardown must not clear a result pending upload");
}

/* A low battery must fail the run and still leave the hardware powered down. */
ZTEST(etc_calibration_session, test_run_with_low_battery_fails_and_tears_down)
{
	mock.battery_mv = 100;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0, "run should fail on low battery");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_BATTERY_LOW,
		      "expected BATTERY_LOW result");
	zassert_false(mock.hw_powered, "hardware must be powered down even when the run fails");
}

/* FW-614: four unrelated faults all reported code 3 ("ambient temperature out
 * of range"). Each now gets its own code plus the measurement that produced it.
 */

/* A calibrator ambient outside the window keeps the historical code 3. */
ZTEST(etc_calibration_session, test_calibrator_ambient_out_of_range)
{
	mock.ambient_temp = 35.0f;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0, "run should fail");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE, "expected code 3");
	zassert_not_null(strstr(current_error_detail(), "CAL_AMB"),
			 "detail must name the calibrator ambient check, got '%s'",
			 current_error_detail());
}

/* An unreadable calibrator sensor also fails the range check, but must not be
 * reported as an out-of-range ambient.
 */
ZTEST(etc_calibration_session, test_calibrator_ambient_sensor_failure_is_distinct)
{
	mock.ambient_temp = SENSOR_TEMP_NO_CONNECTED;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0, "run should fail");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_CALIBRATOR_AMBIENT_SENSOR_FAIL,
		      "a dead calibrator sensor must not report AMBIENT_TEMP_OUT_OF_RANGE");
}

/* The device's own ambient is checked separately from the calibrator's. */
ZTEST(etc_calibration_session, test_device_ambient_out_of_range_is_distinct)
{
	mock.ambient_temp = 20.0f;
	mock.device_ambient_temp = 35.0f;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0, "run should fail");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_DEVICE_AMBIENT_OUT_OF_RANGE,
		      "a bad device ambient must not be blamed on the calibrator");
	zassert_not_null(strstr(current_error_detail(), "DEV_AMB"),
			 "detail must name the device ambient check, got '%s'",
			 current_error_detail());
}

/* Not every board carries a working ambient NTC, and calibration never depended
 * on one: the reading is reported for diagnosis and only its range check is
 * skipped.
 */
ZTEST(etc_calibration_session, test_unreadable_device_ambient_does_not_block_calibration)
{
	struct etc_sensor_calibration_status_info info = {0};

	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;
	mock.device_ambient_temp = SENSOR_TEMP_NO_CONNECTED;

	zassert_ok(etc_calibration_run_and_teardown(),
		   "an unreadable device ambient must not fail the calibration");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_SUCCESS,
		      "expected SUCCESS despite the unreadable device ambient");

	/* NaN, not the -273.15 sentinel, which a consumer could plot as real. */
	etc_calibration_get_current_status(&info);
	zassert_true(isnan(info.device_ambient_c),
		     "an unreadable device ambient must be reported as NaN, got %f",
		     (double)info.device_ambient_c);
}

/* Both ambient readings are reported as data, whether or not the run passed. */
ZTEST(etc_calibration_session, test_ambient_temperatures_are_reported)
{
	struct etc_sensor_calibration_status_info info = {0};

	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;
	mock.ambient_temp = 21.5f;
	mock.device_ambient_temp = 23.5f;

	zassert_ok(etc_calibration_run_and_teardown(), "run failed");

	etc_calibration_get_current_status(&info);
	zassert_within(info.calibrator_ambient_c, 21.5f, 0.01f, "calibrator ambient not reported");
	zassert_within(info.device_ambient_c, 23.5f, 0.01f, "device ambient not reported");
}

/* A switch that will not actuate must be reported as such, not as a generic
 * CALIB_FAIL later in the run.
 */
ZTEST(etc_calibration_session, test_switch_failure_is_reported)
{
	mock.gpio_mask_result = -EIO;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0, "run should fail");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_CALIBRATOR_SWITCH_FAIL, "expected a switch fault");
}

/* A dead ADC reads -1 everywhere, which passes the offset window and is only
 * caught at the high switch. Pins that fallback down.
 */
ZTEST(etc_calibration_session, test_dead_adc_still_aborts_the_run)
{
	mock.adc_fails = true;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0,
			  "a failing ADC must not produce a successful calibration");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_CALIBRATOR_HIGH_OUT_OF_RANGE,
		      "a dead ADC should be caught by the high-switch range check");
}

/* Real hardware reads a small negative mean at the offset switch. Those are
 * valid readings inside the {-100, 100} window and must not fail the run.
 */
ZTEST(etc_calibration_session, test_negative_offset_readings_are_valid)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;
	mock.adc_offset_value = -7;

	zassert_ok(etc_calibration_run_and_teardown(),
		   "a small negative offset reading is valid and must not fail the run");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_SUCCESS,
		      "expected SUCCESS for a negative but in-range offset");
}

/* A failed flash store must not be reported as a successful run. */
ZTEST(etc_calibration_session, test_settings_write_failure_is_not_reported_as_success)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;
	mock.write_setting_result = -ENOSPC;

	zassert_not_equal(etc_calibration_run_and_teardown(), 0,
			  "a failed coefficient store must not report success");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_SETTINGS_WRITE_FAIL, "expected a settings write fault");
}

/* Pre-run checks must set a result, or app_module reads a stale one. */
ZTEST(etc_calibration_session, test_missing_calibrator_sets_a_result)
{
	mock.scan_result = 0;

	zassert_equal(etc_calibration_check(), -ENOENT, "expected -ENOENT");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_CALIBRATOR_NOT_FOUND, "expected CALIBRATOR_NOT_FOUND");
}

ZTEST(etc_calibration_session, test_invalid_calibrator_id_sets_a_result)
{
	mock.scan_result = 1;
	/* Below ETC_CALIB_MIN_SN: an unprogrammed or misread calibrator. */
	mock.read_sn_result = 42;

	zassert_not_equal(etc_calibration_check(), 0, "expected the check to fail");
	zassert_equal(etc_calibration_get_calibration_result(),
		      ETC_SENSOR_CALIB_CALIBRATOR_ID_INVALID, "expected CALIBRATOR_ID_INVALID");
}

/* A successful run must leave no stale detail text behind. */
ZTEST(etc_calibration_session, test_success_reports_no_error_detail)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;

	zassert_ok(etc_calibration_run_and_teardown(), "run failed");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_SUCCESS,
		      "expected SUCCESS");
	zassert_equal(strlen(current_error_detail()), 0, "expected empty detail, got '%s'",
		      current_error_detail());
}

/* etc_calibration_run() leaves the session armed (no teardown), so the next run
 * skips etc_calibration_init() and only the per-run reset clears the outcome.
 * Both runs must sit in one test body: before() re-inits between tests and would
 * hide the staleness.
 */
ZTEST(etc_calibration_session, test_run_after_failure_clears_error_detail)
{
	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;

	mock.battery_mv = 100;
	zassert_not_equal(etc_calibration_run(), 0, "run should fail on low battery");
	zassert_not_equal(strlen(current_error_detail()), 0, "the failed run must record detail");

	mock.battery_mv = 4000;
	zassert_ok(etc_calibration_run(), "second run failed");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_SUCCESS,
		      "expected SUCCESS");
	zassert_equal(strlen(current_error_detail()), 0,
		      "a success must not publish the previous failure's detail, got '%s'",
		      current_error_detail());
}

/* Same staleness class: a run that fails before reading the ambients must not
 * republish the previous run's temperatures.
 */
ZTEST(etc_calibration_session, test_failed_run_does_not_report_previous_ambients)
{
	struct etc_sensor_calibration_status_info info = {0};

	mock.scan_result = 1;
	mock.read_sn_result = ETC_CALIB_MIN_SN;
	mock.ambient_temp = 21.5f;
	mock.device_ambient_temp = 23.5f;
	zassert_ok(etc_calibration_run(), "first run failed");

	/* Fails at the battery gate, before either ambient is read. */
	mock.battery_mv = 100;
	zassert_not_equal(etc_calibration_run(), 0, "run should fail on low battery");
	zassert_equal(etc_calibration_get_calibration_result(), ETC_SENSOR_CALIB_BATTERY_LOW,
		      "expected BATTERY_LOW");

	etc_calibration_get_current_status(&info);
	zassert_true(isnan(info.calibrator_ambient_c),
		     "unmeasured calibrator ambient must be NaN, got %f",
		     (double)info.calibrator_ambient_c);
	zassert_true(isnan(info.device_ambient_c), "unmeasured device ambient must be NaN, got %f",
		     (double)info.device_ambient_c);
}

ZTEST_SUITE(etc_calibration_session, NULL, NULL, before, NULL, NULL);
