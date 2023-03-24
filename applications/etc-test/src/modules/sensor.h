#ifndef SENSOR_H_
#define SENSOR_H_

#include <stdint.h>

enum etc_adc_channel {
        ETC_ADC_CHANNEL_AMB = 0,
        ETC_ADC_CHANNEL_BATTERY,
        ETC_ADC_CHANNEL_SENSOR,
        ETC_ADC_CHANNEL_HW_VER,
        ETC_ADC_CHANNEL_MAX
};

enum sensor_input {
	SENSOR_INPUT_AMBIENT = 0,
	SENSOR_INPUT_IN1,
	SENSOR_INPUT_IN2,
	SENSOR_INPUT_IN3,
	SENSOR_INPUT_IN4,
	SENSOR_INPUT_MAX
};

int sensor_init(void);

float sensor_ntc_converter(enum etc_adc_channel channel, int val);

int sensor_get_raw_value(enum etc_adc_channel channel);

void sensor_adc_switch_channel(enum sensor_input channel);

#endif /* SENSOR_H_ */
