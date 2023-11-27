#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_

#include "events/sensor_event.h"
#include "etc_sensor.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ETC_CONFIG_TYPE_SIZE   (32)
#define ETC_DEVICE_RECORD_SIZE (36)
#define ETC_DEVICE_NUM_SENSOR  (6) // 5 temperatures + 1 humidity
#define ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_OFFSET_HOUR (16)

#define IMG_PUBKEY_ID_LEN	4

/* Define an enum to describe the job of logger currently */
enum etc_logger_job {
	ETC_LOGGER_JOB_LOG = 0x00,
	ETC_LOGGER_JOB_TX,
	ETC_LOGGER_JOB_BOTH,
};

/* Define an enum to describe the sub job for transmit event */
enum etc_transmit_sub_job {
	ETC_TRANSMIT_NORMAL,
	ETC_TRANSMIT_SYNC_CLOUD_LORA,
	ETC_TRANSMIT_SYNC_MAGNET,
};

/* Define a enum to describe about device mode */
enum etc_device_mode {
	ETC_DEVICE_MODE_RELAY = 0x00,
	ETC_DEVICE_MODE_LORA_LOGGER = 0x01,
	ETC_DEVICE_MODE_LTE_LOGGER = 0x02,
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
union etc_device_record_header { // It will always change  NVS
	uint8_t header;
	struct {
		uint8_t ready: 1;
		uint8_t ack: 1;
		uint8_t wait: 1;
		uint8_t unused: 3;
	};
};

struct etc_device_record_index { // Constant in flash until the index is override (exflash)
	uint8_t sector_idx;
	uint8_t element_idx;
};

struct etc_device_record_table {
	struct etc_device_record_index oldest;
	struct etc_device_record_index newest;
	uint16_t total;
	uint16_t last_nack_record_id;
};

/**
 * @brief Define a callback function for record reading
 * 
 */
typedef int (*etc_device_record_reading_callback)(uint16_t record_id, void* user_data);

enum etc_setting_id {
	ETC_CONFIG_ID = 0x01,
	ETC_RECORD_STAT = 0x02,
	ETC_RECORD_RECLAIM = 0x03,
	ETC_SERIAL_NUMBER_TYPE = 0x04,
	ETC_SERIAL_NUMBER_ID = 0x05,
	ETC_PSK_ID = 0x06,
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
	ETC_CALIBRATION_OFFSET_ID = 0xFF0,
	ETC_CALIBRATION_RAWHIGH_ID,
	ETC_CALIBRATION_REF_ID,
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
	uint16_t wake_early_secs;
	uint16_t tx_delay_msec;
	uint16_t rx_duration_secs;
	uint16_t alarm_threshold;
};

union etc_device_record {
	uint8_t data[ETC_DEVICE_RECORD_SIZE];
	struct {
		float battery;
		float sensor[ETC_DEVICE_NUM_SENSOR];
		uint32_t timestamp;
		uint32_t flag; /* Use 8 bytes to save battery status */
	};
};

/* Assert to verify the record size must fit the macro ETC_DEVICE_RECORD_SIZE */
BUILD_ASSERT(ETC_DEVICE_RECORD_SIZE >= sizeof(union etc_device_record));

void etc_device_nvs_init(void);
void etc_device_init(void);

int etc_device_write_setting(uint16_t setting_id, const void *setting, int setting_size);
int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size);

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

int etc_device_write_record_sensor(struct sensor_data *sensor);
int etc_device_write_record(union etc_device_record *record);

/** Get the next-in-line (unack'd) measurement record. If a reclaim is active,
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
int etc_device_set_ack_record(int record_id);
bool etc_device_is_logger_lora(void);
int etc_device_get_rx_timeout(void);
int etc_device_get_log_interval_second(void);
int etc_device_get_tx_interval_second(void);
int etc_device_get_tx_probe_second(void);

/**
 * Get the current number of not acknowledged samples (nacks) stored on the
 * device.
 * 
 * @return Number of not acknowledged samples.
*/
uint16_t etc_device_nack_count(void);

enum etc_device_mode etc_device_get_mode(void);
void etc_device_set_job(enum etc_logger_job job);
enum etc_logger_job etc_device_get_job(void);
void etc_device_set_transmit_sub_job(enum etc_transmit_sub_job job);
enum etc_transmit_sub_job etc_device_get_transmit_sub_job(void);
const struct device* etc_device_get_record(void);
size_t etc_device_get_record_size(void);
off_t etc_device_get_record_offset(void);
size_t etc_device_get_record_max_element_index(void);
size_t etc_device_get_record_max_sector_index(void);
size_t etc_device_get_record_element_size(void);
struct etc_device_record_table etc_device_get_record_status(void);
int etc_device_get_record_header(uint8_t element, uint8_t sector, union etc_device_record_header *header);
int etc_device_reclaim_record(int start_time, int stop_time);
int etc_device_reclaim_work(int start_time, int stop_time);
int etc_device_erase_cfg(void);
uint16_t etc_device_get_tx_logger_lora_offset_mins(void);
uint16_t etc_device_get_tx_no_probe_offset_mins(void);

/** Get the current image's pubkey ID. The public key ID is the first 4 bytes
 * of the public key hash that is stored in the image's TLV.
 * 
 * @param pubkey_id Buffer to hold the pubkey id. Needs to be at least 4 bytes
 * long. See @ref IMG_PUBKEY_ID_LEN.
 * @param pubkey_id_len Size of the buffer.
 * 
 * @return Number of bytes written to pubkey_id 
*/
int etc_device_get_img_pubkey_id(uint8_t *pubkey_id, uint8_t pubkey_id_len);
#endif /* ETC_DEVICE_H_ */