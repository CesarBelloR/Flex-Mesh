#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_

#include "events/sensor_event.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ETC_CONFIG_TYPE_SIZE   (32)
#define ETC_DEVICE_RECORD_SIZE (36)
#define ETC_DEVICE_NUM_SENSOR  (6) // 5 temperatures + 1 humidity

/* Define a enum to describe about device mode */
enum etc_device_mode {
	ETC_DEVICE_MODE_RELAY = 0x01,
	ETC_DEVICE_MODE_LOGGER = 0x02,
};

/* Define a enum to describe about radio mode */
enum etc_radio_mode {
	ETC_RADIO_MODE_LTE = 0x00,
	ETC_RADIO_MODE_LORA = 0x01,
	ETC_RADIO_MODE_BLE = 0x02,
	ETC_RADIO_MODE_LORAWAN = 0x03,
};

/* Define a enum to describe about power mode */
enum etc_power_mode_e {
	ETC_POWER_MODE_POWER_SAVER = 0x00,
	ETC_POWER_MODE_AWLAYS_ON = 0x01,
	ETC_POWER_MODE_HIBERNATE = 0x02,
};

/* Define a enum to describe about alarm condition (direction) */
enum etc_alarm_direction {
	ETC_ALARM_DIR_GREATER = 0x00,
	ETC_ALARM_DIR_LESS = 0x01,
};

/**
 * @brief Define a callback function for record reading
 * 
 */
typedef int (*etc_device_record_reading_callback)(uint16_t record_id, void* user_data);

enum {
	ETC_CONFIG_ID = 0x01,
	ETC_RECORD_STAT = 0x02,
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
	ETC_RECORD_HEADER = 0x1000,
};

struct etc_config {
	enum etc_device_mode device_mode;
	enum etc_radio_mode radio_mode;
	enum etc_power_mode_e power_mode;
	enum etc_alarm_direction alarm_direction;
	uint32_t log_interval_secs;
	uint16_t log_interval_alarm_secs;
	uint16_t tx_interval_secs;
	uint16_t tx_interval_alarm_secs;
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
		uint32_t flag; // Counter or PCB Fault
	};
};

/* Assert to verify the record size must fit the macro ETC_DEVICE_RECORD_SIZE */
BUILD_ASSERT(ETC_DEVICE_RECORD_SIZE >= sizeof(union etc_device_record));

void etc_device_init(void);

int etc_device_write_setting(uint16_t setting_id, void *setting, int setting_size);
int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size);
int etc_device_write_record_sensor(struct sensor_data *sensor);
int etc_device_write_record(union etc_device_record *record);
int etc_device_read_record(union etc_device_record *record);
int etc_device_set_ack_record(int record_id);
enum etc_device_mode etc_device_get_mode(void);
int etc_device_get_rx_timeout(void);
int etc_device_find_nack(etc_device_record_reading_callback reading_callback, void* data);

#endif /* ETC_DEVICE_H_ */