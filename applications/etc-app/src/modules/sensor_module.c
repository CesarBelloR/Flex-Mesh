#include <zephyr/kernel.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <app_event_manager.h>
#include "common.h"
#include "adc.h"
#include "etc_date_time.h"
#include "etc_settings.h"
#include "etc_device.h"
#include "watchdog_app.h"
#define MODULE sensor_module
#include "cloud/cloud_codec/data_codec.h"
#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"
#include "events/ui_event.h"
#include "events/cloud_event.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, CONFIG_ETC_APP_LOG_LEVEL);

struct sensor_msg_data {
	union {
		struct app_event app;
		struct data_event data;
		struct util_event util;
		struct ui_event ui;
		struct cloud_event cloud;
	} module;
};

/* Sensor module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

static struct k_work_delayable sensor_poll_work;
static struct sensor_data static_sensor_data;

/* Sensor module message queue. */
#define SENSOR_QUEUE_ENTRY_COUNT	10
#define SENSOR_QUEUE_BYTE_ALIGNMENT	4

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
#define SENSOR_NTC_REFERENCE_VOLTAGE ((float)(DT_PROP(DT_PATH(ntc), reference_voltage_mv)) / 1000.0f)

#define SENSOR_BATTERY_MAX_VOLTAGE_MS 40

#define SENSOR_HANDLER_MAX_WAIT_S 10

/* Battery constant information */
const uint32_t sFullOhms = DT_PROP(DT_PATH(vbatt), full_ohms);
const uint32_t sOutputOhms = DT_PROP(DT_PATH(vbatt), output_ohms);


K_MSGQ_DEFINE(msgq_sensor, sizeof(struct sensor_msg_data),
	      SENSOR_QUEUE_ENTRY_COUNT, SENSOR_QUEUE_BYTE_ALIGNMENT);

/* Forward declarations */
static bool sensor_is_processing = false;

static struct module_data self = {
	.name = "sensor",
	.msg_q = &msgq_sensor,
	.supports_shutdown = true,
};

static const struct gpio_dt_spec vsen_en_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
static const struct gpio_dt_spec sense_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
static const struct gpio_dt_spec s0_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);

/* Remap channels according to HW-772, so that PCBA ports match housing port numbering */
inline static int8_t remap_th_channel(int8_t channel)
{
	__ASSERT(channel >= 0 && channel <= 3, "invalid channel number");

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
}

static void sensor_adc_switch_channel(int8_t channel) 
{
	channel = remap_th_channel(channel);
	gpio_pin_set_dt(&sense_dt, 0U);
	gpio_pin_set_dt(&s0_dt, channel & 0x01);
	gpio_pin_set_dt(&s1_dt, (channel >> 1) & 0x01);
}

static void sensor_adc_hw_init(void) 
{
	if (!device_is_ready(sense_dt.port)) {
		return;
	}
	if (!device_is_ready(s0_dt.port)) {
		return;
	}
	if (!device_is_ready(s1_dt.port)) {
		return;
	}
	if (!device_is_ready(vsen_en_dt.port)) {
		return;
	}
	gpio_pin_configure_dt(&vsen_en_dt, GPIO_OUTPUT_INACTIVE);
}

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct sensor_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_app_event(aeh)) {
		struct app_event *event = cast_app_event(aeh);

		msg.module.app = *event;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);

		msg.module.data = *event;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *event = cast_util_event(aeh);

		msg.module.util = *event;
		enqueue_msg = true;
	}

	if (is_ui_event(aeh)) {
		struct ui_event *event = cast_ui_event(aeh);

		msg.module.ui = *event;
		enqueue_msg = true;
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *event = cast_cloud_event(aeh);

		msg.module.cloud = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
		}
	}

	return false;
}

static void sensor_module_send_sensor(struct sensor_data* sensor, bool is_test)
{
	struct sensor_event *sensor_event = new_sensor_event();
	sensor_event->type = is_test ? SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY : 
		SENSOR_EVT_ENVIRONMENTAL_DATA_READY;
	sensor_event->data.sensors = sensor;
	APP_EVENT_SUBMIT(sensor_event);
}

static int setup(void)
{
	adc_init();
	sensor_adc_hw_init();
	return 0;
}

#if defined(CONFIG_NTC_USE_TABLE)
static float sensor_ntc_converter(const float table[], int table_length, int offset , int raw_adc, int max_adc) {
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
static float sensor_ntc_converter(int data, float full_scale_v, int full_scale_count) {
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

static void sensor_gpios_enable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 1U);
	gpio_pin_configure_dt(&sense_dt, GPIO_OUTPUT_INACTIVE);
	/* Note: pin s0 is configured by watchdog module. */
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
}

static void sensor_gpios_disable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 0U);
	gpio_pin_configure_dt(&sense_dt, GPIO_DISCONNECTED);
	/* Note: pin s0 is configured by watchdog module. */
	gpio_pin_configure_dt(&s1_dt, GPIO_DISCONNECTED);
}

static int sensor_poll_handler(bool is_test) {
	if (sensor_is_processing) {
		return 0;
	}
	if (watchdog_sens_sel0_wdt_sem_take(K_SECONDS(SENSOR_HANDLER_MAX_WAIT_S)) != 0) {
		LOG_WRN("Could not take watchdog_sens_sel0 semaphore");
		return -EAGAIN;
	}

	sensor_gpios_enable();
	k_msleep(100);

	sensor_is_processing = true;
	struct sensor_data* data = &static_sensor_data;
	int utc_timestamp = date_time_now_second();
	data->timestamp = utc_timestamp == -1 ? 0 : utc_timestamp;
	data->temperature[SENSOR_INPUT_AMBIENT] = 
	#if defined(CONFIG_NTC_USE_TABLE)
			sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, adc_get_channel(ETC_ADC_CHANNEL_AMB), 
				adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
	#else
			sensor_ntc_converter(adc_get_channel(ETC_ADC_CHANNEL_AMB),
			  (float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_AMB) / 1000.0f,
			  adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
	if (data_codec_compare_temperature_is_valid(data->temperature[SENSOR_INPUT_AMBIENT])) {
		LOG_DBG("Ambient temp %2.2f", data->temperature[SENSOR_INPUT_AMBIENT]);
	}
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		sensor_adc_switch_channel(i);
		k_msleep(50);
		data->temperature[i] = 
	#if defined(CONFIG_NTC_USE_TABLE)
			sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, adc_get_channel(ETC_ADC_CHANNEL_SENSOR), 
				adc_get_full_scale_count(ETC_ADC_CHANNEL_AMB));
	#else
			sensor_ntc_converter(adc_get_channel(ETC_ADC_CHANNEL_SENSOR),
				(float)adc_get_full_scale_voltage_mv(ETC_ADC_CHANNEL_SENSOR) / 1000.0f,
				adc_get_full_scale_count(ETC_ADC_CHANNEL_SENSOR));
		if (data_codec_compare_temperature_is_valid(data->temperature[i])) {
			LOG_DBG("Channel %d temp %f", i, data->temperature[i]);
		} else {
			LOG_DBG("Channel %d isn't available", i);
		}
	}

	int raw_adc_battery = adc_get_channel(ETC_ADC_CHANNEL_BATTERY);
	adc_get_raw_to_millivolts(ETC_ADC_CHANNEL_BATTERY, &raw_adc_battery);
	int adc_mv_battery = raw_adc_battery * (sFullOhms / sOutputOhms);
	data->battery_mV = adc_mv_battery;
	sensor_module_send_sensor(data, is_test);
	sensor_is_processing = false;
	
	sensor_gpios_disable();

	watchdog_sens_sel0_wdt_sem_give();

	return 0;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_INIT)) {
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct sensor_msg_data *msg)
{
}

/* Message handler for all states. */
static void on_all_states(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {
		LOG_INF("APP_EVT_DATA_GET");
		sensor_poll_handler(false);
		return;
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		state_set(STATE_SHUTDOWN);
		SEND_SHUTDOWN_ACK(sensor, SENSOR_EVT_SHUTDOWN_READY, self.id);
		return;
	}

	if (IS_EVENT(msg, ui, UI_EVT_INPUT_DATA_READY)) {
		LOG_INF("UI_EVT_INPUT_DATA_READY");
		/* The UI input (HALL Sensor or Button) is triggered */
		adc_init();
		sensor_poll_handler(false);
		return;
	}

	if (IS_EVENT(msg, ui, UI_EVT_TEST_DATA_READY)) {
		sensor_poll_handler(true);
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		/* In boot-up, device connected to cloud, start a sensor poll to get data */
		static bool is_send = false;
		if (etc_device_is_logger_lora() == false) {
			if (!is_send) {
				LOG_DBG("Device is online. Collecting and sending first sensor data");
				is_send = true;
				sensor_poll_handler(false);
			}
		}
		return;
	}
}

void sensor_module_thread_fn(void)
{
	int err;
	struct sensor_msg_data msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown sensor module state.");
			break;
		}

		on_all_states(&msg);
	}
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, ui_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);