#include "app_module_helper.h"
#include "events/app_event.h"
#include "etc_util.h"
#include "etc_settings.h"
#include "modules_common.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_module_helper, CONFIG_ETC_APP_LOG_LEVEL);

static int app_backoff_multiple = 1;
static int app_backoff_last_multiple = -1;

bool app_module_is_aligned_interval(int interval_s)
{
	return (DEFAULT_PUBLISH_INTERVAL_S % interval_s) == 0 ||
	       (interval_s % DEFAULT_PUBLISH_INTERVAL_S) == 0;
}

bool app_module_in_regular_tx_window(time_t now)
{
	int interval_s = etc_device_get_tx_interval_second();
	int duration_s = etc_device_get_rx_duration();

	/* Without an absolute interval boundary the window is undefined; treat as
	 * in-window so the standard tx delay is used. */
	if (interval_s <= 0 || !app_module_is_aligned_interval(interval_s)) {
		return true;
	}

	return (now % interval_s) <= duration_s;
}

uint32_t app_module_lte_sync_backoff_s(uint32_t failures)
{
	uint64_t base = (uint64_t)etc_device_get_tx_interval_second() *
			CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE;
	unsigned int shift = (failures > 31) ? 31 : failures; /* double per failure, saturate */
	uint64_t window = base << shift;

	if (window > CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S) {
		window = CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S;
	}
	if (window < base) {
		window = base; /* ceiling below the floor: honour the floor */
	}
	return (uint32_t)window;
}

bool app_module_lte_sync_over_threshold(uint16_t nack_count)
{
	return etc_device_get_mode() == ETC_DEVICE_MODE_LORA_LOGGER &&
	       nack_count > CONFIG_ETC_APP_LTE_SYNC_NACK_THRESHOLD;
}

bool app_module_lte_sync_due(time_t now, time_t last_attempt, uint32_t failures)
{
	if (now <= 0) { /* RTC not valid yet */
		return false;
	}
	if (last_attempt == 0 || now < last_attempt) { /* never attempted, or clock stepped back */
		return true;
	}

	time_t window = (time_t)app_module_lte_sync_backoff_s(failures);

	return (now - last_attempt) >= (window - APP_LTE_SYNC_SLACK_S);
}

time_t app_module_align_wakeup(time_t now, int interval_s, enum etc_device_job job)
{
	time_t wakeup_time;
	uint16_t tx_delay_sec = etc_get_tx_delay_msec() / 1000;

	wakeup_time = now + interval_s;

	if (app_module_is_aligned_interval(interval_s)) {
		int time_diff = wakeup_time % interval_s;
		int time_to_wakeup = wakeup_time - time_diff - now;

		if (job == ETC_DEVICE_JOB_TX_RX) {
			if (!etc_device_is_relay()) {
				time_to_wakeup += tx_delay_sec;
			}
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

	if (job == ETC_DEVICE_JOB_TX_RX) {
		if (!etc_device_is_relay()) {
			/* Add tx_delay for tx wake ups */
			wakeup_time += tx_delay_sec;
		}
	}

	return wakeup_time;
}

void app_module_backoff_check_multiple_value(void)
{
	enum etc_sensor_status sensor_status = etc_sensor_get_status();
	enum etc_power_mode_e power_mode = etc_get_power_mode();
	LOG_DBG("Sensor %d - Power %d", sensor_status, power_mode);
	if ((sensor_status == SENSOR_NO_CONNECTION) && (power_mode == ETC_POWER_MODE_PROBE)) {
		if (app_backoff_last_multiple != -1) {
			int backoff_expect = app_backoff_last_multiple * 2;
			if ((backoff_expect) != app_backoff_multiple) {
				if (app_backoff_multiple == 1 && app_backoff_last_multiple == 1) {
					app_backoff_multiple = app_backoff_multiple * 2;
				} else {
					app_backoff_multiple = app_backoff_last_multiple;
				}
			} else {
				int tx_interval_s = etc_device_get_tx_interval_second();
				int tx_max_offset_probe_s = etc_device_get_tx_probe_second();
				if ((tx_interval_s * app_backoff_multiple) <=
				    tx_max_offset_probe_s) {
					app_backoff_last_multiple = app_backoff_multiple;
					app_backoff_multiple = app_backoff_multiple * 2;
				}
			}
		} else {
			app_backoff_last_multiple = 1;
			app_backoff_multiple = 1;
		}
		LOG_DBG("%d %d", app_backoff_multiple, app_backoff_last_multiple);
		return;
	}
	LOG_DBG("Reset the backoff");
	/* Reset the backoff */
	app_backoff_multiple = 1;
	app_backoff_last_multiple = 1;
}

time_t app_module_backoff_interval_no_probe(time_t now)
{
	int tx_interval_s = etc_device_get_tx_interval_second();
	int tx_max_offset_probe_s = etc_device_get_tx_probe_second();
	int transmit_interval_s = app_backoff_multiple * tx_interval_s;
	if (transmit_interval_s > tx_max_offset_probe_s) {
		/* If the transmit interval is larger than the probe mode interval,
		 * use the regular method to calculate the next probe mode wake up.
		 */
		return -1;
	}
	LOG_DBG("Backoff interval no problem %d", transmit_interval_s);
	return app_module_align_wakeup(now, transmit_interval_s, ETC_DEVICE_JOB_TX_RX);
}

time_t app_module_get_next_transmit_no_probe(time_t now, uint16_t tx_no_probe_mins,
						 enum etc_sensor_status sensor_status)
{
	/* Ignore this time in Relay Mode */
	if (etc_get_device_mode() == ETC_DEVICE_MODE_RELAY) {
		return -1;
	}

	if ((sensor_status != SENSOR_NO_CONNECTION) ||
	    (etc_get_power_mode() != ETC_POWER_MODE_PROBE)) {
		return -1;
	}

	/* If NACK is more than zero */
	if (etc_device_nack_count() > 0) {
		LOG_DBG("Update next transmit for NACK probe mode");
		time_t next_transmit = app_module_backoff_interval_no_probe(now);
		if (next_transmit != -1) {
			return next_transmit;
		}
	}

	LOG_DBG("Update next transmit for normal probe mode");
	uint16_t tx_delay_sec = etc_get_tx_delay_msec() / 1000;
	struct tm tm_time = {0};
	/* Otherwise, proceed as normal */
	gmtime_r(&now, &tm_time);
	int hour_offset = etc_device_get_tx_probe_second() / 3600;
	int next_hour = ((tm_time.tm_hour / hour_offset) + 1) * hour_offset;
	if (next_hour >= 24) {
		next_hour = 24;
	}

	/* Update next transmit interval in seconds */
	int transmit_interval_s = (next_hour - tm_time.tm_hour) * 3600 +
				  (tx_no_probe_mins - tm_time.tm_min) * 60 - tm_time.tm_sec;

	if (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) {
		return (time_t)(((now + transmit_interval_s) / DEFAULT_PUBLISH_INTERVAL_S) + 1) *
			       DEFAULT_PUBLISH_INTERVAL_S +
		       tx_delay_sec;
	}
	return (time_t)(now + transmit_interval_s + tx_delay_sec);
}

time_t app_module_get_next_transmit_for_interval_or_probe(time_t now, int transmit_interval_s,
							      enum etc_sensor_status sensor_status)
{
	static enum etc_sensor_status last_sensor_status = SENSOR_NA;
	enum etc_power_mode_e current_power = etc_get_power_mode();
	LOG_DBG("%d %d", sensor_status, current_power);
	/* If device is not Relay, check sensor and probe mode */
	if (etc_get_device_mode() != ETC_DEVICE_MODE_RELAY) {
		if (last_sensor_status != sensor_status) {
			if ((last_sensor_status == SENSOR_CONNECTED &&
			     sensor_status == SENSOR_NO_CONNECTION) &&
			    (current_power == ETC_POWER_MODE_PROBE)) {
				last_sensor_status = sensor_status;
				return app_module_align_wakeup(now, transmit_interval_s,
							       ETC_DEVICE_JOB_TX_RX);
			}
			last_sensor_status = sensor_status;
		}

		if ((sensor_status == SENSOR_NO_CONNECTION) &&
		    (current_power == ETC_POWER_MODE_PROBE)) {
			return -1;
		}
	}

	return app_module_align_wakeup(now, transmit_interval_s, ETC_DEVICE_JOB_TX_RX);
}

void app_module_print_time_debug(time_t time, const char *msg)
{
#if CONFIG_ETC_APP_LOG_LEVEL >= LOG_LEVEL_DBG
	struct tm tm_time = {0};
	gmtime_r(&time, &tm_time);
	LOG_DBG("%s, time: %02d:%02d:%02d", msg, tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
#endif
#if CONFIG_BOARD_NATIVE_SIM
	if (time == -1) {
		LOG_INF("%s, time: %d", msg, (int32_t)time);
		return;
	}
	struct tm tm_time = {0};
	gmtime_r(&time, &tm_time);
	LOG_INF("%s, time: %04d-%02d-%02d %02d:%02d:%02d", msg, TM_YEAR_TO_YEAR(tm_time.tm_year),
		TM_MON_TO_MONTH(tm_time.tm_mon), tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min,
		tm_time.tm_sec);
#endif
}

time_t app_module_get_next_transmit_lora_sync_cloud(time_t now, uint16_t tx_logger_lora_mins,
							enum etc_sensor_status sensor_status)
{
	if (etc_get_device_mode() != ETC_DEVICE_MODE_LORA_LOGGER) {
		return -1;
	}
	uint16_t cloud_sync_hour = etc_device_get_tx_lora_cloud_sync_hour();
	uint16_t tx_delay_sec = etc_get_tx_delay_msec() / 1000;
	time_t wakeup_s;
	struct tm tm_time = {0};
	gmtime_r(&now, &tm_time);
	int tx_logger_lora_diff_hours = 0;
	if (tm_time.tm_hour <= cloud_sync_hour) {
		tx_logger_lora_diff_hours = cloud_sync_hour - tm_time.tm_hour;
	} else {
		tx_logger_lora_diff_hours = (24 - tm_time.tm_hour) + cloud_sync_hour;
	}

	wakeup_s = (time_t)(now + tx_logger_lora_diff_hours * 3600 +
			    (tx_logger_lora_mins - tm_time.tm_min) * 60 - tm_time.tm_sec +
			    tx_delay_sec);

	/* Return invalid time when new wakeup time is in the past or too close to current time. */
	if (wakeup_s < (now + MINIMUM_TIME_TO_WAKEUP_S)) {
		return -1;
	}

	/* Return the next transmit for logger lora mode to sync with cloud */
	return wakeup_s;
}

void app_module_notify_calibration_timeout(void)
{
	SEND_EVENT(app, APP_EVT_TIMEOUT_CALIBRATION);
}

enum app_upload_reason app_module_upload_reason_merge(enum app_upload_reason a,
						      enum app_upload_reason b)
{
	/* Rank is explicit so the enum declaration order is not load-bearing. */
	static const uint8_t rank[] = {
		[APP_UPLOAD_NONE] = 0,
		[APP_UPLOAD_NORMAL] = 1,
		[APP_UPLOAD_LORA_SYNC] = 2,
		[APP_UPLOAD_MAGNET] = 3,
	};

	return rank[a] >= rank[b] ? a : b;
}

enum app_upload_reason app_module_upload_reason_from_schedule(enum app_wakeup_tx_work_type sched)
{
	switch (sched) {
	case APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK:
		return APP_UPLOAD_LORA_SYNC;
	case APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK:
		return APP_UPLOAD_MAGNET;
	case APP_WAKEUP_TX_INTERVAL_WORK:
	case APP_WAKEUP_TX_PROBE_WORK:
	default:
		return APP_UPLOAD_NORMAL;
	}
}

enum etc_transmit_sub_job app_module_sub_job_for_reason(enum app_upload_reason reason)
{
	switch (reason) {
	case APP_UPLOAD_LORA_SYNC:
		return ETC_TRANSMIT_SYNC_CLOUD_LORA;
	case APP_UPLOAD_MAGNET:
		return ETC_TRANSMIT_SYNC_MAGNET;
	case APP_UPLOAD_NONE:
	case APP_UPLOAD_NORMAL:
	default:
		return ETC_TRANSMIT_NORMAL;
	}
}