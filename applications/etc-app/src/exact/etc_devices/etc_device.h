#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_

#include "events/sensor_event.h"
#include "etc_sensor.h"
#include "etc_device_helper.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Logger ID size */
#define ETC_DEVICE_LORA_LOGGER_ID_SIZE (sizeof("FFFFFFFFFFFFFFFF"))
/* App version size */
#define ETC_DEVICE_APP_VER_SIZE	       (sizeof("##.##.##") + 1)

/* Lora mode sync with cloud offset */
#define ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_HOUR		  (16)
/* Maximum random hourly offset for LoRa cloud sync */
#define ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_HOUR_OFFSET_MAX (6)
/* Equal the buffer for decoded buffer Lora */
#define ETC_DEVICE_RELAY_BUF_SIZE			  (128)
/* Number of extra elements in logger data */
#define ETC_DEVICE_NUM_EXTRA_ELEMENT			  (4)
/* Define a invalid for element in Logger data */
#define ETC_DEVICE_INVALID_VALUE_ELEMENT		  (0xCAFEBEEF)
/* Define a pubkey ID length */
#define IMG_PUBKEY_ID_LEN				  (4)
/* Max element in record for Relay */
#define ETC_RELAY_RECORD_MAX_ELEMENT			  (CONFIG_ETC_DEVICE_RELAY_MAX_RECORD_HISTORY)
/* Define a mask for relay data ready */
#define ETC_DEVICE_RELAY_DATA_READY_MASK		  (0x80000000U)
/* Maximum reclaim relay supports */
#define ETC_RECLAIM_RELAY_MAX_ELEMENT			  (20)
/* Maximum number of logger samples in one relay package */
#define ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS		  5

/* Define an enum to describe the job of logger currently */
enum etc_device_job {
	ETC_DEVICE_JOB_LOG = 0x00,
	ETC_DEVICE_JOB_TX_RX,
	ETC_DEVICE_JOB_BOTH,
};

/* Define an enum to describe the sub job for transmit event */
enum etc_transmit_sub_job {
	ETC_TRANSMIT_NORMAL,
	ETC_TRANSMIT_SYNC_CLOUD_LORA,
	ETC_TRANSMIT_SYNC_MAGNET,
};

enum etc_device_type {
	ETC_DEVICE_TYPE_LOGGER,
	ETC_DEVICE_TYPE_RELAY,
	ETC_DEVICE_TYPE_EMBEDDABLE,
	ETC_DEVICE_TYPE_AMBIENT,
};

/* Define a enum to describe about device mode */
enum etc_device_mode {
	ETC_DEVICE_MODE_RELAY = 0x00,
	ETC_DEVICE_MODE_LORA_LOGGER = 0x01,
	ETC_DEVICE_MODE_LTE_LOGGER = 0x02,
	ETC_DEVICE_MODE_BLE = 0x03,
};

/* Define a enum to describe about power mode */
enum etc_power_mode_e {
	ETC_POWER_MODE_ALWAYS_ON = 0x00,
	ETC_POWER_MODE_INTERVAL = 0x01,
	ETC_POWER_MODE_PROBE = 0x02,
};

/* Define a enum to describe about alarm condition (direction) */
enum etc_alarm_direction {
	ETC_ALARM_DIR_GREATER = 0x00,
	ETC_ALARM_DIR_LESS = 0x01,
};

enum gnss_location_request_status {
	ETC_GNSS_LOCATION_NO_REQUEST,
	ETC_GNSS_LOCATION_REQUESTED,
	ETC_GNSS_LOCATION_ACQUIRED,
	ETC_GNSS_LOCATION_TIMEOUT
};

enum etc_setting_id {
	ETC_CONFIG_ID = 0x01,
	ETC_RECORD_STAT = 0x02,
	ETC_RECORD_RECLAIM = 0x03,
	ETC_SERIAL_NUMBER_TYPE = 0x04,
	ETC_SERIAL_NUMBER_ID = 0x05,
	ETC_PSK_ID = 0x06,
	ETC_RTC_CALIBRATION_OFFSET_PPM,
	/* Reference value to compensate temperature-dependent ADC error */
	ETC_ADC_TEMPERATURE_REFERENCE,
	/* Current status of GNSS location request */
	ETC_GNSS_LOCATION_REQUEST_STATUS,
	/* Last location information retrieved through GNSS */
	ETC_GNSS_LAST_LOCATION,
	/* Time when GNSS was last requested */
	ETC_GNSS_TIME_LAST_REQUEST,
	/* Flag to indicate if records have been erased after upgrade */
	ETC_RECORDS_ERASED_AFTER_UPGRADE,
	ETC_SETTING_HW_VERSION_ID = 0x100,
	ETC_SETTING_FW_VERSION_ID,
	ETC_SETTING_DEVICE_ID,
	ETC_SETTING_TIME_MEASURE_INTERVAL_ID,
	ETC_SETTING_TIME_TRANSMISSION_INTERVAL_ID,
	ETC_SETTING_DEVICE_MODE_ID,
	ETC_SETTING_RADIO_MODE_ID,
	ETC_SETTING_POWER_MODE_ID,
	ETC_SETTING_ALARM_DIRECTION_ID,
	ETC_SETTING_LOG_INTERVAL_SECS_ID,
	ETC_SETTING_LOG_INTERVAL_ALARM_SECS_ID,
	ETC_SETTING_TX_INTERVAL_SECS_ID,
	ETC_SETTING_TX_INTERVAL_ALARMS_SECS_ID,
	ETC_SETTING_WAKEUP_EARLY_SECS_ID,
	ETC_SETTING_TX_DELAY_MSEC_ID,
	ETC_SETTING_RX_DURATION_SECS_ID,
	ETC_SETTING_ALARM_THRESHOLD_ID,
	ETC_SETTING_DEVICE_NEXT_JOB_ID,
	ETC_SETTING_TX_PROBE_SEC_ID,
	ETC_SETTING_LORA_PROBE_MODE_OFFSET_SEC_ID,
	ETC_SETTING_LTE_PROBE_MODE_OFFSET_SEC_ID,
	ETC_SETTING_GNSS_INTERVAL_SEC_ID,
	ETC_SETTING_GNSS_TIMEOUT_SEC_ID,
	ETC_SETTING_RX_TIMEOUT_SEC_ID,
	ETC_FUNCTIONAL_TEST_RSRP_ID = 0xE00,
	ETC_CALIBRATION_OFFSET_ID = 0xFF0,
	ETC_CALIBRATION_RAWHIGH_ID,
	ETC_CALIBRATION_REF_ID,
	ETC_CALIBRATION_TIME_ID,
	ETC_CALIBRATOR_ID,
	ETC_CALIBRATION_USER_OFFSET_ID = 0xFF7,
	ETC_CALIBRATION_USER_RAWHIGH_ID,
	ETC_CALIBRATION_USER_REF_ID,
	ETC_CALIBRATION_USER_TIME_REF_ID,
	ETC_CALIBRATOR_USER_ID,
	ETC_RECORD_RAM_ID = 0xFFF,
	ETC_RECORD_HEADER = 0x1000,
};

struct etc_config {
	enum etc_device_mode device_mode;
	enum etc_power_mode_e power_mode;
	enum etc_alarm_direction alarm_direction;
	uint32_t log_interval_secs;
	uint32_t log_interval_alarm_secs;
	uint32_t tx_interval_secs;
	uint32_t tx_interval_alarm_secs;
	uint32_t tx_probe_secs;
	uint32_t gnss_interval_secs;
	uint16_t wake_early_secs;
	uint16_t tx_delay_msec;
	uint16_t rx_duration_secs;
	uint16_t alarm_threshold;
	uint16_t lora_probe_offset_secs;
	uint16_t lte_probe_offset_secs;
	uint16_t gnss_timeout_secs;
	uint8_t rx_timeout_secs;
};

struct etc_device_relay_record {
	bool is_reclaim;
	int16_t logger_rssi;
	float battery;
	int8_t packet_number;
	uint32_t timestamp;
	char relay_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE];
	char logger_ver[ETC_DEVICE_APP_VER_SIZE];
	char logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE];
	float sensor[ETC_DEVICE_NUM_SENSOR];
	int data[ETC_DEVICE_NUM_EXTRA_ELEMENT];
};

struct etc_device_relay_packet {
	struct etc_device_relay_record records[ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS];
	uint8_t num_records;
};

struct etc_device_relay_record_stat {
	uint16_t read_index;
	uint16_t write_index;
	/* Number of records that were written, but not read */
	uint16_t number_record;
	/* The number/count of records last read with etc_device_read_relay_data_packet() */
	uint8_t last_read_count;
	bool flag_error;
	bool flag_over_flow;
};

#pragma pack(push, 1)
struct etc_gnss_data {
	int64_t latitude;
	int64_t longitude;
	time_t timestamp;
};
#pragma pack(pop)

/* Structure for request reclaim over cloud or BLE */
struct etc_device_reclaim_request {
	char logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE];
	int start_time;
	int stop_time;
	atomic_t flag_set;
};

/**
 * @brief Initialize the Non-Volatile Storage (NVS) for the ETC device.
 */
void etc_device_nvs_init(void);

/**
 * @brief Initialize the ETC device.
 */
void etc_device_init(void);

/**
 * @brief Write a setting to the ETC device.
 *
 * @param setting_id	The ID of the setting to be written.
 * @param setting	A pointer to the setting data.
 * @param setting_size	The size of the setting data.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_write_setting(uint16_t setting_id, const void *setting, int setting_size);

/**
 * @brief Read a setting from the ETC device.
 *
 * @param setting_id	The ID of the setting to be read.
 * @param setting	A pointer to store the read setting data.
 * @param setting_size	The size of the buffer to store the setting data.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size);

/**
 * @brief Delete a setting from the ETC device.
 *
 * @param setting_id	The ID of the setting to be read.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_delete_setting(uint16_t setting_id);

/**
 * @brief Read a setting from non-volatile storage and return the data's length.
 * Same as @ref etc_device_read_setting, but returning the length of the setting.
 *
 * @param setting_id NVS ID of the setting to be read. One of @ref enum etc_setting_id
 * @param setting Buffer to store the retrieved setting
 * @param setting_size Size of the setting buffer
 *
 * @return Number of bytes read on success. Negative on error.
 */
int etc_device_read_setting_with_len(uint16_t setting_id, void *setting, int setting_size);

/**
 * @brief Write a sensor record to the ETC device.
 *
 * @param sensor	A pointer to the sensor data structure.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_write_record_sensor(struct sensor_data *sensor);

/**
 * @brief Write a generic record to the ETC device.
 *
 * @param record	A pointer to the generic record data structure.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_write_record(union etc_device_record *record);

/**
 * @brief Writes relay data to queue.
 *
 * @param record Relay record containing the data to be written.
 * @return 0 on success, an error code otherwise.
 */
int etc_device_write_relay_data(struct etc_device_relay_record *record);

/**
 * @brief Read relay data from queue.
 *
 * @param record Pointer to the relay record where the read data will be stored.
 * @return 0 on success, an error code otherwise.
 */
int etc_device_read_relay_data(struct etc_device_relay_record *record);

/**
 * @brief Read a relay data packet from the queue.
 *
 * A data packet can contain multiple samples from multiple loggers.
 *
 * @param packet Pointer to packet buffer for storing the read data.
 * @return 0 on success, negative on error.
 */
int etc_device_read_relay_data_packet(struct etc_device_relay_packet *packet);

/**
 * @brief Sync the relay read index from queue buffer.
 */
int etc_device_sync_relay_data(void);

/**
 * @brief the next-in-line (unack'd) measurement record. If a reclaim is active,
 * previously ack'd records that are part of the reclaim period will be returned
 * as well.
 *
 * @param record Buffer to store measurement record.
 * @param active_reclaim Buffer to store the current reclaim status. True if
 * reclaim is active.
 *
 * @retval Record ID >0 if successful.
 * @retval 0 if error or no nack record available (all records have been ack'd
 * and there is no active reclaim).
 */
int etc_device_read_record(union etc_device_record *record, bool *active_reclaim);

/**
 *  @brief Set the ack status
 *  @param record_id the ID of record to set ack status
 *
 * @return	0 on success, an error code otherwise.
 */
int etc_device_set_ack_record(int record_id);

/**
 * Get the current number of not acknowledged samples (nacks) stored on the
 * device.
 *
 * @return Number of not acknowledged samples.
 */
uint16_t etc_device_nack_count(void);

/**
 * @brief Check if the ETC device is configured for LoRa logging.
 *
 * @return	true if LoRa logging is enabled, false otherwise.
 */
bool etc_device_is_logger_lora(void);

/**
 * @brief Check if the ETC device is configured for LoRa relaying.
 *
 * @return	true if LoRa relaying is enabled, false otherwise.
 */
bool etc_device_is_relay(void);

/**
 * @brief Check if the ETC device is configured for ALWAYS ON power mode.
 *
 * @return	true if ETC device is ALWAYS ON mode
 */
bool etc_device_is_always_on(void);

/**
 * @brief Get the receive timeout for the ETC device.
 *
 * @return	The receive timeout value.
 */
int etc_device_get_rx_duration(void);

/**
 * @brief Get the log interval in seconds for the ETC device.
 *
 * @return	The log interval in seconds.
 */
int etc_device_get_log_interval_second(void);

/**
 * @brief Get the transmit interval in seconds for the ETC device.
 *
 * @return	The transmit interval in seconds.
 */
int etc_device_get_tx_interval_second(void);

/**
 * @brief Get the transmit probe interval in seconds for the ETC device.
 *
 * @return	The transmit probe interval in seconds.
 */
int etc_device_get_tx_probe_second(void);

/**
 * @brief Get the current operating mode of the ETC device.
 *
 * @return	The current operating mode.
 */
enum etc_device_mode etc_device_get_mode(void);

/**
 * @brief Set the job for the ETC logger.
 *
 * @param job	The job to set for the logger.
 */
void etc_device_set_job(enum etc_device_job job);

/**
 * @brief Get the current job of the ETC logger.
 *
 * @return	The current job of the logger.
 */
enum etc_device_job etc_device_get_job(void);

/**
 * @brief Sets the transmit sub job for the ETC device.
 *
 * This function sets the transmit sub job for the ETC device.
 *
 * @param job The transmit sub job to be set.
 */
void etc_device_set_transmit_sub_job(enum etc_transmit_sub_job job);

/**
 * @brief Gets the current transmit sub job for the ETC device.
 *
 * This function retrieves the current transmit sub job for the ETC device.
 *
 * @return The current transmit sub job.
 */
enum etc_transmit_sub_job etc_device_get_transmit_sub_job(void);

/**
 * @brief Erase the configuration of the ETC device.
 *
 * @return	0 on success, an error code otherwise.
 */
int etc_device_erase_cfg(void);

/**
 * @brief Get the offset in minutes for transmitting logs using LoRa.
 *
 * @return	The LoRa transmission offset in minutes.
 */
uint16_t etc_device_get_tx_logger_lora_offset_mins(void);

/**
 * @brief Get the hour of the day when the cloud sync should happen for LoRa loggers.
 *
 * This hour is randomly generated and is
 * @ref ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_HOUR plus a random offset of maximum
 * 5 hours.
 */
uint16_t etc_device_get_tx_lora_cloud_sync_hour(void);

/**
 * @brief Get the offset in minutes for transmitting logs without probe.
 *
 * @return	The no probe transmission offset in minutes.
 */
uint16_t etc_device_get_tx_no_probe_offset_mins(void);

/**
 * @brief Get the current image's pubkey ID. The public key ID is the first 4 bytes
 * of the public key hash that is stored in the image's TLV.
 *
 * @param pubkey_id Buffer to hold the pubkey id. Needs to be at least 4 bytes
 * long. See @ref IMG_PUBKEY_ID_LEN.
 * @param pubkey_id_len Size of the buffer.
 *
 * @return Number of bytes written to pubkey_id
 */
int etc_device_get_img_pubkey_id(uint8_t *pubkey_id, uint8_t pubkey_id_len);

/**
 * @brief Set the next transmit time for the device.
 *
 * This function sets the next transmit time for the device.
 * The time is specified in seconds until the next transmission.
 *
 * @param next_transmit_s The number of seconds for the next transmission.
 */
void etc_device_set_next_transmit(time_t next_transmit_s);

/**
 * @brief Get the next transmit time for the device.
 *
 * Retrieves the next transmit time that has been set for the device.
 * The time is returned in seconds until the next transmission.
 *
 * @return Returns the number of seconds until the next transmission.
 */
time_t etc_device_get_next_transmit(void);

/**
 * Set a GNSS location request status. The modem module uses this information
 * to determine if a location should be searched through GNSS.
 *
 * @param status New status of type @ref enum gnss_location_request_status
 *
 * @retval 0 success
 * @retval <0 fail
 */
int etc_device_set_location_request(enum gnss_location_request_status status);

/**
 * Check if a location through GNSS was requested. If there is an active request,
 * GNSS should be enabled to try to determine a location.
 *
 * @retval true A GNSS location was requested.
 * @retval false A GNSS location was not requested.
 */
bool etc_device_is_location_requested(void);

/**
 * Set the time that GNSS was last requested and save to non-volatile memory.
 * Storing the last requested time in nv memory ensures that there is not a GNSS
 * request on every reboot.
 *
 * NOT THREAD SAFE
 *
 * @param time_requested Last time GNSS was requested in seconds since epoch.
 *
 * @retval 0 success
 * @retval <0 fail
 */
int etc_device_set_last_time_gnss_request(time_t time_requested);

/**
 * Get the time that GNSS was last requested.
 *
 * NOT THREAD SAFE
 *
 * @return Time GNSS was last requested in seconds since epoch.
 */
time_t etc_device_get_last_time_gnss_request(void);

/**
 * Write the location information to non-volatile memory.
 *
 * NOT THREAD SAFE
 *
 * @param data Buffer containing GNSS data.
 *
 * @retval 0 success
 * @retval <0 error
 */
int etc_device_set_location(struct etc_gnss_data *data);

/**
 * Retrieve the last saved GNSS location from non-volatile memory.
 *
 * NOT THREAD SAFE
 *
 * @param data Buffer to write retrieved GNSS data to.
 *
 * @retval 0 success
 * @retval <0 error
 */
int etc_device_retrieve_location(struct etc_gnss_data *data);

/**
 * @brief Writes a sensor record to the relay.
 *
 * This function writes the sensor data to RAM data for relay
 *
 * @param sensor Pointer to the sensor_data that contains the sensor to be set
 */
void etc_device_relay_write_record_sensor(struct sensor_data *sensor);

/**
 * @brief Reads a sensor record from the relay device.
 *
 * This function attempts to read latest sensor data for relay device.
 *
 * @param record Pointer to a union etc_device_record where the read sensor data will be stored.
 *
 * @retval 0 success
 * @retval <0 error
 */
int etc_device_relay_read_record_sensor(union etc_device_record *record);

/**
 * @brief Re-sync record on RAM
 *
 * This function will sync all record on RAM to external flash.
 * It should call after OTA done.
 */
void etc_device_sync_record_on_ram(void);

/**
 * Set the reclaim request for Relay over cloud
 *
 * @param logger_id 	The logger id that relay want to reclaim
 * @param start_time	The start time of the reclaim range.
 * @param stop_time	The stop time of the reclaim range.
 * @return 0 on success, <0 on error.
 */
int etc_set_reclaim_request_for_relay(char *logger_id, int start_time, int stop_time);

/**
 * Retrieve the reclaim request for a specific logger ID
 *
 * @param logger_id 	The logger id to check if any reclaim request
 * @param request 	The output to store the reclaim request
 * @return 0 on success, <0 on error.
 */

int etc_get_reclaim_request_for_relay_with_logger_id(const char *logger_id,
						     struct etc_device_reclaim_request *request);

/**
 * @brief Check the new configuration if device need to ON/OFF PSM
 *
 * @retval 1 if system need to ON PSM (Exit ALWAYS ON)
 * @retval 2 if system need to OFF PSM (Enter ALWAYS ON)
 * @return 0 if no need to do anything
 */
int etc_device_verify_to_set_psm(const struct etc_config *new_config);
#endif /* ETC_DEVICE_H_ */