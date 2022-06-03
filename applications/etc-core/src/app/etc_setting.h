/***************************************************************************/
/*!
\file       etc_setting.h
\brief      Setting service

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_SETTING_H_
#define ETC_SETTING_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define ETC_CONFIG_TYPE_SIZE (32)

/* Define a enum to describe about device mode */
typedef enum {
    ETC_DEVICE_MODE_RELAY = 0x00,
    ETC_DEVICE_MODE_LOGGER = 0x01,
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

extern etc_config_t* p_etc_config;
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the Setting Storage
 *
 * @param None
 * @retval None
 */
void etc_setting_init(void);

/** @brief Get the configuration
 *
 * @param config point to where to get the configuration
 * @retval Zero if success
 */
int  etc_setting_get_config(etc_config_t *config);

/** @brief Set the configuration
 *
 * @param config point to where to set the configuration
 * @retval Zero if success
 */
int  etc_setting_set_config(etc_config_t *config);
#endif /* ETC_SETTING_H_ */