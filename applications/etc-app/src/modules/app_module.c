#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/drivers/gpio.h>
#include "pcf85263a.h"
#include "etc_settings.h"
#include "etc_interface.h"
#include "etc_device.h"
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
#include "etc_date_time.h"
#endif

#define MODULE			     app

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
#define APP_QUEUE_ENTRY_COUNT	 10
#define APP_QUEUE_BYTE_ALIGNMENT 4

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

	LOG_DBG("State transition %s --> %s", state2str(state), state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s", sub_state2str(sub_state),
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

static void app_peripheral_off(void)
{
	const struct gpio_dt_spec vsen_en_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
	if (!device_is_ready(vsen_en_dt.port)) {
		return;
	}
	gpio_pin_configure_dt(&vsen_en_dt, GPIO_DISCONNECTED);

	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(cons)) {
		LOG_ERR("%s: device not ready.", cons->name);
		return;
	}

#ifdef CONFIG_PM_DEVICE
	pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
#endif

	/* Disconnect all ADC pin */
	const struct device *gpio_0 = device_get_binding("GPIO_0");
	if (!device_is_ready(gpio_0)) {
		LOG_ERR("%s: device not ready.", gpio_0->name);
		return;
	}

	gpio_pin_configure(gpio_0, 31, GPIO_DISCONNECTED);
	gpio_pin_configure(gpio_0, 5, GPIO_DISCONNECTED);
	gpio_pin_configure(gpio_0, 4, GPIO_DISCONNECTED);
}

static void app_peripheral_on(void)
{
	const struct gpio_dt_spec vsen_en_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
	if (!device_is_ready(vsen_en_dt.port)) {
		return;
	}
	gpio_pin_configure_dt(&vsen_en_dt, GPIO_OUTPUT_ACTIVE);
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(cons)) {
		LOG_ERR("%s: device not ready.", cons->name);
		return;
	}
#ifdef CONFIG_PM_DEVICE
	pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
#endif
	LOG_DBG("Wakeup from sleep");
	etc_interface_disable_rtc_event();
#if defined(CONFIG_PCF85263)
	time_t now = 0;
	pcf85263a_rtc_get_time(&now);
	int last_sample = etc_get_time_last_log();  // Get the last wakeup time for sample
	int last_transmit = etc_get_time_last_tx(); // Get the last wakeup time for transmit
	LOG_DBG("Sample %d - Transmit %d - UTC time %d", last_sample, last_transmit, (int)now);
	if (now >= last_sample && now < last_transmit) {
		LOG_DBG("Doing sample");
		etc_device_set_job(ETC_LOGGER_JOB_LOG);
		SEND_EVENT(app, APP_EVT_DATA_GET);
	} else if (now >= last_transmit && now < last_sample) {
		LOG_DBG("Doing transmit");
		etc_device_set_job(ETC_LOGGER_JOB_TX);
		SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
	} else if (now >= last_transmit && now >= last_sample) {
		LOG_DBG("Doing both job");
		etc_device_set_job(ETC_LOGGER_JOB_BOTH);
		SEND_EVENT(app, APP_EVT_DATA_GET);
	} else {
		LOG_DBG("Unknown task - set default job to log");
		etc_device_set_job(ETC_LOGGER_JOB_LOG);
		SEND_EVENT(app, APP_EVT_DATA_GET);
	}
#endif
}

static void app_input_handler(enum etc_interface_event_type type)
{
	if (type == ETC_INTERFACE_EVENT_RTC) {
		app_peripheral_on();
	}
}

static int setup(void)
{
	etc_interface_register_event_handler(app_input_handler);
	static bool is_send = false;
	if ((etc_device_is_logger_lora() == true) && (is_send == false)) {
		LOG_DBG("Request to transmit records");
		is_send = true;
		SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
	}
	return 0;
}

static void app_set_wakeup_time(void)
{
#if defined(CONFIG_PCF85263)
	time_t now = 0;
	bool flag_add_offset = false;
	pcf85263a_rtc_get_time(&now);
	int wakeup_for_sample = etc_device_get_log_interval_second();
	int wakeup_for_transmit = etc_device_get_tx_interval_second();
	int last_sample = etc_get_time_last_log();  // Get the last wakeup time for sample
	int last_transmit = etc_get_time_last_tx(); // Get the last wakeup time for transmit
	int next_sample = 0;
	int next_transmit = 0;
	int sleep_time = 0;
	if ((last_sample == -1) && (last_transmit == -1)) {
		// Setup the wakeup time for next sample and transmit
		next_sample = now + wakeup_for_sample;
		next_transmit = now + wakeup_for_transmit;
	} else {
		if (last_transmit <= now) {
			next_transmit = now + wakeup_for_transmit;
		} else {
			next_transmit = last_transmit;
		}
		if (last_sample <= now) {
			next_sample = now + wakeup_for_sample;
		} else {
			next_sample = last_sample;
		}
	}

	if (next_sample < next_transmit) {
		sleep_time = next_sample - now;
	} else {
		sleep_time = next_transmit - now;
	}
	LOG_DBG("Sample %d (%d) - Transmit %d (%d) - Sleep time %d", next_sample, last_sample,
		next_transmit, last_transmit, sleep_time);
	// Update for next sleep
	etc_set_time_last_log(next_sample);
	etc_set_time_last_tx(next_transmit);
	struct tm tm_time = {0};
	struct tm tm_next_time = {0};
	gmtime_r(&now, &tm_time);

	if ((sleep_time < 60) || (60 - tm_time.tm_sec < 30)) {
		// Increase alarm to 1 minutes because the sleep time is not enough
		sleep_time += 60;
	}

	time_t next_sleep = now + sleep_time;
	gmtime_r(&next_sleep, &tm_next_time);
	
	LOG_DBG("      Now: %02d:%02d:%02d", tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
	LOG_DBG("Wakeup at: %02d:%02d:%02d", tm_next_time.tm_hour, tm_next_time.tm_min, 0);
	pcf85263a_alarm_type_1_config_t config = {
		.seconds = 0,
		.minutes = tm_next_time.tm_min,
		.hours = tm_next_time.tm_hour,
		.days = 0,
		.months = 0,
	};

	pcf85263a_alarm_type_1_flag_t flag = {
		.enable_seconds = 0,
		.enable_minutes = 1,
		.enable_hours = 1,
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
#endif
	k_sleep(K_SECONDS(1)); // Wait for print out LOG
	etc_interface_enable_rtc_event();
	app_peripheral_off();
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
		return;
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(app, APP_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
		return;
	}

	if (IS_EVENT(msg, data, DATA_EVT_DATA_READY)) {
		enum etc_logger_job job = etc_device_get_job();
		if ((job == ETC_LOGGER_JOB_BOTH) || (job == ETC_LOGGER_JOB_TX)) {
			LOG_DBG("DATA_EVT_DATA_READY -> APP_EVT_DATA_TRANSMIT");
			SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
		} else if (job == ETC_LOGGER_JOB_LOG) {
			app_set_wakeup_time();
		}
		return;
	}
	
	if ((IS_EVENT(msg, lora, LORA_EVT_RX_DATA_READY)) ||
	    (IS_EVENT(msg, cloud, CLOUD_EVT_USER_ASSOCIATED))) {
		app_set_wakeup_time();
		return;
	}
}

void app_module_thread_fn(void)
{
	int err;
	struct app_msg_data msg = {0};
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

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, lora_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, modem_event);