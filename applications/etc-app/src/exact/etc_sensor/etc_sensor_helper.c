/*
 * Copyright (c) 2025 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include "adc.h"
#include "etc_sensor.h"
#include "etc_sensor_helper.h"
#include "etc_settings.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_sensor_helpers, CONFIG_ETC_SENSOR_LOG_LEVEL);

/* Sensor Analog constant information */
#define SENSOR_NTC_NOMINAL_RESISTANCE (float)DT_PROP(DT_PATH(ntc), norminal_25c_ohms)

#if defined(CONFIG_ETC_NTC_TABLE)
#include "etc_ntc_table.h"
#else
#define SENSOR_NTC_NOMINAL_TEMP 25.0
#define SENSOR_NTC_BETA		(float)DT_PROP(DT_PATH(ntc), b_value_k)
#define SENSOR_NTC_RESISTOR_REF (float)DT_PROP(DT_PATH(ntc), reference_res_ohms)
#define SENSOR_NTC_REFERENCE_VOLTAGE                                                               \
	((float)(DT_PROP(DT_PATH(ntc), reference_voltage_mv)) / 1000.0f)
#endif

uint16_t etc_sensor_helper_temperature_compensation(uint16_t raw_adc, uint16_t *hw_raw_adc)
{
	uint16_t rr_value = etc_get_rr_value();
	uint16_t compensated_adc = raw_adc;
	if (!etc_sensor_rr_value_is_valid(rr_value)) {
		LOG_DBG("No compensation applied. raw_adc: %u", compensated_adc);
		return compensated_adc;
	}

	if (etc_sensor_rr_value_is_valid(*hw_raw_adc)) {
		compensated_adc = raw_adc + 0.6 * (rr_value - *hw_raw_adc) * raw_adc / *hw_raw_adc;
		LOG_DBG("Applying compensation. raw: %u, Rr: %u, R: %u, result: %u", raw_adc,
			rr_value, *hw_raw_adc, compensated_adc);
	}
	return compensated_adc;
}

int etc_sensor_helper_get_calibrated_adc(int raw_adc, uint16_t *hw_raw_adc,
					 struct etc_sensor_adc_calibration_info *calibration_info)
{
	int calibrated_adc = raw_adc;

	if (calibration_info->loaded) {
		calibrated_adc = (int)(((float)(raw_adc)-calibration_info->offset) /
				       (calibration_info->high - calibration_info->offset) *
				       calibration_info->ref);
	}
	calibrated_adc = etc_sensor_helper_temperature_compensation(calibrated_adc, hw_raw_adc);

	return calibrated_adc;
}

#if defined(CONFIG_ETC_NTC_TABLE)
static float etc_sensor_ntc_converter(const float table[], int table_length, int offset,
				      int raw_adc, int max_adc)
{
	float input = ((float)max_adc / (float)raw_adc) - 1;
	input = (float)SENSOR_NTC_NOMINAL_RESISTANCE / input;
	input = input / 1000.0;
	float temp_value = SENSOR_TEMP_NO_CONNECTED;
	float tmp;
	for (int i = 0; i < table_length - 1; i++) {
		if (input <= table[i] && input >= table[i + 1]) {
			tmp = ((-40 + (i * offset)) - (-40 + ((i + 1) * offset))) /
			      (table[i] - table[i + 1]);
			tmp = tmp * (input - table[i]);
			tmp = tmp + (-40 + (i * offset));
			temp_value = tmp;
			return temp_value;
		}
	}
	return temp_value;
}
#else
static float etc_sensor_ntc_converter(int data, float full_scale_v, int full_scale_count)
{
	float raw_data = ((float)(data)*full_scale_v / SENSOR_NTC_REFERENCE_VOLTAGE);
	float tmp_value = (float)full_scale_count / (float)raw_data - 1.0;
	tmp_value = SENSOR_NTC_RESISTOR_REF / tmp_value;
	tmp_value = tmp_value / SENSOR_NTC_NOMINAL_RESISTANCE;
	tmp_value = logf(tmp_value);
	tmp_value = tmp_value / SENSOR_NTC_BETA;
	tmp_value += 1.0 / (SENSOR_NTC_NOMINAL_TEMP + 273.15);
	tmp_value = 1.0 / tmp_value;
	tmp_value -= 273.15;
	return tmp_value;
}
#endif

float etc_sensor_helper_ntc_get(int raw_adc, int channel)
{
	float temp = 0.0;
#if defined(CONFIG_ETC_NTC_TABLE)
	temp = etc_sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset,
					raw_adc, adc_get_full_scale_count(channel));
#else
	temp = etc_sensor_ntc_converter(raw_adc,
					(float)adc_get_full_scale_voltage_mv(channel) / 1000.0f,
					adc_get_full_scale_count(channel));
#endif
	return temp;
}
