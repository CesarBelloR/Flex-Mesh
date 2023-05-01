
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <math.h>
#include "adc.h"
#include "sensor.h"
#define MODULE sensor_module

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, LOG_LEVEL_INF);

/* Sensor Analog constant information */
#define SENSOR_NTC_NOMINAL_RESISTANCE (float)DT_PROP(DT_PATH(ntc), norminal_25c_ohms)
#define SENSOR_NTC_NOMINAL_TEMP 25.0
#define SENSOR_NTC_BETA (float)DT_PROP(DT_PATH(ntc), b_value_k)
#define SENSOR_NTC_RESISTOR_REF (float)DT_PROP(DT_PATH(ntc), reference_res_ohms)
#define SENSOR_NTC_REFERENCE_VOLTAGE (float)(DT_PROP(DT_PATH(ntc), reference_voltage_mv) / 1000.0f)

static const struct gpio_dt_spec sense_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
static const struct gpio_dt_spec s0_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);

void sensor_adc_switch_channel(enum sensor_input channel) 
{
	gpio_pin_set_dt(&sense_dt, 0U);
	gpio_pin_set_dt(&s0_dt, channel & 0x01);
	gpio_pin_set_dt(&s1_dt, (channel >> 1) & 0x01);
}

static void sensor_adc_hw_init(void) {
	if (!device_is_ready(sense_dt.port)) {
		return;
	}
	if (!device_is_ready(s0_dt.port)) {
		return;
	}
	if (!device_is_ready(s1_dt.port)) {
		return;
	}
	gpio_pin_configure_dt(&sense_dt, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
}

int sensor_init(void)
{
	adc_init();
	sensor_adc_hw_init();
	return 0;
}

float sensor_ntc_converter(enum etc_adc_channel channel, int val) 
{
        float full_scale_v = adc_get_full_scale_voltage_mv(channel) / 1000.0f;
        int full_scale_count = adc_get_full_scale_count(channel);
	float raw_data = ((float)(val) * full_scale_v / SENSOR_NTC_REFERENCE_VOLTAGE);
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

int sensor_get_raw_value(enum etc_adc_channel channel)
{
        return adc_get_channel(channel);
}