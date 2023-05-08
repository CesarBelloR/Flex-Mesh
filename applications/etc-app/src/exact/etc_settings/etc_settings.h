#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include "etc_device.h"
#include <stdint.h>

#define ETC_SETTINGS_DEVICE_ID_LEN (32)
#define ETC_SETTING_FW_VER_LEN	   (8)
#define ETC_SETTING_HW_VER_LEN	   (8)

#define ETC_SETTING_DEVICE_MODE_DEFAULT		    ETC_DEVICE_MODE_LTE_LOGGER
#define ETC_SETTING_POWER_MODE_DEFAULT		    ETC_POWER_MODE_POWER_SAVER
#define ETC_SETTING_ALARM_DIRECTION_DEFAULT	    ETC_ALARM_DIR_GREATER
#define ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT	    900
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT 900
#define ETC_SETTING_TX_INTERVAL_SECS_DEFAULT	    900
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT 86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT	    840
#define ETC_SETTING_TX_DELAY_MSEC_DEFAULT	    29500
#define ETC_SETTING_RX_DURATION_SECS_DEFAULT	    120
#define ETC_SETTING_ALARM_THRESHOLD_DEFAULT	    0

#define ETC_SETTING_LOG_INTERVAL_SECS_MAX	86400
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MAX 86400
#define ETC_SETTING_TX_INTERVAL_SECS_MAX	86400
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MAX 86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_MAX	840
#define ETC_SETTING_TX_DELAY_MSEC_MAX		29500
#define ETC_SETTING_RX_DURATION_SECS_MAX	120
#define ETC_SETTING_ALARM_THRESHOLD_MAX		120

#define ETC_SETTING_LOG_INTERVAL_SECS_MIN	60
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MIN 60
#define ETC_SETTING_TX_INTERVAL_SECS_MIN	60
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MIN 60
#define ETC_SETTING_WAKEUP_EARLY_SECS_MIN	0
#define ETC_SETTING_TX_DELAY_MSEC_MIN		0
#define ETC_SETTING_RX_DURATION_SECS_MIN	30
#define ETC_SETTING_ALARM_THRESHOLD_MIN		-20

int etc_settings_init(void);
void etc_settings_refresh();
void etc_set_hw_version(const char *hw_version);
void etc_set_fw_version(const char *fw_version);
void etc_set_device_id(const char *device_id);
void etc_set_time_last_log(int time);
void etc_set_time_last_tx(int time);

/**
 * @brief Copy the current configuration into config.
 * Load the configuration values from flash if called for the first time.
 * 
 * @param config Pointer to buffer that the current configuration is copied to.
 * @return 0 on success, <0 on error.
*/
int etc_settings_get_config(struct etc_config *config);

/**
 * @brief Update the settings with the values passed in new_config.
 * Individual settings are only updated if the value does not match
 * the current setting's value.
 * 
 * @param new_config Configuration with new values.
*/
void etc_settings_update(const struct etc_config *new_config);
int etc_set_device_mode(enum etc_device_mode mode);
int etc_set_power_mode(enum etc_power_mode_e power);
int etc_set_alarm_direction(enum etc_alarm_direction alarm);
int etc_set_log_interval_secs(uint32_t second);
int etc_set_log_interval_alarm_secs(uint32_t second);
int etc_set_tx_interval_secs(uint32_t second);
int etc_set_tx_interval_alarm_secs(uint32_t second);
int etc_set_wake_early_secs(uint16_t second);
int etc_set_tx_delay_msec(uint16_t msecond);
int etc_set_rx_duration_secs(uint16_t second);
int etc_set_alarm_threshold(uint16_t threshold);
int etc_get_hw_version(char *buf, int buf_len);
int etc_get_fw_version(char *buf, int buf_len);
int etc_get_device_id(char *buf, int buf_len);
int etc_get_time_last_log(void);
int etc_get_time_last_tx(void);
enum etc_device_mode etc_get_device_mode(void);
enum etc_power_mode_e etc_get_power_mode(void);
enum etc_alarm_direction etc_get_alarm_direction(void);
uint32_t etc_get_log_interval_secs(void);
uint16_t etc_get_log_interval_alarm_secs(void);
uint32_t etc_get_tx_interval_secs(void);
uint32_t etc_get_tx_interval_alarm_secs(void);
uint16_t etc_get_wake_early_secs(void);
uint16_t etc_get_tx_delay_msec(void);
uint16_t etc_get_rx_duration_secs(void);
uint16_t etc_get_alarm_threshold(void);

#endif /* ETC_SETTINGS_H__ */