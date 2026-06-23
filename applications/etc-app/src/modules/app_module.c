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
#include "etc_memfault.h"
#include "pcf85263a.h"
#include "etc_settings.h"
#include "etc_interface.h"
#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_ble.h"
#include "etc_calibration.h"
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
#include "etc_date_time.h"
#endif
#include "etc_util.h"

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
#include "events/ble_event.h"
#include "modules_common.h"
#include <zephyr/shell/shell.h>
#include "app_module_helper.h"

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
		struct ble_event ble;
	} module;
};

/* Data module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

static enum sub_state_type {
	SUB_STATE_NORMAL_STATE,
	SUB_STATE_FOTA_STATE,
	SUB_STATE_RELAY_RECEIVING,
} sub_state;

static enum relay_state {
	STATE_RELAY_IDLE,
	STATE_RELAY_IN_PROGRESS,
} relay_state;

static enum fota_state {
	STATE_FOTA_IDLE,
	STATE_FOTA_IN_PROGRESS
} fota_state;

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
static int app_backoff_multiple = 1;
static int app_backoff_last_multiple = -1;

static enum app_wakeup_tx_work_type wakeup_tx_type = APP_WAKEUP_TX_INTERVAL_WORK;
static void app_soft_watchdog_work_handler(struct k_work* work);

K_MUTEX_DEFINE(app_module_lock);
K_WORK_DELAYABLE_DEFINE(app_soft_watchdog_work, app_soft_watchdog_work_handler);

static void app_set_tx_work_type(enum app_wakeup_tx_work_type work_type)
{
	k_mutex_lock(&app_module_lock, K_FOREVER);
	if (wakeup_tx_type >= APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
		LOG_WRN("Wakeup tx work type already set to %d", wakeup_tx_type);
		k_mutex_unlock(&app_module_lock);
		return;
	}
	wakeup_tx_type = work_type;
	k_mutex_unlock(&app_module_lock);
}

/* Defind functions for set/get next wake up */
static void app_set_next_wakeup(int wakeup, enum app_wakeup_tx_work_type work_type)
{
	k_mutex_lock(&app_module_lock, K_FOREVER);
	next_wakeup = wakeup;
	app_set_tx_work_type(work_type);
	k_mutex_unlock(&app_module_lock);
}

static int app_get_next_wakeup(void)
{
	int wakeup = 0;
	k_mutex_lock(&app_module_lock, K_FOREVER);
	wakeup = next_wakeup;
	k_mutex_unlock(&app_module_lock);
	return wakeup;
}

static enum app_wakeup_tx_work_type app_get_and_reset_wakeup_tx_work_type(void)
{
	enum app_wakeup_tx_work_type type = APP_WAKEUP_TX_INTERVAL_WORK;
	k_mutex_lock(&app_module_lock, K_FOREVER);
	type = wakeup_tx_type;
	wakeup_tx_type = APP_WAKEUP_TX_INTERVAL_WORK;
	k_mutex_unlock(&app_module_lock);
	return type;
}

static enum app_wakeup_tx_work_type app_get_wakeup_tx_work_type(void)
{
	enum app_wakeup_tx_work_type type = APP_WAKEUP_TX_INTERVAL_WORK;
	k_mutex_lock(&app_module_lock, K_FOREVER);
	type = wakeup_tx_type;
	k_mutex_unlock(&app_module_lock);
	return type;
}

static void app_soft_watchdog_work_handler(struct k_work* work) 
{
	ETC_MEMFAULT_TRACE_EVENT(soft_watchdog_reset);
	SEND_EVENT(app, APP_EVT_REQUEST_SHUTDOWN);
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
	case SUB_STATE_NORMAL_STATE:
		return "SUB_STATE_NORMAL_STATE";
	case SUB_STATE_FOTA_STATE:
		return "SUB_STATE_FOTA_STATE";
	case SUB_STATE_RELAY_RECEIVING:
		return "SUB_STATE_RELAY_RECEIVING";
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

	if (is_debug_event(aeh)) {
		struct debug_event *evt = cast_debug_event(aeh);

		msg.module.debug = *evt;
		enqueue_msg = true;
	}

	if (is_ble_event(aeh)) {
		struct ble_event *evt = cast_ble_event(aeh);

		msg.module.ble = *evt;
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
	if (relay_state == STATE_RELAY_IDLE) {
		const struct device *spi2_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
		ret = pm_device_action_run(spi2_dev, PM_DEVICE_ACTION_SUSPEND);
		if (ret != 0) {
			LOG_ERR("Could not suspend device %s", spi2_dev->name);
		}
	}

	if (fota_state == STATE_FOTA_IDLE) {
		const struct device *spi3_dev = DEVICE_DT_GET(DT_NODELABEL(spi3));
		ret = pm_device_action_run(spi3_dev, PM_DEVICE_ACTION_SUSPEND);
		if (ret != 0) {
			LOG_ERR("Could not suspend device %s", spi3_dev->name);
		}
	}
#endif
}

#define GNSS_TIME_INACCURACY_S 60

/**
 * Determine if the current wakeup interval is a GNSS request interval.
*/
static bool is_gnss_request_interval(time_t now)
{
	time_t time_last_aligned_gnss_request = etc_device_get_last_time_gnss_request();
	uint32_t interval_s = etc_get_gnss_interval_secs();
	time_t next_interval = time_last_aligned_gnss_request + interval_s;

	if (interval_s == 0) {
		return false;
	}

	if (now >= next_interval) {
		struct tm tm_time = {0};

		if (app_module_is_aligned_interval(interval_s)) {
			/* Assign the aligned interval closest to the current time as last interval.
			 */
			time_last_aligned_gnss_request = round_int32(now, interval_s);
		} else {
			time_last_aligned_gnss_request = now;
		}
		etc_device_set_last_time_gnss_request(time_last_aligned_gnss_request);
		
		gmtime_r(&time_last_aligned_gnss_request, &tm_time);
		LOG_INF("GNSS request interval detected");
		LOG_INF("Next GNSS request interval at: %04d-%02d-%02d %02d:%02d:%02d",
			TM_YEAR_TO_YEAR(tm_time.tm_year), TM_MON_TO_MONTH(tm_time.tm_mon),
			tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);

		return true;
	}

	return false;
}

static void set_gnss_request(time_t now)
{
	if (is_gnss_request_interval(now)) {
		SEND_EVENT(app, APP_EVT_REQUEST_LOCATION);
	}
}

static void app_set_next_wakeup_time_for_job(enum etc_device_job job) 
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
		case ETC_DEVICE_JOB_LOG: {
			next_log = app_module_align_wakeup(now, wakeup_for_log, ETC_DEVICE_JOB_LOG);
			break;
		}
		case ETC_DEVICE_JOB_TX_RX: {
			/* Get next transmit in normal case */
			next_transmit_normal = app_module_get_next_transmit_for_interval_or_probe(
				now, wakeup_for_transmit, sensor_status);
			/* Get next transmit in logger lora case (-1 is no plan for next transmit) */
			next_transmit_logger_lora_sync_cloud =
				app_module_get_next_transmit_lora_sync_cloud(
					now, tx_offset_logger_lora_mins, sensor_status);
			/* Get next transmit in no probe case (-1 is no plan for next transmit) */
			next_transmit_no_probe = app_module_get_next_transmit_no_probe(
				now, tx_offset_no_probe_mins, sensor_status);
			/* Cast all next transmit to highest integer value if -1 */
			next_transmit = MIN_OF_3((uint32_t)next_transmit_normal, 
				(uint32_t)next_transmit_logger_lora_sync_cloud, 
				(uint32_t)next_transmit_no_probe);
			break;
		}
		case ETC_DEVICE_JOB_BOTH: {
			/* Get next log */
			next_log = app_module_align_wakeup(now, wakeup_for_log, ETC_DEVICE_JOB_LOG);
			/* Get next transmit in normal case */
			next_transmit_normal = app_module_get_next_transmit_for_interval_or_probe(
				now, wakeup_for_transmit, sensor_status);
			/* Get next transmit in logger lora case (-1 is no plan for next transmit) */
			next_transmit_logger_lora_sync_cloud =
				app_module_get_next_transmit_lora_sync_cloud(
					now, tx_offset_logger_lora_mins, sensor_status);
			/* Get next transmit in no probe case (-1 is no plan for next transmit) */
			next_transmit_no_probe = app_module_get_next_transmit_no_probe(
				now, tx_offset_no_probe_mins, sensor_status);
			/* Cast all next transmit to highest integer value if -1 */
			next_transmit = MIN_OF_3((uint32_t)next_transmit_normal, 
				(uint32_t)next_transmit_logger_lora_sync_cloud, 
				(uint32_t)next_transmit_no_probe);
			break;
		}
	}

	LOG_DBG("Log %u - Transmit/Receive %u", (uint32_t)next_log, (uint32_t)next_transmit);

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
		LOG_DBG("Next wakeup for logging at: %02d:%02d:%02d", tm_log_time.tm_hour,
			tm_log_time.tm_min, 0);
	}

	if (next_transmit != 0) {
		if (etc_device_is_relay()) {
			int16_t wakeup_early = (int16_t)etc_get_wake_early_secs();
			int16_t sleep_time = next_transmit - now;
			/* Skip to next interval if transmit time has passed */
			if (wakeup_early >= sleep_time) {
				next_transmit = app_module_get_next_transmit_for_interval_or_probe(
					next_transmit, wakeup_for_transmit, sensor_status);
			}
			next_transmit = next_transmit > wakeup_early ? next_transmit - wakeup_early
								     : next_transmit;
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
		LOG_DBG("Transmit time for each mode [%d] %d %d %d", type,
			(int)next_transmit_logger_lora_sync_cloud, (int)next_transmit_no_probe,
			(int)next_transmit_normal);
		LOG_DBG("Next wakeup for transmitting at: %02d:%02d:%02d", tm_transmit_time.tm_hour,
			tm_transmit_time.tm_min, tm_transmit_time.tm_sec);

		/* Sync the next transmit data */
		etc_device_set_next_transmit(next_transmit);

		if ((wakeup == 0) || (wakeup > next_transmit)) {
			wakeup = next_transmit;
		}
	}

	app_set_next_wakeup(wakeup, type);
	
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
	if (relay_state == STATE_RELAY_IDLE) {
		const struct device *spi2_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
		ret = pm_device_action_run(spi2_dev, PM_DEVICE_ACTION_RESUME);
		if (ret != 0) {
			LOG_ERR("Could not resume device %s", spi2_dev->name);
		}
	}

	if (sub_state == SUB_STATE_NORMAL_STATE) {
		const struct device *spi3_dev = DEVICE_DT_GET(DT_NODELABEL(spi3));
		ret = pm_device_action_run(spi3_dev, PM_DEVICE_ACTION_RESUME);
		if (ret != 0) {
			LOG_ERR("Could not resume device %s", spi3_dev->name);
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

		set_gnss_request(now);
		
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
			app_module_backoff_check_multiple_value();
			if (app_get_and_reset_wakeup_tx_work_type() ==
			    APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
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
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_TX_RX);

			break;
		}
		case ETC_DEVICE_JOB_BOTH: {
			LOG_DBG("Doing both job");
			app_module_backoff_check_multiple_value();
			etc_device_set_job(ETC_DEVICE_JOB_BOTH);
			SEND_EVENT(app, APP_EVT_DATA_GET);
			if (app_get_and_reset_wakeup_tx_work_type() ==
			    APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_SYNC_CLOUD_LORA);
				SEND_EVENT(app, APP_EVT_DATA_SYNC_CLOUD);
			}
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
			break;
		}
		}
#endif
	} else {
		LOG_DBG("Wakeup from external HALL sensor");
		etc_device_set_job(ETC_DEVICE_JOB_BOTH);
		SEND_EVENT(app, APP_EVT_DATA_GET_USER_TRIGGERED);
	}
}

static void send_calibration_result(enum app_event_type type, int result)
{
	struct app_event *app_event = new_app_event();
	app_event->type = type;
	app_event->data.calibration_result = result;
	APP_EVENT_SUBMIT(app_event);
}

#ifdef CONFIG_ETC_INTERFACE_TEST_SHELL
/* Count of magnet-swipe HALL events that skipped the calibrator scan because the
 * analog rail was still settling. Exposed via the `magnet status` shell command
 * so HIL tests can verify the FW-492 gate deterministically. */
static uint32_t hall_scan_skipped_count;
#endif

static void app_input_handler(enum etc_interface_event_type type)
{
	if (type == ETC_INTERFACE_EVENT_RTC) {
		app_peripheral_on(true);
	} else if (type == ETC_INTERFACE_EVENT_HALL) {
		/* Skip the calibrator scan while the analog rail is still settling.
		 * etc_calibration_check() powers the rail on to scan the 1-wire bus,
		 * and re-powering it within the LDO settling window corrupts the
		 * subsequent readings on repeated magnet swipes (FW-492). The reading
		 * triggered below stays debounced by sensor_poll_handler(). */
		if (!etc_sensor_power_settling()) {
			if (etc_calibration_check() == 0) {
				SEND_EVENT(app, APP_EVT_REQUEST_CALIBRATION);
				return;
			}
			int calib_result = etc_calibration_get_calibration_result();
			if (calib_result >= ETC_SENSOR_CALIB_POST_ADJ_OUT_OF_RANGE) {
				send_calibration_result(APP_EVT_CALIBRATION_ERROR, calib_result);
			}
		} else {
			LOG_INF("Skipping calibrator scan: analog rail still settling");
#ifdef CONFIG_ETC_INTERFACE_TEST_SHELL
			hall_scan_skipped_count++;
#endif
		}
		app_set_tx_work_type(APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK);
		etc_ble_start_adv_with_timeout();
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
		if ((now > wakeup) || ((now_wakeup_diff > etc_device_get_log_interval_second()) &&
				       (now_wakeup_diff > tx_sec))) {
			LOG_INF("Update wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
		} else if (now_wakeup_diff > etc_device_get_log_interval_second()) {
			LOG_INF("Update log wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_LOG);
		} else if (now_wakeup_diff > tx_sec) {
			LOG_INF("Update tx wakeup time after date/time synced");
			app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_TX_RX);
		}
		break;
	}
	case DATE_TIME_SYSTEM_RELOAD: {
		app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
		break;
	}
	case DATE_TIME_NOT_OBTAINED: 
	case DATE_TIME_PREVIOUS:
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

	int soft_watchdog_timeout_secs = etc_get_soft_watchdog_timeout_secs();
	if (soft_watchdog_timeout_secs != -1) {
		k_work_schedule(&app_soft_watchdog_work, K_SECONDS(soft_watchdog_timeout_secs));
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

/* Message handler for SUB_STATE_NORMAL_STATE. */
static void on_sub_state_normal(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_START) || IS_EVENT(msg, ble, BLE_EVT_FOTA_START)) {
		sub_state_set(SUB_STATE_FOTA_STATE);
		return;
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RELAY_START_RX)) {
		sub_state_set(SUB_STATE_RELAY_RECEIVING);
		return;
	}
}

/* Message handler for SUB_STATE_FOTA_STATE. */
static void on_sub_state_fota(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DONE) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DOWNLOADED) ||
	    IS_EVENT(msg, ble, BLE_EVT_FOTA_DONE) || IS_EVENT(msg, ble, BLE_EVT_FOTA_ERROR)) {
		/* Changing fota_state is processed in on_all_states() */
		if (relay_state == STATE_RELAY_IN_PROGRESS) {
			sub_state_set(SUB_STATE_RELAY_RECEIVING);
		} else {
			sub_state_set(SUB_STATE_NORMAL_STATE);
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DOWNLOADED)) {
		/* Trigger a cloud connection and transmit after FOTA has been downloaded
		 * to communicate the current firmware update state.
		*/
		SEND_EVENT(app, APP_EVT_DATA_TRANSMIT);
	}
}

/* Message handler for SUB_STATE_FOTA_STATE. */
static void on_sub_state_relay_in_progress(struct app_msg_data *msg)
{
	if (IS_EVENT(msg, lora, LORA_EVT_RELAY_RX_COMPLETE)) {
		/* Changing relay_state is processed in on_all_states() */
		if (fota_state == STATE_FOTA_IN_PROGRESS) {
			sub_state_set(SUB_STATE_FOTA_STATE);
		} else {
			sub_state_set(SUB_STATE_NORMAL_STATE);
		}
	}
}

/* Message handler for all states. */
static void on_all_events(struct app_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED_READY) &&
	     !IS_ENABLED(CONFIG_DEBUG_MODULE)) ||
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
		enum etc_device_job job = etc_device_get_job();
		if ((job == ETC_DEVICE_JOB_BOTH) || (job == ETC_DEVICE_JOB_TX_RX)) {
			enum app_wakeup_tx_work_type type = app_get_and_reset_wakeup_tx_work_type();
			if (type == APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK) {
				LOG_DBG("DATA_EVT_DATA_READY -> APP_EVT_DATA_SYNC_CLOUD");
				etc_device_set_transmit_sub_job(ETC_TRANSMIT_SYNC_CLOUD_LORA);
				SEND_EVENT(app, APP_EVT_DATA_SYNC_CLOUD);
			} else if (type == APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK) {
				LOG_DBG("DATA_EVT_DATA_READY -> "
					"APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK");
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
		} else if (job == ETC_DEVICE_JOB_LOG) {
			app_peripheral_off();
		}
		return;
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RX_DATA_READY) || IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED)) {
		app_peripheral_off();
		return;
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT)) {
		app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
		return;
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_CONNECTED)) {
		app_set_next_wakeup_time_for_job(ETC_DEVICE_JOB_BOTH);
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		int soft_watchdog_timeout_secs = etc_get_soft_watchdog_timeout_secs();
		if (soft_watchdog_timeout_secs != -1) {
			k_work_reschedule(&app_soft_watchdog_work, 
					  K_SECONDS(soft_watchdog_timeout_secs));	
		}
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_START) || IS_EVENT(msg, ble, BLE_EVT_FOTA_START)) {
		fota_state = STATE_FOTA_IN_PROGRESS;
		return;
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RELAY_START_RX)) {
		relay_state = STATE_RELAY_IN_PROGRESS;
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DONE) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DOWNLOADED) ||
	    IS_EVENT(msg, ble, BLE_EVT_FOTA_DONE) || IS_EVENT(msg, ble, BLE_EVT_FOTA_ERROR)) {
		fota_state = STATE_FOTA_IDLE;
		return;
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RELAY_RX_COMPLETE)) {
		relay_state = STATE_RELAY_IDLE;
		app_peripheral_off();
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
	sub_state_set(SUB_STATE_NORMAL_STATE);
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
			case SUB_STATE_NORMAL_STATE:
				on_sub_state_normal(&msg);
				break;
			case SUB_STATE_FOTA_STATE:
				on_sub_state_fota(&msg);
				break;
			case SUB_STATE_RELAY_RECEIVING:
				on_sub_state_relay_in_progress(&msg);
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

static int cmd_trigger_tx(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Triggering sample + LoRa TX (interval)");
	etc_device_set_job(ETC_DEVICE_JOB_BOTH);
	SEND_EVENT(app, APP_EVT_DATA_GET);
	return 0;
}

static int cmd_trigger_rx(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Triggering relay LoRa RX listen (interval)");
	etc_device_set_job(ETC_DEVICE_JOB_TX_RX);
	SEND_EVENT(app, APP_EVT_DATA_RECEIVE);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_app_module,
			       SHELL_CMD(trigger_rx, NULL,
					 "Trigger relay LoRa RX listen (interval-based)",
					 cmd_trigger_rx),
			       SHELL_CMD(trigger_tx, NULL,
					 "Trigger sample read and LoRa TX (interval-based)",
					 cmd_trigger_tx),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(app_module, &sub_app_module, "App module commands", NULL);

#ifdef CONFIG_ETC_INTERFACE_TEST_SHELL
static int cmd_magnet_swipe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	etc_interface_test_inject_hall();
	shell_print(sh, "Magnet swipe (HALL) injected");
	return 0;
}

static int cmd_magnet_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "settling=%s skips=%u", etc_sensor_power_settling() ? "yes" : "no",
		    hall_scan_skipped_count);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_magnet, SHELL_CMD(swipe, NULL, "Inject a magnet swipe (HALL) event.", cmd_magnet_swipe),
	SHELL_CMD(status, NULL, "Print rail-settling state and skipped-scan count.",
		  cmd_magnet_status),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(magnet, &sub_magnet, "Magnet-swipe HIL test commands (FW-492)", NULL);
#endif /* CONFIG_ETC_INTERFACE_TEST_SHELL */

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
APP_EVENT_SUBSCRIBE_FINAL(MODULE, ble_event);