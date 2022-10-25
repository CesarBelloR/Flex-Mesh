#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/sensor.h>
#include <app_event_manager.h>

#define MODULE sensor_module
#define MODULE_SENSOR_THREAD_STACK_SIZE 2048

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, CONFIG_ETC_APP_LOG_LEVEL);

struct sensor_msg_data {
	union {
		struct app_event app;
		struct data_event data;
		struct util_event util;
	} module;
};

/* Sensor module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

/* Sensor module message queue. */
#define SENSOR_QUEUE_ENTRY_COUNT	10
#define SENSOR_QUEUE_BYTE_ALIGNMENT	4

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

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
		}
	}

	return false;
}

static void apply_config(struct sensor_msg_data *msg)
{
}

static void environmental_data_get(void)
{
	struct sensor_event *sensor_event;
	LOG_DBG("No external sensors, submitting dummy sensor data");
	sensor_event = new_sensor_event();
	sensor_event->type = SENSOR_EVT_ENVIRONMENTAL_NOT_SUPPORTED;
	APP_EVENT_SUBMIT(sensor_event);
}

static int setup(void)
{
	return 0;
}

static bool environmental_data_requested(enum app_data_type *data_list,
					 size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (data_list[i] == APP_DATA_ENVIRONMENTAL) {
			return true;
		}
	}

	return false;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_INIT)) {
		apply_config(msg);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_READY)) {
		apply_config(msg);
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {
		if (!environmental_data_requested(
			msg->module.app.data_list,
			msg->module.app.count)) {
			return;
		}

		environmental_data_get();
	}
}

/* Message handler for all states. */
static void on_all_states(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(sensor, SENSOR_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}
}

static void module_thread_fn(void)
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

K_THREAD_DEFINE(sensor_module_thread, MODULE_SENSOR_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
