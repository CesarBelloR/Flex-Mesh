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
#include "etc_battery.h"
#include "etc_ble.h"
#include "watchdog_app.h"
#define MODULE sensor_module
#include "cloud/cloud_codec/data_codec.h"
#include "modules_common.h"
#include "events/app_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"
#include "events/ui_event.h"
#include "events/cloud_event.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, CONFIG_ETC_APP_LOG_LEVEL);

struct sensor_msg_data {
	union {
		struct app_event app;
		struct util_event util;
		struct ui_event ui;
		struct cloud_event cloud;
	} module;
};

/* Sensor module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_FUNCTIONAL_TEST,
	STATE_SHUTDOWN
} state;

enum sensor_sample_type {
	SENSOR_SAMPLE_NORMAL,
	SENSOR_SAMPLE_TEST,
	SENSOR_SAMPLE_USER_TRIGGERED,
};

static struct sensor_data static_sensor_data;

int64_t last_poll_complete_time_ms = 0;

/* Sensor module message queue. */
#define SENSOR_QUEUE_ENTRY_COUNT	10
#define SENSOR_QUEUE_BYTE_ALIGNMENT	4

#define SENSOR_BATTERY_MAX_VOLTAGE_MS 40

#define SENSOR_HANDLER_MAX_WAIT_S 10
/* The minimum interval that needs to pass between two sensor readings */
#define SENSOR_MIN_INTERVAL_MS 2000

K_MSGQ_DEFINE(msgq_sensor, sizeof(struct sensor_msg_data),
	      SENSOR_QUEUE_ENTRY_COUNT, SENSOR_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "sensor",
	.msg_q = &msgq_sensor,
	.supports_shutdown = true,
};

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

		__ASSERT_NO_MSG(err == 0);
		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
		}
	}

	return false;
}

static void sensor_module_send_sensor(struct sensor_data *sensor,
				      enum sensor_sample_type sample_type)
{
	struct sensor_event *sensor_event = new_sensor_event();
	switch (sample_type) {
	case SENSOR_SAMPLE_NORMAL:
		sensor_event->type = SENSOR_EVT_ENVIRONMENTAL_DATA_READY;
		break;
	case SENSOR_SAMPLE_TEST:
		sensor_event->type = SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY;
		break;
	case SENSOR_SAMPLE_USER_TRIGGERED:
		sensor_event->type = SENSOR_EVT_ENVIRONMENTAL_USER_TRIGGERED_DATA_READY;
		break;
	default:
		__ASSERT_NO_MSG(true);
	}

	sensor_event->data.sensors = sensor;
	APP_EVENT_SUBMIT(sensor_event);
}

static void sensor_module_enter_functional_test(struct sensor_data *sensor)
{
	struct sensor_event *sensor_event = new_sensor_event();

	state_set(STATE_FUNCTIONAL_TEST);

	sensor_event->type = SENSOR_EVT_FUNCTIONAL_TEST_START;
	sensor_event->data.sensors = sensor;
	APP_EVENT_SUBMIT(sensor_event);
}

static void sensor_module_exit_functional_test(void)
{
	if (state != STATE_FUNCTIONAL_TEST) {
		return;
	}
	struct sensor_event *sensor_event = new_sensor_event();

	last_poll_complete_time_ms = k_uptime_get();

	etc_sensor_exit_functional_test();
	state_set(STATE_RUNNING);

	sensor_event->type = SENSOR_EVT_FUNCTIONAL_TEST_END;
	APP_EVENT_SUBMIT(sensor_event);
}

static void sensor_module_battery_handler(enum battery_status status) {
	static enum sensor_event_type last_sensor_battery_event = SENSOR_EVT_ERROR;
	enum sensor_event_type battery_event = SENSOR_EVT_ERROR;
	switch (status) {
		case BATTERY_NORMAL: {
			const uint16_t battery_voltage_mv = etc_battery_get_voltage_mV();
			const uint8_t battery_percent_now =
				etc_battery_percentage_from_voltage(battery_voltage_mv);
			if (battery_percent_now >= BATTERY_MIN_FULL) {
				battery_event = SENSOR_EVT_BATTERY_NORMAL_FULL;
			} else if (battery_percent_now >= BATTERY_MIN_MED) {
				battery_event = SENSOR_EVT_BATTERY_NORMAL_MED;
			} else {
				battery_event = SENSOR_EVT_BATTERY_NORMAL_LOW;
			}
			sensor_module_exit_functional_test();
			break;
		}
		case BATTERY_LOW: {
			battery_event = SENSOR_EVT_BATTERY_NORMAL_LOW;
			sensor_module_exit_functional_test();
			break;
		}
		case BATTERY_CHARGE_IN_PROCESS: {
			battery_event = SENSOR_EVT_BATTERY_IN_CHARGING;
			break;
		}
		case BATTERY_CHARGE_COMPLETE: {
			battery_event = SENSOR_EVT_BATTERY_CHARGE_COMPLETE;
			break;
		}
		case BATTERY_NO_INSTALLED: 
		case BATTERY_DAMAGED: {
			battery_event = SENSOR_EVT_BATTERY_ERROR;
			break;
		}
		default:
			LOG_DBG("Battery Status %d", status);
			break;
	}

	if (last_sensor_battery_event != battery_event) {
		last_sensor_battery_event = battery_event;
		SEND_EVENT(sensor, battery_event);
	}
}

static void sensor_module_evt_handler(enum etc_sensor_status status) {
	LOG_INF("Sensor status %d", status);
	if (status == SENSOR_NO_CONNECTION) {
		SEND_EVENT(sensor, SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT);
	} else if (status == SENSOR_CONNECTED) {
		SEND_EVENT(sensor, SENSOR_EVT_ENVIRONMENTAL_CONNECTED);
	}
}

static int setup(void)
{
	etc_sensor_init(sensor_module_evt_handler);
	etc_battery_init(sensor_module_battery_handler);
	return 0;
}

static bool is_enter_functional_test(void)
{
	enum battery_status bat_status;

	bat_status = etc_battery_get_status();

	if (etc_sensor_get_enter_functional_test() &&
	    (bat_status == BATTERY_CHARGE_IN_PROCESS || 
	     bat_status == BATTERY_CHARGE_COMPLETE)) {
		return true;
	}

	return false;
}

static int sensor_poll_handler(enum sensor_sample_type sample_type)
{
	int64_t now_ms = k_uptime_get();
	if ((sample_type != SENSOR_SAMPLE_TEST) &&
	    (now_ms - last_poll_complete_time_ms) < SENSOR_MIN_INTERVAL_MS) {
		/* Ignore sample request if last reading finished
		 * < SENSOR_MIN_INTERVAL_MS ago. Re-enabling VCC_SENS within
		 * quick succession causes issues with the linear regulator
		 * supplying VCC_A (voltage for analog sensors)
		 */
		return 0;
	}

#if !DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	if (watchdog_sens_sel0_wdt_sem_take(K_SECONDS(SENSOR_HANDLER_MAX_WAIT_S)) != 0) {
		LOG_WRN("Could not take watchdog_sens_sel0 semaphore");
		return -EAGAIN;
	}
#endif
	SEND_EVENT(sensor, SENSOR_EVT_ENVIRONMENTAL_AQUIRING);
	etc_sensor_run_acquisition();

	struct sensor_data* data = &static_sensor_data;
	int utc_timestamp = date_time_now_second();
	data->timestamp = utc_timestamp == -1 ? 0 : utc_timestamp;
	data->sensor[SENSOR_INPUT_AMBIENT] = etc_sensor_get_ambient_temp();
	
	if (sensor_temperature_is_valid(data->sensor[SENSOR_INPUT_AMBIENT])) {
		LOG_DBG("Ambient temp %2.2f", data->sensor[SENSOR_INPUT_AMBIENT]);
	}
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		data->sensor[i] = etc_sensor_get_probe_temp(i);
		if (sensor_temperature_is_valid(data->sensor[i])) {
			LOG_DBG("Channel %d temp %f", i, data->sensor[i]);
		} else {
			LOG_DBG("Channel %d isn't available", i);
		}
	}

	data->sensor[SENSOR_INPUT_HUMID] = etc_sensor_get_probe_humid();
	LOG_DBG("Humid %2.2f%% at port %d", data->sensor[SENSOR_INPUT_HUMID], etc_sensor_get_probe_humid_index());
	data->battery_mV = etc_battery_get_voltage_mV();
	LOG_DBG("Battery %u mV", data->battery_mV);
	data->battery_status = etc_battery_get_status();
	if (is_enter_functional_test()) {
		LOG_DBG("Enter functional test");
		etc_sensor_enter_functional_test();
		sensor_module_enter_functional_test(data);
	} else {
		sensor_module_send_sensor(data, sample_type);
	}
	etc_ble_set_current_sensor(data);
	last_poll_complete_time_ms = k_uptime_get();
	LOG_DBG("Sample total time: %lld", (last_poll_complete_time_ms - now_ms));
#if !DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	watchdog_sens_sel0_wdt_sem_give();
#endif
	return 0;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {
		LOG_INF("APP_EVT_DATA_GET");
		sensor_poll_handler(SENSOR_SAMPLE_NORMAL);
		return;
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET_USER_TRIGGERED)) {
		/* The UI input (HALL Sensor or Button) is triggered */
		sensor_poll_handler(SENSOR_SAMPLE_USER_TRIGGERED);
		return;
	}

	if (IS_EVENT(msg, ui, UI_EVT_TEST_DATA_READY)) {
		sensor_poll_handler(SENSOR_SAMPLE_TEST);
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		/* In boot-up, device connected to cloud, start a sensor poll to get data */
		static bool is_send = false;
		if (!is_send) {
			LOG_DBG("Device is online. Collecting and sending first sensor data");
			is_send = true;
			sensor_poll_handler(SENSOR_SAMPLE_NORMAL);
		}
		return;
	}
}

/* Message handler for STATE_FUNCTIONAL_TEST. */
static void on_state_functional_test(struct sensor_msg_data *msg)
{
}

/* Message handler for all states. */
static void on_all_states(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		state_set(STATE_SHUTDOWN);
		SEND_SHUTDOWN_ACK(sensor, SENSOR_EVT_SHUTDOWN_READY, self.id);
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
		case STATE_FUNCTIONAL_TEST:
			on_state_functional_test(&msg);
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
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, ui_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);