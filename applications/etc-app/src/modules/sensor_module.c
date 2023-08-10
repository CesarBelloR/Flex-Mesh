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
#include "etc_sensor.h"
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
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_set_dt(&sense_dt, 0U);
#endif
	gpio_pin_set_dt(&s0_dt, channel & 0x01);
	gpio_pin_set_dt(&s1_dt, (channel >> 1) & 0x01);
}

static void sensor_adc_hw_init(void) 
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

	// Set the SLPZ to LOW -> Sleep mode
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_INACTIVE);
#endif
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

#if !DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
static void sensor_gpios_enable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 1U);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_OUTPUT_INACTIVE);
#endif
	/* Note: pin s0 is configured by watchdog module. */
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
}

static void sensor_gpios_disable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 0U);
	gpio_pin_set_dt(&s0_dt, 0);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_DISCONNECTED);
#endif
	/* Note: pin s0 is configured by watchdog module. */
	gpio_pin_configure_dt(&s1_dt, GPIO_DISCONNECTED);
}
#endif

static int sensor_poll_handler(bool is_test) {
	if (sensor_is_processing) {
		return 0;
	}

	#if !DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	if (watchdog_sens_sel0_wdt_sem_take(K_SECONDS(SENSOR_HANDLER_MAX_WAIT_S)) != 0) {
		LOG_WRN("Could not take watchdog_sens_sel0 semaphore");
		return -EAGAIN;
	}

	sensor_gpios_enable();
	#endif

	k_msleep(100);

	sensor_is_processing = true;
	struct sensor_data* data = &static_sensor_data;
	int utc_timestamp = date_time_now_second();
	data->timestamp = utc_timestamp == -1 ? 0 : utc_timestamp;
	data->temperature[SENSOR_INPUT_AMBIENT] = etc_sensor_get_ambient_temp();

	if (data_codec_compare_temperature_is_valid(data->temperature[SENSOR_INPUT_AMBIENT])) {
		LOG_DBG("Ambient temp %2.2f", data->temperature[SENSOR_INPUT_AMBIENT]);
	}
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		sensor_adc_switch_channel(i);
		k_msleep(50);
		data->temperature[i] = etc_sensor_get_probe_temp();
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
	
	#if !DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	sensor_gpios_disable();
	watchdog_sens_sel0_wdt_sem_give();
	#endif
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