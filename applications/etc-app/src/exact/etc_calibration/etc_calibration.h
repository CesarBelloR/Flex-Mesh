/*
 * Copyright (c) 2025 EXACT Technology
 */

#ifndef ETC_CALIBRATION_H_
#define ETC_CALIBRATION_H_

#include <stdint.h>

/**
 * @brief Initializes the calibration process.
 *
 * This function sets up any necessary configurations and prepares
 * the system for calibration procedures. It should be called before
 * any calibration activities are run.
 */
void etc_calibration_init(void);

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
 * @brief Cleans up after the calibration process.
 *
 * This function performs the disable the vsense power supply
 */
void etc_calibration_exit(void);

/**
 * @brief Lock the calibration process
 */
void etc_calibration_lock(void);

/**
 * @brief Unlock the calibration process
 */
void etc_calibration_unlock(void);

#endif /* ETC_CALIBRATION_H_ */