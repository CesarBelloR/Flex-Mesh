#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include "etc_device.h"
#include <stdint.h>

#define ETC_SETTINGS_DEVICE_ID_LEN (32)
#define ETC_SETTING_FW_VER_LEN	   (8)
#define ETC_SETTING_HW_VER_LEN	   (8)

int etc_settings_init(void);
void etc_settings_refresh();
void etc_set_hw_version(const char *hw_version);
void etc_set_fw_version(const char *fw_version);
void etc_set_device_id(const char *device_id);
void etc_set_time_last_log(int time);
void etc_set_time_last_tx(int time);
void etc_set_interval_last_log(int time);
void etc_set_interval_last_tx(int time);
int etc_get_time_last_log(void);
int etc_get_time_last_tx(void);
int etc_get_interval_last_log(void);
int etc_get_interval_last_tx(void);

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