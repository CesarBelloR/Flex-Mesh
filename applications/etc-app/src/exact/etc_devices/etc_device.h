#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define ETC_CONFIG_TYPE_SIZE (32)

#define ETC_DEVICE_RECORD_SIZE (36)
#define ETC_DEVICE_NUM_SENSOR (6) // 5 temperatures + 1 humidity

/* Define a enum to describe about device mode */
typedef enum {
    ETC_DEVICE_MODE_RELAY = 0x01,
    ETC_DEVICE_MODE_LOGGER = 0x02,
} etc_device_mode_e;

/* Define a enum to describe about radio mode */
typedef enum {
    ETC_RADIO_MODE_LTE = 0x00,
    ETC_RADIO_MODE_LORA = 0x01,
    ETC_RADIO_MODE_BLE = 0x02,
    ETC_RADIO_MODE_LORAWAN = 0x03,
} etc_radio_mode_e;

/* Define a enum to describe about power mode */
typedef enum {
    ETC_POWER_MODE_POWER_SAVER = 0x00,
    ETC_POWER_MODE_AWLAYS_ON = 0x01,
    ETC_POWER_MODE_HIBERNATE = 0x02,
} etc_power_mode_e;

/* Define a type for log interval in seconds */
typedef int etc_log_interval_seconds_t;

/* Define a type for log interval alarm in seconds */
typedef int etc_log_interval_alarm_seconds_t;

/* Define a type for transmit interval in seconds */
typedef int etc_tx_interval_seconds_t;

/* Define a type for transmit interval alarm in seconds */
typedef int etc_tx_interval_alarm_seconds_t;

/* Define a type for wake up early in seconds */
typedef int etc_wake_early_seconds_t;

/* Define a type for transmit delay in mseconds */
typedef int etc_tx_delay_mseconds_t;

/* Define a type for receiving duration in seconds */
typedef int etc_rx_duration_seconds_t;

/* Define a type for alarm threshold such as C, PSI, PSF */
typedef int etc_alarm_threshold_t;

/* Define a enum to describe about alarm condition (direction) */
typedef enum {
    ETC_ALARM_DIR_GREATER = 0x00,
    ETC_ALARM_DIR_LESS = 0x01,
} etc_alarm_direction_e;

enum {
    ETC_CONFIG_ID = 0x01,
    ETC_RECORD_STAT = 0x02,
    ETC_RECORD_HEADER = 0x03,
    ETC_SETTING_HW_VERSION_ID = 0x100,
    ETC_SETTING_FW_VERSION_ID,
    ETC_SETTING_DEVICE_ID,
    ETC_SETTING_TIME_MEASURE_INTERVAL_ID,
    ETC_SETTING_TIME_TRANSMISSION_INTERVAL_ID,
};

typedef union {
    uint8_t bytes[ETC_CONFIG_TYPE_SIZE];
    struct {
        etc_device_mode_e device_mode;
        etc_radio_mode_e radio_mode;
        etc_power_mode_e power_mode;
        etc_alarm_direction_e alarm_direction;
        etc_log_interval_seconds_t log_interval_secs;
        etc_log_interval_alarm_seconds_t log_interval_alarm_secs;
        etc_tx_interval_seconds_t tx_interval_secs;
        etc_tx_interval_alarm_seconds_t tx_interval_alarm_secs;
        etc_wake_early_seconds_t wake_early_secs;
        etc_tx_delay_mseconds_t tx_delay_msec;
        etc_rx_duration_seconds_t rx_duration_secs;
        etc_alarm_threshold_t alarm_threshold;
    };
} etc_config_t;

typedef union {
    uint8_t data[ETC_DEVICE_RECORD_SIZE];
    struct {
        float battery;
        uint8_t flag; // Counter or PCB Fault
        uint32_t timestamp;
        float sensor[ETC_DEVICE_NUM_SENSOR];
    };
} etc_device_record_t;

void etc_device_init(void);

int etc_device_write_setting(int setting_id, void* setting, int setting_size);
int etc_device_read_setting(int setting_id, void* setting, int setting_size);
int etc_device_write_record(etc_device_record_t* record);
int etc_device_read_record(etc_device_record_t* record, int index);

#endif /* ETC_DEVICE_H_ */