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
#include "etc_sensor.h"
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
#include "events/debug_event.h"
#include "modules_common.h"

#define DEFAULT_PUBLISH_INTERVAL_S (60 * 15)
#define MINIMUM_TIME_TO_WAKEUP_S   (3)

enum app_wakeup_tx_work_type {
	APP_WAKEUP_TX_INTERVAL_WORK,
	APP_WAKEUP_TX_PROBE_WORK,
	APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK,
	APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK,
};

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
		struct debug_event debug;
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

/* Get min of 3 unsigned values */
#define MIN_OF_3(a,b,c)  ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

K_MSGQ_DEFINE(msgq_app, sizeof(struct app_msg_data), APP_QUEUE_ENTRY_COUNT,
	      APP_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "app",
	.msg_q = &msgq_app,
	.supports_shutdown = true,
};

/* Store the next wakup */
static int next_wakeup = 0;
static enum app_wakeup_tx_work_type wakeup_tx_type = APP_WAKEUP_TX_INTERVAL_WORK;

K_MUTEX_DEFINE(app_module_lock);

/* Defind functions for set/get next wake up */
static void app_set_next_wakekup(int wakeup, enum app_wakeup_tx_work_type work_type) {
	next_wakeup = wakeup;
	wakeup_tx_type = work_type;
}

static void app_set_tx_work_type(enum app_wakeup_tx_work_type work_type) {
	k_mutex_lock(&app_module_lock, K_FOREVER);
	wakeup_tx_type = work_type;
	k_mutex_unlock(&app_module_lock);
}

static int app_get_next_wakeup(void) {
	int wakeup = 0;
	k_mutex_lock(&app_module_lock, K_FOREVER);
	wakeup = next_wakeup;
	k_mutex_unlock(&app_module_lock);
	return wakeup;
}

static enum app_wakeup_tx_work_type app_get_wakeup_tx_work_type(void) {
	enum app_wakeup_tx_work_type type = APP_WAKEUP_TX_INTERVAL_WORK;
	k_mutex_lock(&app_module_lock, K_FOREVER);
	type = wakeup_tx_type;
	k_mutex_unlock(&app_module_lock);
	return type;
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
	
	if (is_debug_event(aeh))
	{
		struct debug_event *evt = cast_debug_event(aeh);

		msg.module.debug = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		__ASSERT_NO_MSG(err == 0);
		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(app, APP_EVT_ERROR, err);
		}
	}

	return false;
}

static const struct device *pm_devs[] = {
	DEVICE_DT_GET(DT_NODELABEL(spi2)),
	DEVICE_DT_GET(DT_NODELABEL(spi3)),
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console))
};

static void app_peripheral_off(void)
{
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	int ret;

	if (!device_is_ready(cons)) {
		LOG_ERR("%s: device not ready.", cons->name);
		return;
	}

#ifdef CONFIG_PM_DEVICE
	LOG_DBG("suspending devices");
	for (int i = 0; i < ARRAY_SIZE(pm_devs); i++) {
		ret = pm_device_action_run(pm_devs[i], PM_DEVICE_ACTION_SUSPEND);
		if (ret != 0) {
			LOG_ERR("Could not suspend device %s", pm_devs[i]->name);
		}
	}
#endif
}

static time_t align_wakeup(time_t now, int interval_s, enum etc_logger_job job)
{
	time_t wakeup_time;
	uint16_t tx_delay_sec = etc_get_tx_delay_msec() / 1000;

	wakeup_time = now + interval_s;

	if ((DEFAULT_PUBLISH_INTERVAL_S % interval_s) == 0 ||
	    (interval_s % DEFAULT_PUBLISH_INTERVAL_S) == 0) {
		int time_diff = wakeup_time % interval_s;
		int time_to_wakeup = wakeup_time - time_diff - now;

		if (job == ETC_LOGGER_JOB_TX) {
			time_to_wakeup += tx_delay_sec;
		}

		if (time_to_wakeup < MINIMUM_TIME_TO_WAKEUP_S) {
			/* Next wake up is too close to current time. Move next
			   wakeup to following interval. */
			wakeup_time = wakeup_time + (interval_s - time_diff);
		} else if (time_to_wakeup > (interval_s + MINIMUM_TIME_TO_WAKEUP_S)) {
			/* Next wake up is further in the future than necessary.
			   Move next wake up to current interval. */
			wakeup_time = wakeup_time - time_diff - interval_s;
		} else {
			/* Align next wake up to be exactly at the absolute time
			   specified through interval_s */
			wakeup_time = wakeup_time - time_diff;
		}
	}

	if (job == ETC_LOGGER_JOB_TX) {
		/* Add tx_delay for tx wake ups */
		wakeup_time += tx_delay_sec;
	}

	return wakeup_time;
}

static time_t app_get_next_transmit_for_interval_or_probe(time_t now, int transmit_interval_s, 
	enum etc_sensor_status sensor_status) {
	LOG_DBG("%d %d", sensor_status, etc_get_power_mode());
	if ((sensor_status == SENSOR_NO_CONNECTION) && 
		(etc_get_power_mode() == ETC_POWER_MODE_PROBE)) {
		return -1;
	}
	
	return align_wakeup(now, transmit_interval_s, ETC_LOGGER_JOB_TX);
}

static time_t app_get_next_transmit_no_probe(time_t now, uint16_t tx_no_probe_mins, 
	enum etc_sensor_status sensor_status) {
	if ((sensor_status != SENSOR_NO_CONNECTION) || (etc_get_power_mode() != ETC_POWER_MODE_PROBE)) {
		return -1;
	}

	uint16_t tx_delay_sec = etc_get_tx_delay_msec() / 1000;
	struct tm tm_time = {0};
	gmtime_r(&now, &tm_time);
	int hour_offset = etc_device_get_tx_probe_second() / 3600;
	int next_hour = ((tm_time.tm_hour / hour_offset) + 1) * hour_offset;
	if (next_hour >= 24) {
		next_hour = 24;
	}
	/* Return next transmit for no probe */
	return (time_t)(now + (next_hour - tm_time.tm_hour) * 3600 + 
		(tx_no_probe_mins - tm_time.tm_min) * 60 - tm_time.tm_sec + tx_delay_sec); 
}

static time_t app_get_next_transmit_lora_sync_cloud(time_t now, uint16_t tx_logger_lora_mins,
	enum etc_sensor_status sensor_status) {
	if (etc_get_device_mode() != ETC_DEVICE_MODE_LORA_LOGGER) return -1;
	struct tm tm_time = {0};
	gmtime_r(&now, &tm_time);
	int tx_logger_lora_diff_hours = 0;
	if (tm_time.tm_hour <= ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_OFFSET_HOUR) {
		tx_logger_lora_diff_hours = ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_OFFSET_HOUR - tm_time.tm_hour;
	} else {
		tx_logger_lora_diff_hours = (24 - tm_time.tm_hour) + ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_OFFSET_HOUR;
	}
	/* Return the next transmit for logger lora mode to sync with cloud */
	return (time_t)(now + tx_logger_lora_diff_hours * 3600 + 
		(tx_logger_lora_mins  - tm_time.tm_min) * 60 - tm_time.tm_sec);
}

static void app_set_next_wakeup_time_for_job(enum etc_logger_job job) 
{
	k_mutex_lock(&app_module_lock, K_FOREVER);
#if defined(CONFIG_PCF85263)
	time_t now = 0;
	bool flag_add_offset = false;
	pcf85263a_rtc_get_time(&now);
	/* Now >= 0 required for calculations below. */
	__ASSERT_NO_MSG(now >= 0);
	
	int wakeup_for_log = etc_device_get_log_interval_second();
	int wakeup_for_transmit = etc_device_get_tx_interval_second();
	uint16_t tx_offset_logger_lora_mins = etc_device_get_tx_logger_lora_offset_mins();
	uint16_t tx_offset_no_probe_mins = etc_device_get_tx_no_probe_offset_mins();
	time_t next_log = 0;
	time_t next_transmit_normal = 0;
	time_t next_transmit_logger_lora_sync_cloud = 0;
	time_t next_transmit_no_probe = 0;
	time_t next_transmit = 0;
	enum app_wakeup_tx_work_type type = app_get_wakeup_tx_work_type();
	enum etc_sensor_status sensor_status = etc_sensor_get_status();
	LOG_DBG("Mode %d %d %d", etc_device_get_mode(), etc_get_power_mode(), sensor_status);
	int wakeup = 0;

	struct tm tm_time = {0};
	gmtime_r(&now, &tm_time);
	
	switch (job) {
		case ETC_LOGGER_JOB_LOG: {
			next_log = align_wakeup(now, wakeup_for_log, ETC_LOGGER_JOB_LOG);
			break;
		}
		case ETC_LOGGER_JOB_TX: {
			/* Get next transmit in normal case */
			next_transmit_normal = app_get_next_transmit_for_interval_or_probe(now, wakeup_for_transmit,
				sensor_status);
			/* Get next transmit in logger lora case (-1 is no plan for next transmit) */
			next_transmit_logger_lora_sync_cloud = app_get_next_transmit_lora_sync_cloud(now, 
				tx_offset_logger_lora_mins, sensor_status);
			/* Get next transmit in no probe case (-1 is no plan for next transmit) */
			next_transmit_no_probe = app_get_next_transmit_no_probe(now, tx_offset_no_probe_mins, 
				sensor_status);
			/* Cast all next transmit to highest integer value if -1 */
			next_transmit = MIN_OF_3((uint32_t)next_transmit_normal, 
				(uint32_t)next_transmit_logger_lora_sync_cloud, 
				(uint32_t)next_transmit_no_probe);
			break;
		}
		case ETC_LOGGER_JOB_BOTH: {
			/* Get next log */
			next_log = align_wakeup(now, wakeup_for_log, ETC_LOGGER_JOB_LOG);
			/* Get next transmit in normal case */
			next_transmit_normal = app_get_next_transmit_for_interval_or_probe(now, wakeup_for_transmit,
				sensor_status);
			/* Get next transmit in logger lora case (-1 is no plan for next transmit) */
			next_transmit_logger_lora_sync_cloud = app_get_next_transmit_lora_sync_cloud(now, 
				tx_offset_logger_lora_mins, sensor_status);
			/* Get next transmit in no probe case (-1 is no plan for next transmit) */
			next_transmit_no_probe = app_get_next_transmit_no_probe(now, tx_offset_no_probe_mins, 
				sensor_status);
			/* Cast all next transmit to highest integer value if -1 */
			next_transmit = MIN_OF_3((uint32_t)next_transmit_normal, 
				(uint32_t)next_transmit_logger_lora_sync_cloud, 
				(uint32_t)next_transmit_no_probe);
			break;
		}
	}

	if (next_log != 0) {
		struct tm tm_log_time = {0};
		gmtime_r(&next_log, &tm_log_time);
		if (tm_log_time.tm_sec >= 30) {
			/* Round up wake up time to the next minute */
			tm_log_time.tm_min++;
			if (tm_log_time.tm_min >= 60) {
				tm_log_time.tm_min = 0;
				tm_log_time.tm_hour++;
				if (tm_log_time.tm_hour >= 24) {
					tm_log_time.tm_hour = 0;
				}
			}
		}

		/* Calculated actual next wakeup */
		tm_log_time.tm_sec = 0;
		wakeup = (int)timeutil_timegm(&tm_log_time);

		pcf85263a_alarm_type_2_config_t config_2 = {
			.minutes = tm_log_time.tm_min,
			.hours = tm_log_time.tm_hour,
		};

		pcf85263a_alarm_type_2_flag_t flag_2 = {
			.enable_minutes = 1,
			.enable_hours = 1,
			.enable_weekdays = 0,
		};

		pcf85263a_alarm_config_type_2(config_2);
		pcf85263a_alarm_enable_type_2(flag_2);
		LOG_DBG("Next wakeup for logging at: %02d:%02d:%02d", tm_log_time.tm_hour, tm_log_time.tm_min, 0);
	}

	if (next_transmit != 0) {
		if (etc_device_is_relay()) {
			uint16_t wakeup_early = etc_get_wake_early_secs();
			next_transmit = next_transmit > wakeup_early ? next_transmit - wakeup_early : next_transmit;
		} 
		
		struct tm tm_transmit_time = {0};
		gmtime_r(&next_transmit, &tm_transmit_time);
		pcf85263a_alarm_type_1_config_t config_1 = {
			.seconds = tm_transmit_time.tm_sec,
			.minutes = tm_transmit_time.tm_min,
			.hours = tm_transmit_time.tm_hour,
			.days = 0,
			.months = 0,
		};

		pcf85263a_alarm_type_1_flag_t flag_1 = {
			.enable_seconds = tm_transmit_time.tm_sec != 0 ? 1 : 0,
			.enable_minutes = 1,
			.enable_hours = 1,
			.enable_days = 0,
			.enable_months = 0,
		};

		pcf85263a_alarm_config_type_1(config_1);
		pcf85263a_alarm_enable_type_1(flag_1);
		if (next_transmit == next_transmit_logger_lora_sync_cloud) {
			type = APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK;
		} else if (next_transmit == next_transmit_no_probe) {
			type = APP_WAKEUP_TX_PROBE_WORK;
		} else {
			type = APP_WAKEUP_TX_INTERVAL_WORK;
		}
		LOG_DBG("Transmit time for each mode [%d] %d %d %d", type, (int)next_transmit_logger_lora_sync_cloud, 
			(int)next_transmit_no_probe, (int)next_transmit_normal);
		LOG_DBG("Next wakeup for transmitting at: %02d:%02d:%02d", tm_transmit_time.tm_hour, tm_transmit_time.tm_min, tm_transmit_time.tm_sec);


		if ((wakeup == 0) || (wakeup > next_transmit)) {
			wakeup = next_transmit;
		}
	}

	app_set_next_wakekup(wakeup, type);
	
	LOG_DBG("Now at: %02d:%02d:%02d", tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
	LOG_DBG("Log %u - Transmit/Receive %u", (uint32_t)next_log, (uint32_t)next_transmit);

	pcf85263a_interrupt_flag_t interrupt_flag = {
		.enable_level_pulse = 0,
		.enable_periodic = 0,
		.enable_offset_correction = 0,
		.enable_alarm_1 = 1,
		.enable_alarm_2 = 1,
		.enable_timestamp = 0,
		.enable_battery_switch = 0,
		.enable_wdg = 0,
	};

	pcf85263a_interrupt_enable(interrupt_flag);
	pcf85263a_set_interrupt_io(true);
#endif
	k_mutex_unlock(&app_module_lock);
}

static void app_peripheral_on(bool is_rtc)
{	
	int ret;
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(cons)) {
		LOG_ERR("%s: device not ready.", cons->name);
		return;
	}
#ifdef CONFIG_PM_DEVICE
	LOG_DBG("resuming devices");
	for (int i = 0; i < ARRAY_SIZE(pm_devs); i++) {
		ret = pm_device_action_run(pm_devs[i], PM_DEVICE_ACTION_RESUME);
		if (ret != 0) {
			LOG_ERR("Could not resume device %s", pm_devs[i]->name);
		}
	}
#endif
	if (is_rtc) {
		LOG_DBG("Wakeup from sleep");
#if defined(CONFIG_PCF85263)
	time_t now = 0;
	pcf85263a_rtc_get_time(&now);
	bool flag_1 = pcf85263a_is_alarm_1_flags();
	bool flag_2 = pcf85263a_is_alarm_2_flags();
	
	enum etc_device_job job = ETC_DEVICE_JOB_BOTH;
	if (flag_1 && flag_2) {
		job = ETC_DEVICE_JOB_BOTH;
	} else if (flag_1) {
		job = ETC_DEVICE_JOB_TX_RX;
	} else if (flag_2) {
		job = ETC_DEVICE_JOB_LOG;
	} 

	LOG_DBG("UTC time %d - Job %d", (int)now, job);
	switch (job) {
		case ETC_DEVICE_JOB_LOG: {
			LOG_DBG("Doing log");
			etc_device_set_job(ETC_DEVICE_JOB_LOG);
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_LOG);
			SEND_EVENT(app, APP_EVT_DATA_GET);
			break;
		}
		case ETC_DEVICE_JOB_TX_RX: {
			etc_device_set_job(ETC_DEVICE_JOB_TX_RX);
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_TX_RX);
			if (app_get_wakeup_tx_work_type() == APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_SYNC_CLOUD_LORA);
				SEND_EVENT(app, APP_EVT_DATA_SYNC_CLOUD);
			} else {
				if (etc_device_is_relay()) {
					LOG_DBG("Doing receive");
					SEND_EVENT(app, APP_EVT_DATA_RECEIVE);
				} else {
					LOG_DBG("Doing transmit");
					SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
				}
			}
			
			break;
		}
		case ETC_DEVICE_JOB_BOTH: {
			LOG_DBG("Doing both job");
			etc_device_set_job(ETC_DEVICE_JOB_BOTH);
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
			SEND_EVENT(app, APP_EVT_DATA_GET);
			break;
		}
	}
#endif
	} else {
		LOG_DBG("Wakeup from external HALL sensor");
		etc_device_set_job(ETC_DEVICE_JOB_BOTH);
		SEND_EVENT(app, APP_EVT_DATA_GET);
	}
}

static void app_input_handler(enum etc_interface_event_type type)
{
	if (type == ETC_INTERFACE_EVENT_RTC) {
		app_peripheral_on(true);
	} else if (type == ETC_INTERFACE_EVENT_HALL) {
		app_set_tx_work_type(APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK);
		app_peripheral_on(false);
	} else {
		/* No action required */
	}
}

#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
void date_time_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_EXT: {
		int now = date_time_now_second();
		int wakeup = app_get_next_wakeup();
		int now_wakeup_diff = abs(wakeup - now);
		int tx_interval_sec = etc_device_get_tx_interval_second();
		int tx_probe_sec = etc_device_get_tx_probe_second();
		int tx_sec = 0;
		if (etc_get_power_mode() == ETC_POWER_MODE_PROBE) {
			tx_sec = tx_probe_sec;
		} else {
			tx_sec = tx_interval_sec;
		}
		if ((now > wakeup) || 
		    ((now_wakeup_diff > etc_device_get_log_interval_second()) &&
		    (now_wakeup_diff > tx_sec))) {
			LOG_INF("Update wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
		} else if (now_wakeup_diff > etc_device_get_log_interval_second()) {
			LOG_INF("Update log wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_LOGGER_JOB_LOG);
		} else if (now_wakeup_diff > tx_sec) {
			LOG_INF("Update tx wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_TX_RX);
		}
		break;
	}
	case DATE_TIME_SYSTEM_RELOAD: {
		app_set_next_wakeup_time_for_job(ETC_LOGGER_JOB_BOTH);
		break;
	}
	case DATE_TIME_NOT_OBTAINED: 
		break;
	}
}
#endif

static int setup(void)
{
	etc_interface_enable_rtc_event();
	etc_interface_register_event_handler(app_input_handler);
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
	date_time_register_handler(date_time_handler);
#endif
	
	app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);

	if (etc_device_is_relay()) {
		/* No action required */
		LOG_DBG("Device is relay");
	} else {
		static bool is_send = false;
		if (is_send == false) {
			LOG_DBG("Request to transmit records");
			is_send = true;
			SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
		}
	}

	return 0;
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
	if ((IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED) 
	    && !IS_ENABLED(CONFIG_DEBUG_MODULE)) ||
	    IS_EVENT(msg, debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE)) {
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
			if (app_get_wakeup_tx_work_type() == APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
				LOG_DBG("DATA_EVT_DATA_READY -> APP_EVT_DATA_SYNC_CLOUD");
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_SYNC_CLOUD_LORA);
				SEND_EVENT(app, APP_EVT_DATA_SYNC_CLOUD);
			} else if (app_get_wakeup_tx_work_type() == APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK) {
				LOG_DBG("DATA_EVT_DATA_READY -> APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK");
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_SYNC_MAGNET);
				SEND_EVENT(app, APP_EVT_DATA_SYNC_CLOUD);
			} else {
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_NORMAL);
				if (etc_device_is_relay()) {
					LOG_DBG("DATA_EVT_DATA_READY -> APP_EVT_DATA_RECEIVE");
					SEND_EVENT(app, APP_EVT_DATA_RECEIVE);
				} else {
					LOG_DBG("DATA_EVT_DATA_READY -> APP_EVT_DATA_TRANSMIT");
					SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
				}
			}
		} else if (job == ETC_LOGGER_JOB_LOG) {
			app_peripheral_off();
		}
		return;
	}
	
	if (IS_EVENT(msg, lora, LORA_EVT_RX_DATA_READY)) {
		app_peripheral_off();
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED)) {
		app_peripheral_off();
		return;
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT)) {
		app_set_next_wakeup_time_for_job(ETC_LOGGER_JOB_BOTH);
		return;
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_CONNECTED)) {
		app_set_next_wakeup_time_for_job(ETC_LOGGER_JOB_BOTH);
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
#if IS_ENABLED(CONFIG_DEBUG_MODULE)
APP_EVENT_SUBSCRIBE(MODULE, debug_event);
#endif
APP_EVENT_SUBSCRIBE_FINAL(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, lora_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, modem_event);
