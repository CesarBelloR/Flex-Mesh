/*
 * Copyright (c) 2025 EXACT Technology
 */

#ifndef ETC_CALIBRATION_H_
#define ETC_CALIBRATION_H_

#include <stdint.h>
#include <time.h>

#define ETC_CALIBRATOR_ID_MAX_SIZE	    (8)
#define ETC_CALIBRATION_ADJUSTMENT_MAX_SIZE (128)
#define ETC_CALIBRATION_REF_MAX_SIZE	    (32)
/* Kept small: object 48939 already uploads two 128-byte adjustment strings plus
 * a 32-byte reference string, and the practical Coiote string budget once SenML
 * and CoAP overhead are accounted for is well under 1 kB.
 */
#define ETC_CALIBRATION_ERROR_DETAIL_MAX_SIZE (96)

/**
 * @brief Structure to hold ADC calibration information for ETC sensor
 */
struct etc_sensor_adc_calibration_info {
	float offset;
	float high;
	float ref;
	bool loaded;
	time_t time;
	char id[ETC_CALIBRATOR_ID_MAX_SIZE];
};

/**
 * @brief Structure to hold ADC calibration status
 */
struct etc_sensor_calibration_status_info {
	uint8_t status;
	uint8_t result;
	char pre_adjustment[ETC_CALIBRATION_ADJUSTMENT_MAX_SIZE];
	char post_adjustment[ETC_CALIBRATION_ADJUSTMENT_MAX_SIZE];
	char reference[ETC_CALIBRATION_REF_MAX_SIZE];
	uint32_t activation;
	/* Free-form troubleshooting text for the failure that produced @ref result:
	 * the measured value and the range it violated. Empty on success.
	 */
	char error_detail[ETC_CALIBRATION_ERROR_DETAIL_MAX_SIZE];
	/* Ambient from the calibrator's TMP1826 and from the device's own onboard
	 * sensor. NaN when unread or not yet measured — never the
	 * SENSOR_TEMP_NO_CONNECTED sentinel. Test with isnan(), not equality.
	 */
	float calibrator_ambient_c;
	float device_ambient_c;
};

/**
 * @brief Structure to hold ADC raw data
 */
struct etc_sensor_adc_raw_data {
	int port[SENSOR_INPUT_IN4 + 1];
};

/**
 * @brief Enumration for calibration current status
 **/
enum etc_sensor_calibration_current_status {
	ETC_SENSOR_CALIB_IDLE,
	ETC_SENSOR_CALIB_PRECALIB_VALUE_CHECK,
	ETC_SENSOR_CALIB_IN_PROCESS,
	ETC_SENSOR_CALIB_POSTCALIB_VALUE_CHECK,
	ETC_SENSOR_CALIB_DATA_UPLOAD
};

/**
 * @brief Enumration for calibration result
 *
 * Uploaded verbatim as LwM2M resource 48939/0/2, so values are part of the
 * cloud-facing contract: only ever append, never renumber or repurpose.
 * app_module.c gates its error indication on
 * `result >= ETC_SENSOR_CALIB_POST_ADJ_OUT_OF_RANGE`, so anything added must
 * sort above ordinal 2 to be reported at all.
 *
 * Keep exact-calibration-status_48939.xml (the Coiote-facing decode table) and
 * the mirror in tests/hil/test_calibration_bugfixes.py in step with this list.
 **/
enum etc_sensor_calibration_result {
	ETC_SENSOR_CALIB_NO_STATUS,
	ETC_SENSOR_CALIB_SUCCESS,
	/* Reserved, never assigned; holds ordinal 2 for the gate described above. */
	ETC_SENSOR_CALIB_POST_ADJ_OUT_OF_RANGE,
	/* Calibrator ambient read fine but outside the acceptable window. */
	ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE,
	ETC_SENSOR_CALIB_CALIB_FAIL,
	ETC_SENSOR_CALIB_BATTERY_LOW,
	/* Calibrator TMP1826 could not be read at all. */
	ETC_SENSOR_CALIB_CALIBRATOR_AMBIENT_SENSOR_FAIL,
	/* Device's own onboard ambient, checked only when the sensor reads: not
	 * every board carries a working NTC, and an absent one is reported as
	 * SENSOR_TEMP_NO_CONNECTED in 48939/0/9 rather than failing the run.
	 */
	ETC_SENSOR_CALIB_DEVICE_AMBIENT_OUT_OF_RANGE,
	/* Calibrator reference network: zero/offset (SW1) and high (SW6). */
	ETC_SENSOR_CALIB_CALIBRATOR_OFFSET_OUT_OF_RANGE,
	ETC_SENSOR_CALIB_CALIBRATOR_HIGH_OUT_OF_RANGE,
	/* Calibrator switch would not actuate (TMP1826 GPIO write failed). There
	 * is no separate ADC-read-error code: adc_get_channel() returns -1 both
	 * for a failure and as a valid sample, so the two cannot be told apart.
	 */
	ETC_SENSOR_CALIB_CALIBRATOR_SWITCH_FAIL,
	/* Pre-run checks: no calibrator on the bus, or one whose EEPROM could not
	 * be read / carries no valid serial number.
	 */
	ETC_SENSOR_CALIB_CALIBRATOR_NOT_FOUND,
	ETC_SENSOR_CALIB_CALIBRATOR_ID_INVALID,
	/* New coefficients could not be persisted to NVS. */
	ETC_SENSOR_CALIB_SETTINGS_WRITE_FAIL,
	/* Session timed out before the result could be uploaded. */
	ETC_SENSOR_CALIB_TIMEOUT,
};

/**
 * @brief Initializes the calibration process.
 *
 * This function sets up any necessary configurations and prepares
 * the system for calibration procedures. It should be called before
 * any calibration activities are run.
 */
void etc_calibration_init(void);

/**
 * @brief Exit the calibration to reset the status
 *
 * Convenience wrapper that powers down the calibration hardware and resets the
 * status to IDLE. Equivalent to @ref etc_calibration_teardown_hw followed by
 * @ref etc_calibration_set_idle.
 */
void etc_calibration_exit(void);

/**
 * @brief Power down the calibration hardware without clearing the status.
 *
 * Use after a calibration run when the result must remain re-encodable (status
 * left at DATA_UPLOAD) until its cloud upload is acknowledged. Pair with
 * @ref etc_calibration_set_idle once delivery is confirmed.
 */
void etc_calibration_teardown_hw(void);

/**
 * @brief Reset the calibration status to IDLE.
 *
 * Call once a pending calibration result has been delivered (its data send was
 * acknowledged) to close the calibration session.
 */
void etc_calibration_set_idle(void);

/**
 * @brief Check the calibration condition
 *
 * This function will check the system is ready for calibration process by calibrator or not.
 *
 * @return 0 if ready.
 */
int etc_calibration_check(void);

/**
 * @brief Executes the calibration process.
 *
 * This function runs the main calibration routine, which involves
 * all steps needed to calibrate the sensor or system.
 *
 * @return int Status code indicating the result of the calibration run.
 * A return value of 0 typically indicates success, while non-zero values
 * indicate specific error conditions.
 */
int etc_calibration_run(void);

/**
 * @brief Run the calibration and power the hardware down in one atomic step.
 *
 * Equivalent to etc_calibration_run() immediately followed by
 * etc_calibration_teardown_hw(), except that the front-end lock is held across
 * both. Callers that need the result to survive until its cloud upload is
 * acknowledged should use this and call etc_calibration_set_idle() on ACK.
 *
 * Prefer this over the run/teardown pair: releasing the lock in between lets a
 * waiting sensor acquisition sample a still-powered calibrator, which both
 * corrupts the reading and can wedge the 1-wire bus.
 *
 * @return int Status code indicating the result of the calibration run.
 * A return value of 0 typically indicates success.
 */
int etc_calibration_run_and_teardown(void);

/**
 * @brief Lock the calibration process
 *
 * Guards the shared analog front-end (VSEN rail, ADC mux, 1-wire bus, TMP1826
 * GPIO mask). Held for a whole calibration hardware session, which takes
 * several seconds.
 *
 * Lock ordering: this lock is taken before the internal status lock, never
 * after. Status accessors such as etc_calibration_get_calibration_status() take
 * only the status lock and are therefore safe to call while this is held by
 * another thread.
 */
void etc_calibration_lock(void);

/**
 * @brief Lock the calibration process, giving up after @p timeout.
 *
 * Use from threads that must not park indefinitely behind a calibration
 * session or a wedged bus.
 *
 * @param timeout How long to wait for the lock.
 * @return 0 when the lock was taken, -EAGAIN on timeout.
 */
int etc_calibration_lock_timeout(k_timeout_t timeout);

/**
 * @brief Unlock the calibration process
 */
void etc_calibration_unlock(void);

/**
 * @brief Loads the calibration configuration for the ETC sensor ADC.
 *
 * @param[out] info Pointer to the calibration info structure to be filled with configuration data
 * @param[in] user Boolean indicating whether to load user configuration (true) or default (false)
 * @return int Returns 0 on success, negative error code on failure
 */
int etc_calibration_load_config(struct etc_sensor_adc_calibration_info *info, bool user);

/**
 * @brief Gets the current calibration status of the ETC sensor.
 *
 * Retrieves the current calibration status information for the ETC sensor.
 *
 * @param[out] status Pointer to store etc_sensor_calibration_status_info structure containing
 * current status
 */
void etc_calibration_get_current_status(struct etc_sensor_calibration_status_info *status);

/**
 * @brief Gets the current calibration status @ref enum etc_sensor_calibration_current_status
 */
int etc_calibration_get_calibration_status(void);

/**
 * @brief Gets the current calibration result @ref enum etc_sensor_calibration_result
 */
int etc_calibration_get_calibration_result(void);
#endif /* ETC_CALIBRATION_H_ */