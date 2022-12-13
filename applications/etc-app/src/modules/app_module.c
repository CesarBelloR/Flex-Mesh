#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>

#include "pcf85263a.h"
#include "etc_settings.h"
#include "etc_interface.h"

#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
#include "etc_date_time.h"
#endif

#define MODULE app
#define MODULE_APP_THREAD_STACK_SIZE 2048

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#include "events/app_event.h"
#include "events/cloud_event.h"
#include "events/data_event.h"
#include "events/lora_event.h"
#include "events/sensor_event.h"
#include "events/ui_event.h"
#include "events/util_event.h"
#include "events/modem_event.h"
#include "modules_common.h"

struct app_msg_data {
	union {
		struct cloud_event cloud;
		struct ui_event ui;
		struct sensor_event sensor;
		struct data_event data;
		struct app_event app;
		struct util_event util;
		struct modem_event modem;
		struct lora_event lora;
	} module;
};

/* Data module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

static enum sub_state_type {
	SUB_STATE_ACTIVE_MODE,
	SUB_STATE_PASSIVE_MODE,
} sub_state;

/* Application module message queue. */
#define APP_QUEUE_ENTRY_COUNT		10
#define APP_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(msgq_app, sizeof(struct app_msg_data), APP_QUEUE_ENTRY_COUNT,
	      APP_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "app",
	.msg_q = &msgq_app,
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

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_ACTIVE_MODE:
		return "SUB_STATE_ACTIVE_MODE";
	case SUB_STATE_PASSIVE_MODE:
		return "SUB_STATE_PASSIVE_MODE";
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

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state2str(sub_state),
		sub_state2str(new_state));

	sub_state = new_state;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	struct app_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_cloud_event(aeh)) {
		struct cloud_event *evt = cast_cloud_event(aeh);

		msg.module.cloud = *evt;
		enqueue_msg = true;
	}

	if (is_app_event(aeh)) {
		struct app_event *evt = cast_app_event(aeh);

		msg.module.app = *evt;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *evt = cast_data_event(aeh);

		msg.module.data = *evt;
		enqueue_msg = true;
	}

	if (is_sensor_event(aeh)) {
		struct sensor_event *evt = cast_sensor_event(aeh);

		msg.module.sensor = *evt;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *evt = cast_util_event(aeh);

		msg.module.util = *evt;
		enqueue_msg = true;
	}

	if (is_modem_event(aeh)) {
		struct modem_event *evt = cast_modem_event(aeh);

		msg.module.modem = *evt;
		enqueue_msg = true;
	}

	if (is_ui_event(aeh)) {
		struct ui_event *evt = cast_ui_event(aeh);

		msg.module.ui = *evt;
		enqueue_msg = true;
	}

	if (is_lora_event(aeh)) {
		struct lora_event *evt = cast_lora_event(aeh);

		msg.module.lora = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(app, APP_EVT_ERROR, err);
		}
	}

	return false;
}

static int setup(void)
{
	return 0;
}

static void app_set_wakeup_time(void) {
	time_t now = 0;
	pcf85263a_rtc_get_time(&now);
	uint16_t sample_time_second = etc_get_time_measurement_interval();
	uint8_t sample_time_min = 15;
	uint8_t alarm_min = (uint8_t)((int)(now / 60) % 100);
	alarm_min = ((uint8_t)(alarm_min / sample_time_min) + 1) * sample_time_min;
	if (alarm_min >= 60) {
		alarm_min = 0;
	}

	alarm_min = 39;
	LOG_INF("Set last wakeup at minutes %d %d", alarm_min, (int)now);
	pcf85263a_alarm_type_1_config_t config = {
		.seconds = 0,
		.minutes = alarm_min,
		.hours = 0,
		.days = 0,
		.months = 0,
	};

	pcf85263a_alarm_type_1_flag_t flag = {
		.enable_seconds = 0,
		.enable_minutes = 1,
		.enable_hours = 0,
		.enable_days = 0,
		.enable_months = 0,
	};

	pcf85263a_interrupt_flag_t interrupt_flag = {
		.enable_level_pulse = 0,
		.enable_periodic = 0,
		.enable_offset_correction = 0,
		.enable_alarm_1 = 1,
		.enable_alarm_2 = 0,
		.enable_timestamp = 0,
		.enable_battery_switch = 0,
		.enable_wdg = 0,
	};

	pcf85263a_interrupt_enable(interrupt_flag);
	pcf85263a_set_interrupt_io(true);
	pcf85263a_alarm_config_type_1(config);
	pcf85263a_alarm_enable_type_1(flag);
}

static void data_sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	SEND_EVENT(app, APP_EVT_DATA_GET);
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct app_msg_data *msg)
{
	state_set(STATE_RUNNING);
}

static void on_state_running(struct app_msg_data *msg)
{

}

/* Message handler for SUB_STATE_PASSIVE_MODE. */
static void on_sub_state_passive(struct app_msg_data *msg)
{
}

/* Message handler for SUB_STATE_ACTIVE_MODE. */
static void on_sub_state_active(struct app_msg_data *msg)
{
	
}

/* Message handler for all states. */
static void on_all_events(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
	date_time_start_work();
#endif
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(app, APP_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_SLEEP_READY)) {
		app_set_wakeup_time();
	}
}

static void module_thread_fn(void)
{
	int err;
	struct app_msg_data msg = { 0 };
	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(app, APP_EVT_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(app, APP_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			switch (sub_state) {
			case SUB_STATE_ACTIVE_MODE:
				on_sub_state_active(&msg);
				break;
			case SUB_STATE_PASSIVE_MODE:
				on_sub_state_passive(&msg);
				break;
			default:
				LOG_WRN("Unknown application sub state");
				break;
			}

			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown application state");
			break;
		}

		on_all_events(&msg);
	}
}

K_THREAD_DEFINE(app_module_thread, MODULE_APP_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, lora_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, modem_event);