#ifndef APP_MODULE_HELPER_H_
#define APP_MODULE_HELPER_H_

#include <time.h>
#include "etc_device.h"

#define DEFAULT_PUBLISH_INTERVAL_S (60 * 15)
#define MINIMUM_TIME_TO_WAKEUP_S   (3)
/**
 * @brief Checks if the given interval is aligned with @ref DEFAULT_PUBLISH_INTERVAL_S.
 * 
 * @param interval_s The interval in seconds to check alignment for.
 * @return true if the interval is aligned, false otherwise.
 */
bool app_module_is_aligned_interval(int interval_s);

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