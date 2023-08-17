#include <zephyr/kernel.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include "events/sensor_event.h"
#include "common.h"
#include "etc_sensor.h"
#include "adc.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_sensor, CONFIG_ETC_SENSOR_LOG_LEVEL);

/* Sensor Analog constant information */
#define SENSOR_NTC_NOMINAL_RESISTANCE (float)DT_PROP(DT_PATH(ntc), norminal_25c_ohms)

#if defined(CONFIG_ETC_NTC_TABLE)
#include "etc_ntc_table.h"
#else
#define SENSOR_NTC_NOMINAL_TEMP 25.0
#define SENSOR_NTC_BETA (float)DT_PROP(DT_PATH(ntc), b_value_k)
#define SENSOR_NTC_RESISTOR_REF (float)DT_PROP(DT_PATH(ntc), reference_res_ohms)
#define SENSOR_NTC_REFERENCE_VOLTAGE ((float)(DT_PROP(DT_PATH(ntc), reference_voltage_mv)) / 1000.0f)
#endif

#if IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
const struct device *const ambient_i2c_dev = DEVICE_DT_GET_ANY(ti_tmp1075);
#endif

#if defined(CONFIG_ETC_NTC_TABLE)
static float etc_sensor_ntc_converter(const float table[], int table_length, int offset , 
	int raw_adc, int max_adc) {
 	float input = ((float)max_adc / (float)raw_adc) - 1;
 	input = (float) SENSOR_NTC_NOMINAL_RESISTANCE / input;
 	input = input / 1000.0;
 	float temp_value = 0.0;
 	float tmp;
 	for (int i = 0; i < table_length - 1; i++) {
		if (input <= table[i] && input >= table[i + 1]) {
			tmp = ( (-40 + (i * offset)) - (-40 + ((i + 1) * offset)) ) / ( table[i] - table[i + 1] );
			tmp = tmp * (input - table[i]);
			tmp = tmp +  (-40 + (i * offset));
			temp_value = tmp;
		}
	}
  	return (temp_value);
}
#else
static float etc_sensor_ntc_converter(int data, float full_scale_v, int full_scale_count) {
	float raw_data = ((float)(data) * full_scale_v / SENSOR_NTC_REFERENCE_VOLTAGE);
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

void etc_sensor_init(void) {
#if IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	__ASSERT(ambient_i2c_dev != NULL, "Failed to get device binding");
	__ASSERT(device_is_ready(ambient_i2c_dev), "Device %s is not ready", ambient_i2c_dev->name);
#endif
}

float etc_sensor_get_ambient_temp(void) {
#if IS_ENABLED(CONFIG_ETC_AMBIENT_NTC_SENSOR)
#if defined(CONFIG_ETC_NTC_TABLE)
	return etc_sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, 
		adc_get_channel(ETC_ADC_CHANNEL_AMB), adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
#else
	return etc_sensor_ntc_converter(adc_get_channel(ETC_ADC_CHANNEL_AMB),
		(float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_AMB) / 1000.0f,
		adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
#endif
#elif IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	int rc = sensor_sample_fetch(ambient_i2c_dev);
	if (rc) {
		LOG_ERR("Failed to sample the sensor (err %d), rc");
		return SENSOR_NTC_NO_CONNECTED;
	}
	
	struct sensor_value temp_value;
	rc = sensor_channel_get(ambient_i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_value);
	if (rc) {
		LOG_ERR("Faied to sensor_channel_get (err %d)", rc);
		return SENSOR_NTC_NO_CONNECTED;
	}

	return (float)sensor_value_to_double(&temp_value);
#endif
	return SENSOR_NTC_NO_CONNECTED;
}

float etc_sensor_get_probe_temp(void) {
	#if defined(CONFIG_ETC_NTC_TABLE)
			return etc_sensor_ntc_converter(table_ntc_resistance_temp, table_length, 
				table_offset, adc_get_channel(ETC_ADC_CHANNEL_SENSOR), 
				adc_get_full_scale_count(ETC_ADC_CHANNEL_SENSOR));
	#else
			return etc_sensor_ntc_converter(adc_get_channel(ETC_ADC_CHANNEL_SENSOR),
				(float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_SENSOR) / 1000.0f,
				adc_get_full_scale_count(ETC_ADC_CHANNEL_SENSOR));
	#endif
}