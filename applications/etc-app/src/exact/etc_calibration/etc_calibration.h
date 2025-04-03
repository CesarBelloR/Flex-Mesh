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
 **/
enum etc_sensor_calibration_result {
	ETC_SENSOR_CALIB_NO_STATUS,
	ETC_SENSOR_CALIB_SUCCESS,
	ETC_SENSOR_CALIB_POST_ADJ_OUT_OF_RANGE,
	ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE,
	ETC_SENSOR_CALIB_CALIB_FAIL,
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
 */
void etc_calibration_exit(void);

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
 * @brief Lock the calibration process
 */
void etc_calibration_lock(void);

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