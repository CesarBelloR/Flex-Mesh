/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

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
	etc_calibration_init();
}

/* The regression test for the observed hang.
 *
 * A single mutex used to guard both the analog front-end and the status struct,
 * so reading the status queued behind a multi-second hardware session. That is
 * what killed the data module thread: it blocked on a 4-byte read behind a
 * sensor acquisition that never finished. Status reads must not touch the
 * front-end lock at all.
 */
ZTEST(etc_calibration_session, test_status_read_does_not_block_on_front_end_lock)
{
	start_holder();

	/* If these queue behind the front-end lock, the k_sem/join below never
	 * runs and the suite times out. Assert on values so a regression reports
	 * the cause rather than just hanging.
	 */
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

/* A check that finds no calibrator must not leave the session armed, and must
 * hand the front-end back so ordinary sampling resumes. The check claims
 * ownership (etc_sensor_calibration_enter) before scanning, so the error path
 * has to release it; leaving it set would skip every later acquisition with
 * -EBUSY. The rail stays powered, though: a sample runs immediately after the
 * swipe and toggling VSENS_EN would corrupt it.
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

/* run_and_teardown() must power the hardware down before releasing the lock.
 * Releasing in between let a queued sensor acquisition sample a still-powered
 * calibrator, which both corrupted the reading and wedged the 1-wire bus.
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

ZTEST_SUITE(etc_calibration_session, NULL, NULL, before, NULL, NULL);
