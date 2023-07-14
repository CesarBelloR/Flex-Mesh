
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
#if defined(CONFIG_NTC_USE_TABLE)
#if IS_ENABLED(CONFIG_NTC_USE_OFFSET_1)
const float table_ntc_resistance_temp[] = {
	195.6520,
	184.9171,
	174.8452,
	165.3910,
	156.5125,
	148.1710,
	140.3304,
	132.9576,
	126.0215,
	119.4936,
	113.3471,
	107.5649,
	102.1155,
	96.9776,
	92.1315,
	87.5588,
	83.2424,
	79.1663,
	75.3157,
	71.6768,
	68.2367,
	64.9907,
	61.9190,
	59.0113,
	56.2579,
	53.6496,
	51.1779,
	48.8349,
	46.6132,
	44.5058,
	42.5062,
	40.5997,
	38.7905,
	37.0729,
	35.4417,
	33.8922,
	32.4197,
	31.0200,
	29.6890,
	28.4231,
	27.2186,
	26.0760,
	24.9877,
	23.9509,
	22.9629,
	22.0211,
	21.1230,
	20.2666,
	19.4495,
	18.6698,
	17.9255,
	17.2139,
	16.5344,
	15.8856,
	15.2658,
	14.6735,
	14.1075,
	13.5664,
	13.0489,
	12.5540,
	12.0805,
	11.6281,
	11.1947,
	10.7795,
	10.3815,
	10.0000,
	9.6342,
	9.2835,
	8.9470,
	8.6242,
	8.3145,
	8.0181,
	7.7337,
	7.4609,
	7.1991,
	6.9479,
	6.7067,
	6.4751,
	6.2526,
	6.0390,
	5.8336,
	5.6357,
	5.4454,
	5.2623,
	5.0863,
	4.9169,
	4.7539,
	4.5971,
	4.4461,
	4.3008,
	4.1609,
	4.0262,
	3.8964,
	3.7714,
	3.6510,
	3.5350,
	3.4231,
	3.3152,
	3.2113,
	3.1110,
	3.0143,
	2.9224,
	2.8337,
	2.7482,
	2.6657,
	2.5861,
	2.5093,
	2.4351,
	2.3635,
	2.2943,
	2.2275,
	2.1627,
	2.1001,
	2.0396,
	1.9811,
	1.9245,
	1.8698,
	1.8170,
	1.7658,
	1.7164,
	1.6685,
	1.6224,
	1.5777,
	1.5345,
	1.4927,
	1.4521,
	1.4129,
	1.3749,
	1.3381,
	1.3025,
	1.2680,
	1.2343,
	1.2016,
	1.1700,
	1.1393,
	1.1096,
	1.0807,
	1.0528,
	1.0256,
	0.9993,
	0.9738,
	0.9492,
	0.9254,
	0.9022,
	0.8798,
	0.8580,
	0.8368,
	0.8162,
	0.7963,
	0.7769,
	0.7580,
	0.7397,
	0.7219,
	0.7046,
	0.6878,
	0.6715,
	0.6556,
	0.6402,
	0.6252,
	0.6106,
	0.5964,
	0.5826,
	0.5692,
	0.5562,
	0.5435,
	0.5311,
};
const int table_offset = 1;
const int table_length = sizeof(table_ntc_resistance_temp) / sizeof(float);
#else
const float table_ntc_resistance_temp[] = {
  195.652,
  148.171,
  113.347,
  87.559,
  68.237,
  53.650,
  42.506,
  33.892,
  27.219,
  22.021,
  17.926,
  14.674,
  12.081,
  10.000,
  8.315,
  6.948,
  5.834,
  4.917,
  4.161,
  3.535,
  3.014,
  2.586,
  2.228,
  1.925,
  1.669,
  1.452,
  1.268,
  1.110,
  0.974,
  0.858,
  0.758,
  0.672,
  0.596,
  0.531,
};
const int table_offset = 1;
const int table_length = sizeof(table_ntc_resistance_temp) / sizeof(float);
#endif /* #if IS_ENABLED(CONFIG_NTC_USE_OFFSET_1) */
#else
#define SENSOR_NTC_NOMINAL_TEMP 25.0
#define SENSOR_NTC_BETA (float)DT_PROP(DT_PATH(ntc), b_value_k)
#define SENSOR_NTC_RESISTOR_REF (float)DT_PROP(DT_PATH(ntc), reference_res_ohms)
#define SENSOR_NTC_REFERENCE_VOLTAGE (float)(DT_PROP(DT_PATH(ntc), reference_voltage_mv) / 1000.0f)
#endif /* #if defined(CONFIG_NTC_USE_TABLE) */

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

#if defined(CONFIG_NTC_USE_TABLE)
float sensor_ntc_converter(const float table[], int table_length, int offset , int raw_adc) {
  float input = (4095.0 / (float)raw_adc) - 1;
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
#endif 

int sensor_get_raw_value(enum etc_adc_channel channel)
{
        return adc_get_channel(channel);
}