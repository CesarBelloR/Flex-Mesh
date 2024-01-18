#include <zephyr/kernel.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include "events/sensor_event.h"
#include "cloud/cloud_codec/data_codec.h"
#include "common.h"
#include "etc_sensor.h"
#include "etc_device.h"
#include "etc_battery.h"
#include "adc.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_sensor, CONFIG_ETC_SENSOR_LOG_LEVEL);

/* Sensor Analog constant information */
#define SENSOR_NTC_NOMINAL_RESISTANCE (float)DT_PROP(DT_PATH(ntc), norminal_25c_ohms)

/* Mutex to lock write/read from tasks */
K_MUTEX_DEFINE(etc_sensor_mtx);

/* Battery constant information */
const uint32_t sFullOhms = DT_PROP(DT_PATH(vbatt), full_ohms);
const uint32_t sOutputOhms = DT_PROP(DT_PATH(vbatt), output_ohms);

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

struct etc_sensor_adc_calibration_info {
	float offset;
	float high;
	float ref;
	bool loaded;
};

// Get any one sht31 in current bus. If NULL, SHT31 is not ready 
const struct device *const sht31_i2c_dev = DEVICE_DT_GET_ANY(sensirion_sht31);
const struct device *ds2484_dev = DEVICE_DT_GET_ANY(exact_ds2484);

#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
static const struct gpio_dt_spec sense_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
static const struct gpio_dt_spec onewire_slpz_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(onewire_slpz), control_gpios, 0);
#endif
static const struct gpio_dt_spec s0_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
static const struct gpio_dt_spec vsen_en_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);

static enum sensor_type list_sensor_type[SENSOR_INPUT_IN4 + 1];
static int list_sensor_raw_adc[SENSOR_INPUT_IN4 + 1];
static float list_sensor_digital_temp[SENSOR_INPUT_IN4 + 1];
static float sensor_digital_humid;
static int8_t sensor_digital_humid_port_index;
static int sensor_ambient_raw_adc = 0;
static int sensor_battery_raw_adc = 0;
static struct etc_sensor_adc_calibration_info etc_sensor_adc_calibration_info= {0x00};
static etc_sensor_evt_handler_t sensor_evt_handler;
static enum etc_sensor_status last_sensor_status = SENSOR_CONNECTED;
/* Flag that signals if current connected sensors' state qualifies for functional
 * test mode. */
static bool enter_functional_test = false;

/* Remap channels according to HW-772, so that PCBA ports match housing port numbering */
inline static int8_t remap_th_channel(int8_t channel)
{
	__ASSERT(channel >= 0 && channel <= 3, "invalid channel number");

#if !defined(CONFIG_BOARD_ETC_0_3_0)
	switch (channel) {
	case 0:
		return 3;
	case 1:
		return 2;
	case 2:
		return 0;
	case 3:
		return 1;
	default:
		return 0;
	}
#else
	switch (channel) {
	case 0:
		return 1;
	case 1:
		return 0;
	case 2:
		return 3;
	case 3:
		return 2;
	default:
		return 0;
	}
#endif
}

static void etc_sensor_adc_switch_channel(int8_t channel) 
{
	channel = remap_th_channel(channel);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_set_dt(&sense_dt, 0U);
#endif
	gpio_pin_set_dt(&s0_dt, channel & 0x01);
	gpio_pin_set_dt(&s1_dt, (channel >> 1) & 0x01);
}

static void etc_sensor_adc_hw_init(void) 
{
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	if (!device_is_ready(sense_dt.port)) {
		return;
	}
#endif
	if (!device_is_ready(s0_dt.port)) {
		return;
	}
	if (!device_is_ready(s1_dt.port)) {
		return;
	}
	if (!device_is_ready(vsen_en_dt.port)) {
		return;
	}

#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	if (!device_is_ready(onewire_slpz_dt.port)) {
		return;
	}
#endif

	gpio_pin_configure_dt(&vsen_en_dt, GPIO_OUTPUT_INACTIVE);

	adc_init();
}

static void etc_sensor_gpios_enable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 1U);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_OUTPUT_INACTIVE);
#endif
	/* Note: pin s0 is configured by watchdog module if s0/wdt are shared */
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
#endif
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
}

static void etc_sensor_gpios_one_wire_enable(void) 
{
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_INACTIVE);
#endif
}

static void etc_sensor_gpios_disable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 0U);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_DISCONNECTED);
#endif
	/* Note: pin s0 is configured by watchdog module if s0/wdt are shared */
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&s0_dt, GPIO_DISCONNECTED);
#else
	gpio_pin_set_dt(&s0_dt, 0);
#endif
	gpio_pin_configure_dt(&s1_dt, GPIO_DISCONNECTED);
}

static void etc_sensor_gpios_one_wire_disable(void) 
{
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_ACTIVE);
#endif
}

static void etc_sensor_run_detection(void) 
{
#if IS_ENABLED(CONFIG_BOARD_ETC_0_3_0)
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		etc_sensor_adc_switch_channel(i);
		k_msleep(50);
		int raw_adc = adc_get_channel(ETC_ADC_CHANNEL_SENSOR);
		if (raw_adc >= SENSOR_ADC_NO_CONNECTED) {
			list_sensor_type[i] = SENSOR_TYPE_UNDEF;
		} else if (raw_adc <= SENSOR_ADC_ONE_WIRE_CONNECTED) {
			list_sensor_type[i] = SENSOR_TYPE_DIGITAL;
		} else {
			list_sensor_type[i] = SENSOR_TYPE_ANALOG;
		}
	}
#endif
}

static void etc_sensor_probe_check(void) 
{
	int no_connected_counter = 0;
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		float temp = etc_sensor_get_probe_temp(i);
		if (!data_codec_compare_temperature_is_valid(temp)) {
			no_connected_counter += 1;
		}
	}

	enum etc_sensor_status sensor_status = SENSOR_NO_CONNECTION;
	if (no_connected_counter != ETC_SENSOR_NUM_PROBE_SENSOR) {
		sensor_status = SENSOR_CONNECTED;
	}

	if (sensor_evt_handler) {
		k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
		if (sensor_status != last_sensor_status) {
			sensor_evt_handler(sensor_status);
			last_sensor_status = sensor_status;
		}
		k_mutex_unlock(&etc_sensor_mtx);
	}
}

static inline int etc_sensor_acquire_digital_sensor(float *humidity_val, int8_t index)
{
	__ASSERT_NO_MSG(humidity_val != NULL);

	if (!device_is_ready(sht31_i2c_dev)) {
		LOG_ERR("SHT31 is not ready in I2C bus");
		return -1;
	}

	/* Reset the bus */
	sensor_attr_set(sht31_i2c_dev, SENSOR_CHAN_ALL, SENSOR_ATTR_CONFIGURATION, NULL);
	struct sensor_value temp, hum;
	int rc = sensor_sample_fetch(sht31_i2c_dev);
	if (rc) {
		LOG_ERR("Failed to fetch sensor SHT31 (err %d)", rc);
		return rc;
	}

	rc = sensor_channel_get(sht31_i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (rc) {
		LOG_ERR("Failed to get temperature sensor SHT31 (err %d)", rc);
		return rc;
	} else {
		list_sensor_digital_temp[index] = (float)sensor_value_to_double(&temp);
	}

	rc = sensor_channel_get(sht31_i2c_dev, SENSOR_CHAN_HUMIDITY, &hum);
	if (rc) {
		LOG_ERR("Failed to get humidity sensor SHT31 (err %d)", rc);
		return rc;
	}
	
	*humidity_val = (float)sensor_value_to_double(&hum);
	
	return 0;
}

extern int ds2484_get_logic_level(const struct device *dev);

static void etc_sensor_run_digital_sample(void) 
{
	int ret;

	etc_sensor_gpios_one_wire_enable();
	sensor_digital_humid = SENSOR_HUMID_NO_CONNECTED;
	sensor_digital_humid_port_index = -1;
	enter_functional_test = true;
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		list_sensor_digital_temp[i] = SENSOR_TEMP_NO_CONNECTED;
		etc_sensor_adc_switch_channel(i);
		if (list_sensor_type[i] == SENSOR_TYPE_DIGITAL) {
			float humidity_val;

			enter_functional_test = false;
			k_msleep(50);

			ret = etc_sensor_acquire_digital_sensor(&humidity_val,
								i);

			/* Only one humidity sensor is supported */
			if (ret == 0 &&
			    sensor_digital_humid == SENSOR_HUMID_NO_CONNECTED) {
				sensor_digital_humid = humidity_val;
				sensor_digital_humid_port_index = i;
			}
		} else if (enter_functional_test) {
			k_msleep(50);

			ret = ds2484_get_logic_level(ds2484_dev);
			LOG_DBG("LL: %d", ret);
			if (ret != 0) {
				enter_functional_test = false;
			}
		}
	}
	etc_sensor_gpios_one_wire_disable();
}

static int etc_sensor_get_calibrated_adc(int raw_adc) 
{
	int calibrated_adc = raw_adc;
	if (etc_sensor_adc_calibration_info.loaded) {
		calibrated_adc = (int)(((float)(raw_adc) - etc_sensor_adc_calibration_info.offset) / 
			(etc_sensor_adc_calibration_info.high - etc_sensor_adc_calibration_info.offset) * 
			etc_sensor_adc_calibration_info.ref);
	}
	return calibrated_adc;
}

static void etc_sensor_run_analog_sample(void) 
{
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		if (list_sensor_type[i] == SENSOR_TYPE_ANALOG) {
			etc_sensor_adc_switch_channel(i);
			k_msleep(50);
			list_sensor_raw_adc[i] = etc_sensor_get_calibrated_adc(adc_get_channel(ETC_ADC_CHANNEL_SENSOR));
		} else {
			list_sensor_raw_adc[i] = -1;
		}
	}
}


#if defined(CONFIG_ETC_NTC_TABLE)
static float etc_sensor_ntc_converter(const float table[], int table_length, int offset , 
	int raw_adc, int max_adc) 
{
 	float input = ((float)max_adc / (float)raw_adc) - 1;
 	input = (float) SENSOR_NTC_NOMINAL_RESISTANCE / input;
 	input = input / 1000.0;
 	float temp_value = SENSOR_TEMP_NO_CONNECTED;
 	float tmp;
 	for (int i = 0; i < table_length - 1; i++) {
		if (input <= table[i] && input >= table[i + 1]) {
			tmp = ( (-40 + (i * offset)) - (-40 + ((i + 1) * offset)) ) / ( table[i] - table[i + 1] );
			tmp = tmp * (input - table[i]);
			tmp = tmp +  (-40 + (i * offset));
			temp_value = tmp;
			return temp_value;
		}
	}
  	return temp_value;
}
#else
static float etc_sensor_ntc_converter(int data, float full_scale_v, int full_scale_count) 
{
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

static void etc_sensor_load_calibration(void) 
{
	int rc = 0;
	etc_sensor_adc_calibration_info.loaded = false;
	rc = etc_device_read_setting(ETC_CALIBRATION_OFFSET_ID, &etc_sensor_adc_calibration_info.offset, sizeof(etc_sensor_adc_calibration_info.offset));
	if (rc) {
		LOG_ERR("Can't load the calibration for offset");
		return;
	}
	rc = etc_device_read_setting(ETC_CALIBRATION_RAWHIGH_ID, &etc_sensor_adc_calibration_info.high, sizeof(etc_sensor_adc_calibration_info.high));
	if (rc) {
		LOG_ERR("Can't load the calibration for raw high offset");
		return;
	} 
	rc = etc_device_read_setting(ETC_CALIBRATION_REF_ID, &etc_sensor_adc_calibration_info.ref, sizeof(etc_sensor_adc_calibration_info.ref));
	if (rc) {
		LOG_ERR("Can't load the calibration for reference");
		return;
	} 
	LOG_INF("Calibration value %f %f %f", etc_sensor_adc_calibration_info.offset, etc_sensor_adc_calibration_info.high, etc_sensor_adc_calibration_info.ref);
	etc_sensor_adc_calibration_info.loaded = true;
}

void etc_sensor_init(etc_sensor_evt_handler_t handler) 
{
#if IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	__ASSERT(ambient_i2c_dev != NULL, "Failed to get device binding");
	__ASSERT(device_is_ready(ambient_i2c_dev), "Device %s is not ready", ambient_i2c_dev->name);
#endif
	etc_sensor_load_calibration();
	etc_sensor_adc_hw_init();
	for (int i = 0; i < SENSOR_INPUT_IN4 + 1; i++) {
#if IS_ENABLED(CONFIG_BOARD_ETC_0_3_0)
		list_sensor_type[i] = SENSOR_TYPE_UNDEF;
#else
		list_sensor_type[i] = SENSOR_TYPE_ANALOG;
#endif
	}

	sensor_evt_handler = handler;
}

float etc_sensor_get_ambient_temp(void) 
{
#if IS_ENABLED(CONFIG_ETC_AMBIENT_NTC_SENSOR)
#if defined(CONFIG_ETC_NTC_TABLE)
	return etc_sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, 
		sensor_ambient_raw_adc, adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
#else
	return etc_sensor_ntc_converter(sensor_ambient_raw_adc,
		(float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_AMB) / 1000.0f,
		adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
#endif
#elif IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	int rc = sensor_sample_fetch(ambient_i2c_dev);
	if (rc) {
		LOG_ERR("Failed to sample the sensor (err %d), rc");
		return SENSOR_TEMP_NO_CONNECTED;
	}
	
	struct sensor_value temp_value;
	rc = sensor_channel_get(ambient_i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_value);
	if (rc) {
		LOG_ERR("Faied to sensor_channel_get (err %d)", rc);
		return SENSOR_TEMP_NO_CONNECTED;
	}

	return (float)sensor_value_to_double(&temp_value);
#endif
	return SENSOR_TEMP_NO_CONNECTED;
}

float etc_sensor_get_probe_temp(enum sensor_input input) 
{
	__ASSERT(input >= 0 && input <= 3, "invalid channel number");
	if (list_sensor_type[input] == SENSOR_TYPE_ANALOG) {
		#if defined(CONFIG_ETC_NTC_TABLE)
				return etc_sensor_ntc_converter(table_ntc_resistance_temp, table_length, 
					table_offset, list_sensor_raw_adc[input], adc_get_full_scale_count(ETC_ADC_CHANNEL_SENSOR));
		#else
				return etc_sensor_ntc_converter(list_sensor_raw_adc[input],
					(float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_SENSOR) / 1000.0f,
					adc_get_full_scale_count(ETC_ADC_CHANNEL_SENSOR));
		#endif
	} else if (list_sensor_type[input] == SENSOR_TYPE_DIGITAL) {
		return list_sensor_digital_temp[input];
	} else {
		/* No action required */
	}
	return SENSOR_TEMP_NO_CONNECTED;
}

float etc_sensor_get_probe_humid(void) 
{
	return sensor_digital_humid;
}

int8_t etc_sensor_get_probe_humid_index(void) 
{
	return sensor_digital_humid_port_index;
}


uint16_t etc_sensor_get_battery(void) 
{
	int raw_battery_adc = sensor_battery_raw_adc;
	adc_get_raw_to_millivolts(ETC_ADC_CHANNEL_BATTERY, &raw_battery_adc);
	int adc_mv_battery = raw_battery_adc * (sFullOhms / sOutputOhms);
	return adc_mv_battery;
}

void etc_sensor_run_acquisition(void) 
{
	sensor_digital_humid = SENSOR_HUMID_NO_CONNECTED;
	/* Enable the GPIOs SEL0/SEL1 */
	etc_sensor_gpios_enable();
	/* Run detection sensor */
	etc_sensor_run_detection();
	/* Run sample for ambient ADC */
	sensor_ambient_raw_adc = etc_sensor_get_calibrated_adc(adc_get_channel(ETC_ADC_CHANNEL_AMB));
	/* Run sample for battery */
	sensor_battery_raw_adc = etc_sensor_get_calibrated_adc(adc_get_channel(ETC_ADC_CHANNEL_BATTERY));
	/* Run sample sensor for all ports - analog part*/
	etc_sensor_run_analog_sample();
	/* Run sample sensor for all ports - digital part */
	etc_sensor_run_digital_sample();
	/* Disable the GPIOs SEL0/SEL1 */
	etc_sensor_gpios_disable();
	/* Sync battery status */
	etc_battery_poll_status();
	/* Check probe connection */
	etc_sensor_probe_check();
}

enum sensor_type etc_sensor_get_probe_type(enum sensor_input input) 
{
	__ASSERT(input >= 0 && input <= 3, "invalid channel number");
	return list_sensor_type[input];
}

enum etc_sensor_status etc_sensor_get_status(void) 
{
	enum etc_sensor_status status;
	k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
	status = last_sensor_status;
	k_mutex_unlock(&etc_sensor_mtx);
	return status;
}

bool etc_sensor_get_enter_functional_test(void)
{
	return enter_functional_test;
}

void etc_sensor_enter_functional_test(void)
{
	etc_sensor_gpios_enable();
}

void etc_sensor_exit_functional_test(void)
{
	etc_sensor_gpios_disable();
}