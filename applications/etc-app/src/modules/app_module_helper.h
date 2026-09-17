#ifndef APP_MODULE_HELPER_H_
#define APP_MODULE_HELPER_H_

#include <time.h>
#include "etc_device.h"

#define DEFAULT_PUBLISH_INTERVAL_S (60 * 15)
#define MINIMUM_TIME_TO_WAKEUP_S   (3)

/* Absorbs RTC second-level rounding so an opportunistic LTE bring-up due exactly
 * on a window boundary is not deferred a whole window. Must stay well below the
 * minimum tx interval (60 s). */
#define APP_LTE_SYNC_SLACK_S (5)

/** @brief Why the next alarm-1 (transmit) RTC wakeup was scheduled. */
enum app_wakeup_tx_work_type {
	APP_WAKEUP_TX_INTERVAL_WORK,
	APP_WAKEUP_TX_PROBE_WORK,
	APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK,
	APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK,
};

/** @brief A pending upload request and its cause. */
enum app_upload_reason {
	APP_UPLOAD_NONE,
	APP_UPLOAD_NORMAL,
	/** A sensor reading crossed an immediate report threshold (FW-1179). */
	APP_UPLOAD_THRESHOLD,
	APP_UPLOAD_LORA_SYNC,
	APP_UPLOAD_MAGNET,
};

/**
 * @brief Merge two upload requests into the one that must win.
 *
 * @param a First upload reason.
 * @param b Second upload reason.
 * @return The higher-ranked reason (MAGNET > LORA_SYNC > THRESHOLD > NORMAL > NONE).
 */
enum app_upload_reason app_module_upload_reason_merge(enum app_upload_reason a,
						      enum app_upload_reason b);

/**
 * @brief Translate a scheduled-wakeup reason into an upload request.
 *
 * @param sched The reason the transmit wakeup was scheduled.
 * @return The upload reason to arm for this wakeup.
 */
enum app_upload_reason app_module_upload_reason_from_schedule(enum app_wakeup_tx_work_type sched);

/**
 * @brief Map an upload reason to the transmit sub-job it must run as.
 *
 * @param reason The upload reason being dispatched.
 * @return The matching transmit sub-job.
 */
enum etc_transmit_sub_job app_module_sub_job_for_reason(enum app_upload_reason reason);

/**
 * @brief Current opportunistic-LTE backoff window (FW-789).
 *
 * The window is the transmit interval times @ref CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MULTIPLE,
 * doubled once per consecutive failure and clamped to
 * @ref CONFIG_ETC_APP_LTE_SYNC_BACKOFF_MAX_S. Computed from the live tx interval
 * each call so a cloud config change takes effect immediately.
 *
 * @param failures Consecutive failed LTE bring-ups since the last success.
 * @return The minimum spacing to the next LTE bring-up, in seconds.
 */
uint32_t app_module_lte_sync_backoff_s(uint32_t failures);

/**
 * @brief Whether a LoRa logger has enough unacked readings to also upload over LTE (FW-788).
 *
 * @param nack_count Unacknowledged reading count for this interval.
 * @return true if the device is a LoRa logger with more than
 *         @ref CONFIG_ETC_APP_LTE_SYNC_NACK_THRESHOLD unacked readings.
 */
bool app_module_lte_sync_over_threshold(uint16_t nack_count);

/**
 * @brief Whether an opportunistic LTE bring-up is due given the backoff clock (FW-789).
 *
 * Pure timing check; the caller gates it behind @ref app_module_lte_sync_over_threshold.
 *
 * @param now          Current time (unix seconds); <= 0 means the RTC is not valid yet.
 * @param last_attempt Time of the last LTE bring-up, or 0 if none.
 * @param failures     Consecutive failed LTE bring-ups since the last success.
 * @return true if the backoff window has elapsed (or no prior attempt / clock stepped back).
 */
bool app_module_lte_sync_due(time_t now, time_t last_attempt, uint32_t failures);

/**
 * @brief Checks if the given interval is aligned with @ref DEFAULT_PUBLISH_INTERVAL_S.
 * 
 * @param interval_s The interval in seconds to check alignment for.
 * @return true if the interval is aligned, false otherwise.
 */
bool app_module_is_aligned_interval(int interval_s);

/**
 * @brief Check whether @p now falls inside the regular transmit/receive window.
 *
 * The regular window is [interval boundary, boundary + rx_duration] for the
 * aligned transmit interval (e.g. 12:00, 12:15 ... plus the rendezvous
 * duration). It is used to decide whether the standard tx delay applies or a
 * shorter reclaim-burst delay may be used outside the window. Intervals that
 * are not aligned to an absolute boundary have no well-defined window and are
 * reported as in-window (standard delay).
 *
 * @param now The current time.
 * @return true if @p now is within the regular transmit window, false otherwise.
 */
bool app_module_in_regular_tx_window(time_t now);

/**
 * @brief Aligns the wakeup time to the nearest interval based on the current time and job type.
 * 
 * @param now The current time.
 * @param interval_s The interval in seconds to align the wakeup time to.
 * @param job The job type that requires the wakeup alignment.
 * @return The aligned wakeup time.
 */
time_t app_module_align_wakeup(time_t now, int interval_s, enum etc_device_job job);

/**
 * @brief Checks and handles any conditions related to multiple backoff values in the application.
 */
void app_module_backoff_check_multiple_value(void);

/**
 * @brief Calculates the backoff interval without probing.
 * 
 * @param now The current time.
 * @return The no-probe backoff interval.
 */
time_t app_module_backoff_interval_no_probe(time_t now);

/**
 * @brief Determines the next transmission time without probing, based on specified conditions.
 * 
 * @param now The current time.
 * @param tx_no_probe_mins The transmission interval in minutes, without probing.
 * @param sensor_status The status of the sensor influencing the next transmission time.
 * @return The next transmission time without probing.
 */
time_t app_module_get_next_transmit_no_probe(time_t now, uint16_t tx_no_probe_mins,
						 enum etc_sensor_status sensor_status);

/**
 * @brief Calculates the next transmission time either based on an interval or probe, considering the sensor status.
 * 
 * @param now The current time.
 * @param transmit_interval_s The transmission interval in seconds.
 * @param sensor_status The status of the sensor that may affect transmission timing.
 * @return The next transmission time considering interval or probe.
 */
time_t app_module_get_next_transmit_for_interval_or_probe(time_t now, int transmit_interval_s,
							      enum etc_sensor_status sensor_status);

/**
 * @brief Determines the next LoRa transmission time synchronized with the cloud logger.
 * 
 * @param now The current time.
 * @param tx_logger_lora_mins The interval in minutes for LoRa transmissions.
 * @param sensor_status The status of the sensor impacting the LoRa transmission timing.
 * @return The next LoRa transmission time synced with the cloud.
 */
time_t app_module_get_next_transmit_lora_sync_cloud(time_t now, uint16_t tx_logger_lora_mins,
							enum etc_sensor_status sensor_status);

/**
 * @brief Prints a debug message along with a formatted time string.
 * 
 * @param time The time to be printed, in `time_t` format.
 * @param msg A message to accompany the printed time, providing context.
 */
void app_module_print_time_debug(time_t time, const char *msg);

/**
 * @brief Notify the calibration timeout
 */
void app_module_notify_calibration_timeout(void);
#endif /* APP_MODULE_HELPER_H_ */