/*
 * Copyright (c) 2023 EXACT Technology
 */

#ifndef ETC_SENSOR_H_
#define ETC_SENSOR_H_

#include <zephyr/device.h>
#include "events/sensor_event.h"
#include "etc_sensor_helper.h"

#define ETC_CALIB_MAX_SN 99999
#define ETC_CALIB_MIN_SN 10000

enum etc_sensor_status {
	SENSOR_NO_CONNECTION,
	SENSOR_CONNECTED,
	SENSOR_NA,
};

typedef void(*etc_sensor_evt_handler_t)(enum etc_sensor_status status);

static inline bool etc_sensor_rr_value_is_valid(uint16_t rr_value) {
	if ((rr_value >= SENSOR_RR_VALID_MIN_VALUE) && 
	    (rr_value <= SENSOR_RR_VALID_MAX_VALUE)) {
		return true;
	}
	return false;
}

static inline bool etc_sensor_temp_ambient_for_rr_is_valid(float temp) {
	if ((temp >= SENSOR_AMBIENT_C_RR_VALID_MIN_VALUE) && 
	    (temp <= SENSOR_AMBIENT_C_RR_VALID_MAX_VALUE)) {
		return true;
	}
	return false;
}

/**
 * @brief Initialize the sensor system.
 * 
 * This function is responsible for setting up and initializing the sensor system 
 * and setting default values.
 */
void etc_sensor_init(etc_sensor_evt_handler_t handler);

/**
 * @brief Get the ambient temperature.
 * 
 * This function retrieves the temperature of the surrounding environment.
 * 
 * @return the ambient temperature
 */
float etc_sensor_get_ambient_temp(void);

/**
 * @brief Get the probe's temperature based on a specific input.
 * 
 * @param input Specifies which sensor input to read the temperature from.
 * @return the temperature of the specified probe.
 */
float etc_sensor_get_probe_temp(enum sensor_input input);

/**
 * @brief Get the probe's humidity level.
 * 
 * This function retrieves the relative humidity percentage measured by the probe.
 * 
 * @return the humidity level of the probe in percentage.
 */
float etc_sensor_get_probe_humid(void);

/**
 * @brief Get the port probe's humidity index.
 * 
 * This function retrieves an index with the probe's humidity measurement.
 * 
 * @return the humidity index.
 */
int8_t etc_sensor_get_probe_humid_index(void);

/**
 * @brief Get the battery mV.
 * 
 * This function provides information regarding mV of battery.
 * 
 * @return the battery level in mV
 */
uint16_t etc_sensor_get_battery(void);

/**
 * @brief Sample and get the battery voltage in mV.
 *
 * The caller is responsible for ensuring that `etc_calibration_lock()` is called
 * before sampling and retrieving the value. This should only be used from
 * etc_sensor or etc_calibration.
 *
 * @return battery voltage in mV
 */
uint16_t etc_sensor_sample_and_get_battery(void);

/**
 * @brief Run or start a new data acquisition cycle.
 * 
 * This function initiates a new round of data collection from the sensors.
 */
void etc_sensor_run_acquisition(void);

/**
 * @brief Get the type of probe based on a specific input.
 * 
 * @param input Specifies which sensor input to determine the probe type from.
 * @return the type of the specified probe.
 */
enum sensor_type etc_sensor_get_probe_type(enum sensor_input input);

/**
 * @brief Get the current probe sensor status 
 * 
 * @return the status of probed sensor (connected or no connect)
 */
enum etc_sensor_status etc_sensor_get_status(void);

/**
 * @brief Disable the sensor voltage rail, if enabled.
 *
 * @return true if power was disabled, false if there was no action.
 */
bool etc_sensor_disable_power(void);

/** Get the enter functional test status. This will return true if all four
 * sensor ports report a connected analog sensor and a low level on the 1-wire
 * sensor line.
 * 
*/
bool etc_sensor_get_enter_functional_test(void);

/** Enter the functional test. Turn on sensor power supplies. 
 * 
*/
void etc_sensor_enter_functional_test(void);

/** Exit the functional test. Turn off sensor power supplies.
 * 
*/
void etc_sensor_exit_functional_test(void);

/**
 * @brief Enter the calibration mode.
 *
 * This function is responsible for entering calibration mode which involves
 * turning on the sensor power supplies and the one-wire bus.
 */
void etc_sensor_calibration_enter(void);

/**
 * @brief Scans for sensor calibration data.
 *
 * @return int Status code indicating the result of the scan.
 */
int etc_sensor_calibration_scan(void);

/**
 * @brief Writes the sensor calibration code.
 *
 * @param serial_number the input serial number
 * @return int Status code indicating whether the write was successful.
 */
int etc_sensor_calibration_write_sn(uint32_t serial_number);

/**
 * @brief Reads the serial number of calibrator
 *
 * @return int The serial number of calibrator or error code.
 */
int etc_sensor_calibration_read_sn(void);

/**
 * @brief Reads the ADC value during sensor calibration.
 *
 * @return int The ADC value.
 */
int etc_sensor_calibration_read_adc(struct etc_sensor_adc_raw_data *raw_adc);

/**
 * @brief Sets the GPIO mask for sensor calibration.
 *
 * @param mask The GPIO mask to set.
 *
 * @return int Status code indicating whether the operation was successful.
 */
int etc_sensor_calibration_set_gpio_mask(int8_t mask);

/**
 * @brief Reads the temperature from the one-sensor.
 *
 * @return float The temperature as read from the sensor.
 */
float etc_sensor_calibration_read_temperature_from_sensor(void);

/**
 * @brief Converts the raw ADC temperature reading to a calibrated temperature value.
 *
 * @param raw_adc The raw ADC value representing the temperature.
 * @param rr_hw_adc Pointer to the hardware-specific ADC value for reference.
 * @param calibration_info Pointer to current calibration info
 * @return The calibrated temperature as a floating-point value.
 */
float etc_sensor_calibration_convert_temperature(
	int raw_adc, uint16_t *rr_hw_adc, struct etc_sensor_adc_calibration_info *calibration_info);

/**
 * @brief Retrieves the hardware version ADC value.
 *
 * @return The hardware version ADC value as an unsigned 16-bit integer.
 */
uint16_t etc_sensor_calibration_get_hw_version_adc(void);

/**
 * @brief Saves the temperature compensation value.
 *
 * @param hw_raw_adc The raw ADC value representing the hardware's temperature compensation data.
 */
void etc_sensor_calibration_save_temperature_compensation(uint16_t hw_raw_adc);

/**
 * @brief Exit the calibration mode.
 *
 * This function exits the calibration mode by turning off the sensor power
 * supplies and the one-wire bus.
 */
void etc_sensor_calibration_exit(void);
#endif /*  ETC_SENSOR_H_ */
