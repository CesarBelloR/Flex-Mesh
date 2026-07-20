#include <zephyr/kernel.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <date_time.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_calibration, CONFIG_ETC_CALIBRATION_LOG_LEVEL);

#include "events/app_event.h"
#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_sensor_helper.h"
#include "etc_calibration.h"
#include "app_module_helper.h"

// Enumration for switch state list
enum switch_state {
	SW_STATE_ADC_OFFSET,
	SW_STATE_TEMP_0_2,
	SW_STATE_TEMP_25_0,
	SW_STATE_TEMP_44_6,
	SW_STATE_TEMP_70_4,
	SW_STATE_ADC_HIGH
};

// Macro to define a range around a given value.
#define RANGE(value) {(value) - CONFIG_TEMP_TOLERANCE, (value) + CONFIG_TEMP_TOLERANCE}

// Structure representing a temperature range with minimum and maximum values.
struct temperature_range {
	int32_t min;
	int32_t max;
};

// Structure representing an ADC range with minimum and maximum values.
struct adc_range {
	int min;
	int max;
};

// Enumration for temperature calibration list
enum temperature_range_list {
	TEMP_RANGE_AMBIENT,
	TEMP_RANGE_SW2,
	TEMP_RANGE_SW3,
	TEMP_RANGE_SW4,
	TEMP_RANGE_SW5
};

// Constant array of temperature ranges used for calibration testing.
static const struct temperature_range list_temperature[] = {
	{CONFIG_TEMP_RANGE_SPECIFIC_MIN, CONFIG_TEMP_RANGE_SPECIFIC_MAX},
	RANGE(CONFIG_TEMP_RANGE_SWITCH2_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH3_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH4_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH5_REF)};

// Enumration for analog calibration list switch
enum analog_range_list {
	ADC_RANGE_SW1,
	ADC_RANGE_SW6
};

// Constant array of ADC ranges used for calibration testing.
static const struct adc_range list_adc[] = {{-100, 100}, {3914, 4114}};

// Constant value for battery valid to do calibration
static const int batt_valid_mv = 3300;

// Macro to check if a given temperature is within a specified temperature range index in
// list_temperature.
#define IS_TEMP_IN_RANGE(index, temp)                                                              \
	({                                                                                         \
		int32_t temp_cd = (int32_t)((temp) * 100.0f);                                      \
		((temp_cd) >= list_temperature[(index)].min &&                                     \
		 (temp_cd) <= list_temperature[(index)].max);                                      \
	})

// Macro to check if a given ADC value is within a specified ADC range index in list_adc.
#define IS_ADC_IN_RANGE(index, adc) ((adc) >= list_adc[index].min && (adc) <= list_adc[index].max)

// Range bounds as degrees C, for reporting alongside the measured value.
#define TEMP_RANGE_MIN_C(index) (list_temperature[(index)].min / 100.0)
#define TEMP_RANGE_MAX_C(index) (list_temperature[(index)].max / 100.0)

/* Guards the shared analog front-end: VSEN rail, ADC mux, 1-wire bus and the
 * TMP1826 GPIO mask. Held for the whole calibration hardware session, which
 * takes several seconds.
 */
K_MUTEX_DEFINE(etc_calibration_mutex);

/* Guards calibration_status only. Kept separate from the front-end mutex so a
 * status read never queues behind a multi-second hardware session: doing so
 * blocked the data module thread behind the sensor thread and wedged both.
 *
 * Lock ordering: front-end mutex -> status mutex. Never the reverse.
 */
K_MUTEX_DEFINE(calibration_status_mutex);

/* Retry interval for the timeout handler when the front-end mutex is busy. */
#define CALIBRATION_TIMEOUT_RETRY_S 1

// Work delayable to handle timeout
static void etc_calibration_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(etc_calibration_timeout, etc_calibration_timeout_handler);
static struct k_work_sync etc_calibration_timeout_sync;

// Boolean flag indicating whether the calibration process is ready to proceed.
static bool etc_calibration_ready = false;

// Calibrator SN
static uint32_t etc_calibrator_sn = 0;
static struct etc_sensor_calibration_status_info calibration_status = {0};

/* The two ways a session ends. Both clear etc_calibration_ready, which is
 * assigned nowhere else, so the flag always reflects whether a session is in
 * progress. The caller must hold the front-end mutex.
 */
static void calibration_teardown_hw_locked(void)
{
	etc_calibration_ready = false;
	etc_sensor_calibration_exit();
}

/* Leaves the analog rail powered: cutting power after a failed check breaks
 * regular sampling following a magnet swipe. Ownership is still released, or
 * every subsequent acquisition would skip with -EBUSY.
 */
static void calibration_release_hw_locked(void)
{
	etc_calibration_ready = false;
	etc_sensor_calibration_release_hw();
}

/* calibration_status writers already run under the front-end mutex; they take
 * the status mutex as well so readers never observe a torn struct.
 */
static void calibration_status_set(uint8_t status)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	calibration_status.status = status;
	k_mutex_unlock(&calibration_status_mutex);
}

static void calibration_result_set(uint8_t result)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	calibration_status.result = result;
	k_mutex_unlock(&calibration_status_mutex);
}

/* Clears the outcome fields so a run cannot publish the previous run's result,
 * detail text or ambient readings.
 */
static void calibration_result_reset(void)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	calibration_status.result = ETC_SENSOR_CALIB_NO_STATUS;
	calibration_status.error_detail[0] = '\0';
	calibration_status.calibrator_ambient_c = NAN;
	calibration_status.device_ambient_c = NAN;
	k_mutex_unlock(&calibration_status_mutex);
}

/* Records a failure: the result code plus the measurement that produced it.
 * Every failure path goes through here so a code never reaches the cloud
 * without the context needed to act on it. Detail text truncation is accepted
 * silently; the result code remains the authoritative signal.
 */
static void calibration_fail(enum etc_sensor_calibration_result result, const char *fmt, ...)
{
	va_list ap;

	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	calibration_status.result = result;
	va_start(ap, fmt);
	vsnprintf(calibration_status.error_detail, sizeof(calibration_status.error_detail), fmt,
		  ap);
	va_end(ap);
	/* HIL suites anchor on this line; keep the "(result N): detail" shape. */
	LOG_ERR("Calibration failed (result %d): %s", result, calibration_status.error_detail);
	k_mutex_unlock(&calibration_status_mutex);
}

/* Records an ambient reading for upload, normalising the unreadable-probe
 * sentinel (SENSOR_TEMP_NO_CONNECTED, -273.15) to NaN so no consumer can
 * mistake it for a measurement.
 */
static void calibration_ambient_set(float temp_c, bool calibrator)
{
	float reported = sensor_temperature_is_valid(temp_c) ? temp_c : NAN;

	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	if (calibrator) {
		calibration_status.calibrator_ambient_c = reported;
	} else {
		calibration_status.device_ambient_c = reported;
	}
	k_mutex_unlock(&calibration_status_mutex);
}

/* Actuates a calibrator switch, reads the four ports, and range-checks the mean.
 *
 * The mean is returned through @p mean_out, not the return value: a valid SW1
 * offset may legitimately be negative, so it cannot share a channel with the
 * errno.
 *
 * @return 0 on success, or a negative errno. On failure the result code and
 * detail text have already been recorded.
 */
static int calibration_read_switch_adc(enum switch_state sw, enum analog_range_list range,
				       enum etc_sensor_calibration_result out_of_range_result,
				       struct etc_sensor_adc_raw_data *raw_data, int *mean_out)
{
	int rc;
	int mean_adc;

	rc = etc_sensor_calibration_set_gpio_mask(sw);
	if (rc) {
		calibration_fail(ETC_SENSOR_CALIB_CALIBRATOR_SWITCH_FAIL, "SW%d gpio err=%d",
				 sw + 1, rc);
		return rc;
	}

	mean_adc = etc_sensor_calibration_read_adc(raw_data);

	/* No per-port error check: adc_get_channel() returns -1 both for a read
	 * error and as a valid raw sample at the offset switch, so the two cannot
	 * be told apart. A dead ADC reads -1 everywhere, which passes the offset
	 * window but fails the high one, so the run still aborts.
	 */
	if (!IS_ADC_IN_RANGE(range, mean_adc)) {
		calibration_fail(
			out_of_range_result, "SW%d adc=%d range=%d..%d ports=[%d,%d,%d,%d]", sw + 1,
			mean_adc, list_adc[range].min, list_adc[range].max,
			raw_data->port[SENSOR_INPUT_IN1], raw_data->port[SENSOR_INPUT_IN2],
			raw_data->port[SENSOR_INPUT_IN3], raw_data->port[SENSOR_INPUT_IN4]);
		return -EINVAL;
	}

	*mean_out = mean_adc;
	return 0;
}

void etc_calibration_init(void)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	memset(&calibration_status, 0, sizeof(calibration_status));
	calibration_status.status = ETC_SENSOR_CALIB_IDLE;
	calibration_status.result = ETC_SENSOR_CALIB_NO_STATUS;
	/* NaN, not 0: distinguishes "not measured" from a genuine 0 °C reading. */
	calibration_status.calibrator_ambient_c = NAN;
	calibration_status.device_ambient_c = NAN;
	k_mutex_unlock(&calibration_status_mutex);
}

int etc_calibration_check(void)
{
	etc_calibration_lock();
	/* A previous calibration result may still be awaiting its cloud upload
	 * acknowledgement (status left at DATA_UPLOAD). Starting a new run would
	 * re-init and discard it. A pending SUCCESS must be protected: its new
	 * coefficients are already stored in NVS but not yet confirmed uploaded,
	 * and a fresh run could overwrite them before the success is delivered.
	 * Block that case. A pending failure stored nothing, so allow it to be
	 * superseded cleanly (the re-init below moves the status off DATA_UPLOAD,
	 * so the prior failed send's stale ACK is harmlessly ignored). */
	if (etc_calibration_get_calibration_status() == ETC_SENSOR_CALIB_DATA_UPLOAD &&
	    etc_calibration_get_calibration_result() == ETC_SENSOR_CALIB_SUCCESS) {
		etc_calibration_unlock();
		return -EBUSY;
	}
	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}

	etc_sensor_calibration_enter();
	calibration_result_reset();
	int rc = 0;
	rc = etc_sensor_calibration_scan();
	if (rc == 0) {
		calibration_fail(ETC_SENSOR_CALIB_CALIBRATOR_NOT_FOUND,
				 "SCAN no calibrator on IN1..IN4");
		rc = -ENOENT;
		goto done;
	}
	LOG_DBG("Number of sensor %d", rc);
	rc = etc_sensor_calibration_read_sn();
	if (rc < ETC_CALIB_MIN_SN || rc > ETC_CALIB_MAX_SN) {
		/* rc is either a negative errno (EEPROM unreadable or prefix
		 * mismatch) or a serial number outside the valid band. Reporting it
		 * verbatim separates a bus fault from an unprogrammed calibrator.
		 */
		calibration_fail(ETC_SENSOR_CALIB_CALIBRATOR_ID_INVALID, "SN rc=%d range=%d..%d",
				 rc, ETC_CALIB_MIN_SN, ETC_CALIB_MAX_SN);
		if (rc >= 0) {
			rc = -EINVAL;
		}
		goto done;
	}
	LOG_DBG("Code sensor %d", rc);
	etc_calibrator_sn = rc;
	calibration_status_set(ETC_SENSOR_CALIB_PRECALIB_VALUE_CHECK);
	/* TODO: need to know the calibrator code */
	rc = 0;
	k_work_schedule(&etc_calibration_timeout, K_SECONDS(CONFIG_CALIBRATION_TIMEOUT));
done:
	if (rc) {
		calibration_status_set(ETC_SENSOR_CALIB_IDLE);
		calibration_release_hw_locked();
	}
	etc_calibration_unlock();
	return rc;
}

static int etc_calibration_packet_status(struct etc_sensor_adc_calibration_info *calibration_info,
					 uint16_t *hw_raw_adc, char *adj_msg, int adj_size)
{
	int rc = 0;
	int adj_val_len = 0;
	calibration_info->loaded = true;
	struct etc_sensor_adc_raw_data raw_data = {0};

	for (int i = TEMP_RANGE_SW2; i <= TEMP_RANGE_SW5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		rc = etc_sensor_calibration_read_adc(&raw_data);
		for (int j = SENSOR_INPUT_IN1; j <= SENSOR_INPUT_IN4; j++) {
			float temp_value = etc_sensor_calibration_convert_temperature(
				raw_data.port[j], hw_raw_adc, calibration_info);
			adj_val_len += snprintf(adj_msg + adj_val_len, adj_size - adj_val_len,
						"%.1f,", temp_value);
		}
		/* Adjust 1 byte to remove extra , */
		adj_msg[adj_val_len - 1] = '|';
	}
	if (adj_val_len + 1 <= adj_size) {
		/* Adjust 1 byte to remove extra | */
		adj_msg[adj_val_len - 1] = '\0';
	} else {
		return -ENOMEM;
	}

	return 0;
}

static int etc_calibration_packet_no_status(char *adj_msg, int adj_size)
{
	memset(adj_msg, 0, adj_size);
	adj_msg[0] = '\0';
	return 0;
}

static int etc_calibration_packet_ref(char *ref_msg, int ref_size)
{
	int32_t refs[] = {CONFIG_TEMP_RANGE_SWITCH2_REF, CONFIG_TEMP_RANGE_SWITCH3_REF,
			  CONFIG_TEMP_RANGE_SWITCH4_REF, CONFIG_TEMP_RANGE_SWITCH5_REF};
	const int num_refs = sizeof(refs) / sizeof(refs[0]);
	ref_msg[0] = '\0';
	for (int i = 0; i < num_refs; i++) {
		float temp_c = refs[i] / 100.0f;
		char temp_str[8];
		snprintf(temp_str, sizeof(temp_str), "%.1f", temp_c);
		if (i > 0) {
			strncat(ref_msg, "|", ref_size - strlen(ref_msg) - 1);
		}
		strncat(ref_msg, temp_str, ref_size - strlen(ref_msg) - 1);
		if (strlen(ref_msg) >= ref_size - 1) {
			return -ENOMEM;
		}
	}
	return 0;
}

static int etc_calibration_packet_calibrator_sn(char *id_msg, int id_size)
{
	snprintf(id_msg, id_size, "%d", etc_calibrator_sn);
	if (strlen(id_msg) >= id_size - 1) {
		return -ENOMEM;
	}
	return 0;
}

static void etc_calibration_timeout_handler(struct k_work *work)
{
	/* The front-end mutex is taken because this handler runs asynchronously on
	 * the system workqueue and can fire while a calibration run is executing on
	 * the data_module thread; the lock serializes the hardware teardown below
	 * against that run so both never drive the front-end at once.
	 *
	 * It must be K_NO_WAIT, though: the system workqueue also carries
	 * app_event_manager dispatch, so blocking on the mutex here would stall
	 * every module's event delivery for the length of a calibration session. A
	 * run holding the mutex tears the hardware down itself and cancels this
	 * work, so rescheduling and retrying later is always safe.
	 */
	if (k_mutex_lock(&etc_calibration_mutex, K_NO_WAIT) != 0) {
		k_work_reschedule(&etc_calibration_timeout, K_SECONDS(CALIBRATION_TIMEOUT_RETRY_S));
		return;
	}
	/* Local-only: the status goes to IDLE, and the codec uploads a result only
	 * from DATA_UPLOAD. Inherent to this path — the session timed out because
	 * no cloud connection was established — so the code serves the shell log
	 * and app_module's indication, not the portal.
	 */
	calibration_fail(ETC_SENSOR_CALIB_TIMEOUT, "TIMEOUT after %ds", CONFIG_CALIBRATION_TIMEOUT);
	calibration_status_set(ETC_SENSOR_CALIB_IDLE);
	calibration_teardown_hw_locked();
	app_module_notify_calibration_timeout();
	k_mutex_unlock(&etc_calibration_mutex);
}

/* Persists the freshly measured coefficients to NVS.
 *
 * Stops at the first failed write and reports which setting failed: a partial
 * store leaves the device on a mix of old and new coefficients.
 *
 * @return 0 on success, or a negative errno with the failure already recorded.
 */
static int calibration_store_coefficients(struct etc_sensor_adc_calibration_info *info)
{
	const struct {
		uint16_t id;
		const char *name;
		const void *value;
		int size;
	} settings[] = {
		{ETC_CALIBRATION_USER_OFFSET_ID, "offset", &info->offset, sizeof(info->offset)},
		{ETC_CALIBRATION_USER_RAWHIGH_ID, "high", &info->high, sizeof(info->high)},
		{ETC_CALIBRATION_USER_REF_ID, "ref", &info->ref, sizeof(info->ref)},
		{ETC_CALIBRATION_USER_TIME_REF_ID, "time", &info->time, sizeof(info->time)},
		{ETC_CALIBRATOR_USER_ID, "id", info->id, sizeof(info->id)},
	};

	etc_calibration_packet_calibrator_sn(info->id, sizeof(info->id));

	for (int i = 0; i < ARRAY_SIZE(settings); i++) {
		int rc = etc_device_write_setting(settings[i].id, settings[i].value,
						  settings[i].size);
		if (rc) {
			calibration_fail(ETC_SENSOR_CALIB_SETTINGS_WRITE_FAIL,
					 "NVS write '%s' err=%d", settings[i].name, rc);
			return rc;
		}
	}

	return 0;
}

/* Runs the calibration measurement. The caller must hold the front-end mutex
 * and must already have cancelled etc_calibration_timeout.
 */
static int calibration_run_locked(void)
{
	int rc = 0;
	uint16_t hw_raw_adc = 0;
	int64_t time_now;
	struct etc_sensor_adc_calibration_info calibration_info = {
		.high = 0.0, .loaded = true, .offset = 0.0, .ref = 3948.75};
	struct etc_sensor_adc_calibration_info previous_calibration_info = {0};
	struct etc_sensor_adc_raw_data raw_data = {0};
	uint16_t current_bat_mv = 0;
	int mean_adc = 0;

	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}
	/* Also covers the shell `calibration run` path, which skips
	 * etc_calibration_check().
	 */
	calibration_result_reset();

	current_bat_mv = etc_sensor_sample_and_get_battery();
	if (current_bat_mv < batt_valid_mv) {
		rc = -EINVAL;
		calibration_fail(ETC_SENSOR_CALIB_BATTERY_LOW, "BATT mv=%u min=%d", current_bat_mv,
				 batt_valid_mv);
		goto done;
	}

	/* Calibrator ambient, over 1-wire from the calibrator's TMP1826. A failed
	 * read also fails the range check below, so test for it first: a dead
	 * sensor and a warm room call for different responses in the field.
	 */
	float current_temp = etc_sensor_calibration_read_temperature_from_sensor();
	calibration_ambient_set(current_temp, true);
	if (!sensor_temperature_is_valid(current_temp)) {
		rc = -EIO;
		calibration_fail(ETC_SENSOR_CALIB_CALIBRATOR_AMBIENT_SENSOR_FAIL,
				 "CAL_AMB tmp1826 read failed t=%.2f", (double)current_temp);
		goto done;
	}
	if (!IS_TEMP_IN_RANGE(TEMP_RANGE_AMBIENT, current_temp)) {
		rc = -EINVAL;
		calibration_fail(ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE,
				 "CAL_AMB t=%.2f range=%.2f..%.2f", (double)current_temp,
				 TEMP_RANGE_MIN_C(TEMP_RANGE_AMBIENT),
				 TEMP_RANGE_MAX_C(TEMP_RANGE_AMBIENT));
		goto done;
	}

	/* Supplementary diagnostic data, range-checked only when the sensor reads:
	 * not every board carries a working ambient NTC, and an absent one must not
	 * block calibration. The calibrator's TMP1826 above stays the authoritative
	 * environmental gate.
	 */
	float device_temp = etc_sensor_calibration_read_device_ambient();
	calibration_ambient_set(device_temp, false);
	if (!sensor_temperature_is_valid(device_temp)) {
		LOG_WRN("Device ambient unreadable (%.2f); reporting it but skipping its "
			"range check",
			(double)device_temp);
	} else if (!IS_TEMP_IN_RANGE(TEMP_RANGE_AMBIENT, device_temp)) {
		rc = -EINVAL;
		calibration_fail(ETC_SENSOR_CALIB_DEVICE_AMBIENT_OUT_OF_RANGE,
				 "DEV_AMB t=%.2f range=%.2f..%.2f", (double)device_temp,
				 TEMP_RANGE_MIN_C(TEMP_RANGE_AMBIENT),
				 TEMP_RANGE_MAX_C(TEMP_RANGE_AMBIENT));
		goto done;
	}

	rc = calibration_read_switch_adc(SW_STATE_ADC_OFFSET, ADC_RANGE_SW1,
					 ETC_SENSOR_CALIB_CALIBRATOR_OFFSET_OUT_OF_RANGE, &raw_data,
					 &mean_adc);
	if (rc) {
		goto done;
	}
	/* Save in RAM offselt */
	calibration_info.offset = (float)mean_adc;

	rc = calibration_read_switch_adc(SW_STATE_ADC_HIGH, ADC_RANGE_SW6,
					 ETC_SENSOR_CALIB_CALIBRATOR_HIGH_OUT_OF_RANGE, &raw_data,
					 &mean_adc);
	if (rc) {
		goto done;
	}

	/* Save in RAM high */
	calibration_info.high = (float)mean_adc;
	/* Save in RAM ADC version HW */
	hw_raw_adc = etc_sensor_calibration_get_hw_version_adc();
	etc_sensor_calibration_save_temperature_compensation(hw_raw_adc);
	/* Reload the calibration */
	rc = etc_calibration_load_config(&previous_calibration_info, true);
	if (rc) {
		rc = etc_calibration_load_config(&previous_calibration_info, false);
	}

	/* Fill the report buffers under the status lock so a concurrent
	 * etc_calibration_get_current_status() cannot copy a torn struct.
	 */
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	if (rc) {
		etc_calibration_packet_no_status(calibration_status.pre_adjustment,
						 sizeof(calibration_status.pre_adjustment));
	} else {
		/* Get pre-adjustment without calibration */
		rc = etc_calibration_packet_status(
			&previous_calibration_info, &hw_raw_adc,
			calibration_status.pre_adjustment,
			sizeof(calibration_status.pre_adjustment));
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Get post-adjustment with calibration */
	rc = etc_calibration_packet_status(&calibration_info, &hw_raw_adc,
					   calibration_status.post_adjustment,
					   sizeof(calibration_status.post_adjustment));
	__ASSERT_NO_MSG(rc == 0);

	/* Write reference values */
	rc = etc_calibration_packet_ref(calibration_status.reference,
		sizeof(calibration_status.reference));
	__ASSERT_NO_MSG(rc == 0);
	k_mutex_unlock(&calibration_status_mutex);

	/* Get current calibration time to report */
	date_time_now(&time_now);
	calibration_info.time = time_now / 1000;

	calibration_status_set(ETC_SENSOR_CALIB_IN_PROCESS);
	int msg_len = 0;
	for (int i = TEMP_RANGE_SW2; i <= TEMP_RANGE_SW5; i++) {
		rc = etc_sensor_calibration_set_gpio_mask(i);
		if (rc) {
			calibration_fail(ETC_SENSOR_CALIB_CALIBRATOR_SWITCH_FAIL,
					 "SW%d gpio err=%d", i + 1, rc);
			goto done;
		}
		etc_sensor_calibration_read_adc(&raw_data);
		for (int j = SENSOR_INPUT_IN1; j <= SENSOR_INPUT_IN4; j++) {
			/* No per-port error check here either: see
			 * calibration_read_switch_adc().
			 */
			float temp_value = etc_sensor_calibration_convert_temperature(
				raw_data.port[j], &hw_raw_adc, &calibration_info);
			LOG_DBG("[%d] ADC %d - Temp %f", i, raw_data.port[j], temp_value);
			if (!IS_TEMP_IN_RANGE(i, temp_value)) {
				rc = -EINVAL;
				calibration_status_set(ETC_SENSOR_CALIB_POSTCALIB_VALUE_CHECK);
				calibration_fail(ETC_SENSOR_CALIB_CALIB_FAIL,
						 "SW%d IN%d t=%.2f range=%.2f..%.2f adc=%d", i + 1,
						 j + 1, (double)temp_value, TEMP_RANGE_MIN_C(i),
						 TEMP_RANGE_MAX_C(i), raw_data.port[j]);
				goto done;
			}
		}
	}

	/* Success is only reportable once the coefficients are actually persisted. */
	rc = calibration_store_coefficients(&calibration_info);
	if (rc) {
		goto done;
	}
	calibration_result_set(ETC_SENSOR_CALIB_SUCCESS);
done:
	calibration_status_set(ETC_SENSOR_CALIB_DATA_UPLOAD);
	/* Single completion marker for the shell and magnet-swipe paths alike;
	 * HIL tests wait on this instead of a fixed capture window. */
	LOG_INF("Calibration run complete (rc %d)", rc);
	return rc;
}

int etc_calibration_run(void)
{
	int rc;

	/* Cancel the timeout before taking the mutex: the handler wants the same
	 * mutex, so cancelling while holding it could stall this thread until the
	 * handler's next retry.
	 */
	k_work_cancel_delayable_sync(&etc_calibration_timeout, &etc_calibration_timeout_sync);

	etc_calibration_lock();
	rc = calibration_run_locked();
	etc_calibration_unlock();
	return rc;
}

int etc_calibration_run_and_teardown(void)
{
	int rc;

	k_work_cancel_delayable_sync(&etc_calibration_timeout, &etc_calibration_timeout_sync);

	/* Measurement and hardware teardown must be one continuous hold. Dropping
	 * the mutex in between let a queued sensor acquisition run against a
	 * still-powered calibrator.
	 */
	etc_calibration_lock();
	rc = calibration_run_locked();
	calibration_teardown_hw_locked();
	etc_calibration_unlock();
	return rc;
}

void etc_calibration_lock(void)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
}

int etc_calibration_lock_timeout(k_timeout_t timeout)
{
	return k_mutex_lock(&etc_calibration_mutex, timeout);
}

void etc_calibration_unlock(void)
{
	k_mutex_unlock(&etc_calibration_mutex);
}

int etc_calibration_load_config(struct etc_sensor_adc_calibration_info *info, bool user)
{
	__ASSERT_NO_MSG(info != NULL);
	int rc = 0;
	if (user) {
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_OFFSET_ID, &info->offset,
					     sizeof(info->offset));
		if (rc) {
			LOG_ERR("Can't load the user calibration for offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_RAWHIGH_ID, &info->high,
					     sizeof(info->high));
		if (rc) {
			LOG_ERR("Can't load the user calibration for raw high offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_REF_ID, &info->ref,
					     sizeof(info->ref));
		if (rc) {
			LOG_ERR("Can't load the user calibration for reference");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_TIME_REF_ID, &info->time,
					     sizeof(info->time));
		if (rc) {
			LOG_ERR("Can't load the user calibration for date/time");
			rc = 0;
		}
		rc = etc_device_read_setting(ETC_CALIBRATOR_USER_ID, &info->id, sizeof(info->id));
		if (rc) {
			LOG_ERR("Can't load the user calibration for calibrator id");
			memset(info->id, 0, sizeof(info->id));
			rc = 0;
		}
	} else {
		rc = etc_device_read_setting(ETC_CALIBRATION_OFFSET_ID, &info->offset,
					     sizeof(info->offset));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_RAWHIGH_ID, &info->high,
					     sizeof(info->high));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for raw high offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_REF_ID, &info->ref, sizeof(info->ref));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for reference");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATOR_ID, &info->id, sizeof(info->id));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for calibrator id");
			memset(info->id, 0, sizeof(info->id));
			rc = 0;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_TIME_ID, &info->time,
					     sizeof(info->time));
		if (rc) {
			LOG_ERR("No factory calibration for date/time");
			info->time = 0;
			rc = 0;
		}
	}
	return rc;
}

void etc_calibration_get_current_status(struct etc_sensor_calibration_status_info *status)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	memcpy(status, &calibration_status, sizeof(calibration_status));
	k_mutex_unlock(&calibration_status_mutex);
}

int etc_calibration_get_calibration_status(void)
{
	int current_status = 0;
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	current_status = calibration_status.status;
	k_mutex_unlock(&calibration_status_mutex);
	return current_status;
}

int etc_calibration_get_calibration_result(void)
{
	int current_result = 0;
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	current_result = calibration_status.result;
	k_mutex_unlock(&calibration_status_mutex);
	return current_result;
}

void etc_calibration_teardown_hw(void)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	calibration_teardown_hw_locked();
	k_mutex_unlock(&etc_calibration_mutex);
}

void etc_calibration_set_idle(void)
{
	k_mutex_lock(&calibration_status_mutex, K_FOREVER);
	calibration_status.status = ETC_SENSOR_CALIB_IDLE;
	k_mutex_unlock(&calibration_status_mutex);
}

void etc_calibration_exit(void)
{
	/* Power down the calibration hardware and clear the status in one step.
	 * Callers that need the result to survive until its cloud upload is
	 * acknowledged should instead call etc_calibration_teardown_hw() now and
	 * etc_calibration_set_idle() once the upload is confirmed. */
	etc_calibration_teardown_hw();
	etc_calibration_set_idle();
}

#ifdef CONFIG_CALIBRATION_MODULE_SHELL
#include <zephyr/shell/shell.h>

/**
 * @brief Initialize the calibration process.
 *
 * This command initializes the calibration module and provides feedback to the shell.
 */
static int cmd_calibration_init(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Initialized!");
	etc_calibration_init();
	etc_calibration_check();
	return 0;
}

/**
 * @brief Run the calibration process.
 *
 * This command starts executing the calibration processes and provides feedback to the shell.
 */
static int cmd_calibration_run(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Run!");
	/* Mirror the data module: run and power the hardware down in one hold.
	 * Plain etc_calibration_run() would leave the session owning the front-end
	 * with nothing to release it (it cancels the timeout), which would skip
	 * every later sensor acquisition. The status stays at DATA_UPLOAD, so the
	 * FW-611 re-entry guard is still reachable from the shell.
	 */
	etc_calibration_run_and_teardown();
	return 0;
}

/**
 * @brief Test calibration I/O (toggle the LED for calibration 3)
 *
 * Placeholder function for I/O testing during calibration.
 */
static int cmd_calibration_io_test(const struct shell *shell, size_t argc, char **argv)
{
	etc_sensor_calibration_set_gpio_mask(atoi(argv[1]));
	return 0;
}

/**
 * @brief Test calibration ADC.
 *
 * This command sets a GPIO mask and reads the temperature from the ADC.
 */
static int cmd_calibration_adc_test(const struct shell *shell, size_t argc, char **argv)
{
	struct etc_sensor_adc_raw_data raw_data = {0};
	for (int i = 0; i <= 5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		k_msleep(10);
		etc_sensor_calibration_read_adc(&raw_data);
		LOG_DBG("%d %d %d %d", raw_data.port[0], raw_data.port[1], raw_data.port[2], raw_data.port[3]);
	}
	return 0;
}

/**
 * @brief Read temperature from sensor via OneWire interface.
 *
 * This command retrieves the temperature from the sensor using a OneWire protocol.
 */
static int cmd_calibration_sensor_onewire(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Temperature sensor one-wire %f",
		    etc_sensor_calibration_read_temperature_from_sensor());
	return 0;
}

static int cmd_calibration_sensor_dump_status(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Pre %s", calibration_status.pre_adjustment);
	shell_print(shell, "Post %s", calibration_status.post_adjustment);
	shell_print(shell, " Ref %s", calibration_status.reference);
	return 0;
}

/**
 * @brief Abort/clear a pending calibration.
 *
 * A calibration result may be left awaiting its cloud upload (status at
 * DATA_UPLOAD); the FW-611 re-entry guard then blocks a fresh run. Power the
 * front-end down, release its ownership and reset the status/result so a new
 * calibration can start immediately.
 */
static int cmd_calibration_abort(const struct shell *shell, size_t argc, char **argv)
{
	etc_calibration_exit();
	etc_calibration_init();
	shell_print(shell, "Calibration aborted");
	return 0;
}

/* Define shell commands and their descriptions */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_calibration,
	SHELL_CMD(init, NULL, "Initialize the calibration module.", cmd_calibration_init),
	SHELL_CMD(run, NULL, "Execute the calibration sequence.", cmd_calibration_run),
	SHELL_CMD(io, NULL, "Perform a calibration I/O test.", cmd_calibration_io_test),
	SHELL_CMD(adc, NULL, "Test calibration using ADC readings. Usage: adc <gpio_mask>",
		  cmd_calibration_adc_test),
	SHELL_CMD(report, NULL, "Test calibration using ADC readings. Usage: adc <gpio_mask>",
		  cmd_calibration_sensor_dump_status),
	SHELL_CMD(sensor, NULL, "Read temperature from the sensor using OneWire.",
		  cmd_calibration_sensor_onewire),
	SHELL_CMD(abort, NULL, "Abort/clear a pending calibration.", cmd_calibration_abort),
	SHELL_SUBCMD_SET_END);

/* Register the shell command set */
SHELL_CMD_REGISTER(calibration, &sub_calibration, "Command set for running calibration tasks.",
		   NULL);

#endif