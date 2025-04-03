/*
 * Copyright (c) 2025 EXACT Technology
 */

#ifndef ETC_SENSOR_HELPER_H_
#define ETC_SENSOR_HELPER_H_

#include <stdbool.h>
#include "etc_calibration.h"

/**
 * @brief Applies temperature compensation to raw ADC value
 *
 * @param raw_adc The raw ADC value to compensate
 * @param hw_raw_adc Pointer to store hardware raw ADC value
 * @return uint16_t Temperature compensated ADC value
 */
uint16_t etc_sensor_temperature_compensation(uint16_t raw_adc, uint16_t *hw_raw_adc);

/**
 * @brief Gets calibrated ADC value using calibration information
 *
 * @param raw_adc Raw ADC value to calibrate
 * @param hw_raw_adc Pointer to store hardware raw ADC value
 * @param calibration_info Pointer to calibration information structure
 * @return int Calibrated ADC value
 */
int etc_sensor_helper_get_calibrated_adc(int raw_adc, uint16_t *hw_raw_adc,
					 struct etc_sensor_adc_calibration_info *calibration_info);

/**
 * @brief Calculates NTC temperature from raw ADC value
 *
 * @param raw_adc Raw ADC value from NTC sensor
 * @param channel Channel number of the NTC sensor
 * @return float Calculated temperature value
 */
float etc_sensor_helper_ntc_get(int raw_adc, int channel);
#endif /* ETC_SENSOR_HELPER_H_ */