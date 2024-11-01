#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include "etc_device.h"
#include <stdint.h>

enum etc_serial_number_types {
	ETC_SERIAL_TYPE_HW_INFO = 0,
	ETC_SERIAL_TYPE_EXACT_INFO
};

#define ETC_SETTINGS_DEVICE_ID_LEN  (32)
#define ETC_SETTING_FW_VER_LEN	    (8)
#define ETC_SETTING_HW_VER_LEN	    (8)
#define ETC_SETTING_RELAY_ICCID_LEN (4)
#define ETC_SETTING_PSK_LEN	    CONFIG_LWM2M_SECURITY_KEY_SIZE

#define ETC_SETTING_SERIAL_NUMBER_DEFAULT	    (enum etc_serial_number_types) CONFIG_SERIAL_NUMBER_TYPE
#define ETC_SETTING_DEVICE_MODE_DEFAULT		    ETC_DEVICE_MODE_LTE_LOGGER
#define ETC_SETTING_POWER_MODE_DEFAULT		    ETC_POWER_MODE_INTERVAL
#define ETC_SETTING_ALARM_DIRECTION_DEFAULT	    ETC_ALARM_DIR_GREATER
#define ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT	    900
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT 900
#define ETC_SETTING_TX_INTERVAL_SECS_DEFAULT	    900
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT 86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT	    20
#define ETC_SETTING_RX_DURATION_SECS_DEFAULT	    120
#define ETC_SETTING_ALARM_THRESHOLD_DEFAULT	    0
#define ETC_SETTING_GNSS_INTERVAL_SECS_DEFAULT	    (60 * 60 * 24 * 2) /* 2 days */
#define ETC_SETTING_GNSS_TIMEOUT_SECS_DEFAULT	    CONFIG_MODEM_MODULE_GNSS_TIMEOUT_S

#define ETC_SETTING_POWER_MODE_MIN		ETC_POWER_MODE_INTERVAL
#define ETC_SETTING_LOG_INTERVAL_SECS_MAX	86400
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MAX 86400
#define ETC_SETTING_TX_INTERVAL_SECS_MAX	86400
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MAX 86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_MAX	770
#define ETC_SETTING_TX_DELAY_MSEC_MAX		29500
#define ETC_SETTING_RX_DURATION_SECS_MAX	120
#define ETC_SETTING_ALARM_THRESHOLD_MAX		120
#define ETC_SETTING_LTE_PROBE_OFFSET_SECS_MAX	3540
#define ETC_SETTING_GNSS_INTERVAL_SECS_MAX	(60 * 60 * 24 * 7) /* 7 days */

#define ETC_SETTING_POWER_MODE_MAX		ETC_POWER_MODE_PROBE
#define ETC_SETTING_LOG_INTERVAL_SECS_MIN	60
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MIN 60
#define ETC_SETTING_TX_INTERVAL_SECS_MIN	60
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MIN 60
#define ETC_SETTING_WAKEUP_EARLY_SECS_MIN	0
#define ETC_SETTING_TX_DELAY_MSEC_MIN		0
#define ETC_SETTING_TX_DELAY_MSEC_MIN_LTE	5000
#define ETC_SETTING_RX_DURATION_SECS_MIN	30
#define ETC_SETTING_ALARM_THRESHOLD_MIN		-20
#define ETC_SETTING_LTE_PROBE_OFFSET_SECS_MIN	0
#define ETC_SETTING_GNSS_INTERVAL_SECS_MIN	(60 * 60) /* 1 hour */
#define ETC_SETTING_FUNCTIONAL_TEST_RSRP_DEFAULT (-90)
#define ETC_SETTING_TX_PROBE_SECS 21600
#define ETC_SETTING_SOFT_WATCHDOG_OFFSET_SECS	900

/* Buffer for calculation of allowable wakeup early max value during runtime */
#define ETC_SETTING_WAKEUP_EARLY_SECS_BUFFER    10

int etc_settings_init(void);
void etc_set_hw_version(const char *hw_version);
void etc_set_fw_version(const char *fw_version);
void etc_set_device_id(const char *device_id);

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
int etc_set_tx_probe_secs(uint32_t second);
int etc_set_wake_early_secs(uint16_t second);
int etc_set_tx_delay_msec(uint16_t msecond);
int etc_set_rx_duration_secs(uint16_t second);
int etc_set_alarm_threshold(uint16_t threshold);
int etc_set_serial_number_type(enum etc_serial_number_types type);
int etc_set_relay_iccid(const char *iccid);

/**
 * @brief Sets the LoRa probe offset seconds.
 * This function updates the offset time used for LoRa probing operations.
 *
 * @param second The offset time in seconds for LoRa probes. The value must be
 *               a multiple of 900 and less than or equal to 2700 seconds.
 * @return 0 on success, <0 on error.
 */
int etc_set_lora_probe_offset_secs(uint16_t second);

/**
 * @brief Sets the LTE probe offset seconds.
 * This function updates the offset time used for LTE probing operations.
 *
 * @param second The offset time in seconds for LTE probes. The value must be
 *               a multiple of 900 and less than or equal to 2700 seconds.
 * @return 0 on success, <0 on error.
 */
int etc_set_lte_probe_offset_secs(uint16_t second);

/** 
 * Set a new GPS request interval in seconds and save the new interval to non-volatile
 * memory.
 *
 * @param interval_secs New GNSS interval in seconds.
 *
 * @retval 0 success
 * @retval <0 error
*/
int etc_set_gnss_interval_secs(uint32_t interval_secs);

/**
 * Set the GNSS timeout value and save to non-volatile memory. If no location is found
 * through GNSS when this timeout expires, GNSS is turned off.
 *
 * @param timeout_secs GNSS timeout in seconds.
 *
 * @retval 0 success
 * @retval <0 error
*/
int etc_set_gnss_timeout_secs(uint16_t timeout_secs);

/**
 * Set the Rr value for temperature compensation
 *
 * @param value: the input Rr that will store to @ref ETC_ADC_TEMPERATURE_REFERENCE 
 * @return 0 on success, <0 on error.
 */
int etc_set_rr_value(int value);

/**
 * Set RSRP value used by functional test
 *
 * @param value: the functional test rsrp value
 * @return 0 on success, <0 on error.
 */
int etc_set_functional_test_rsrp_value(int16_t value);

int etc_get_hw_version(char *buf, int buf_len);
int etc_get_fw_version(char *buf, int buf_len);
int etc_get_hw_id(char *buf, int buf_len);
int etc_get_device_id(char *buf, int buf_len);

/**
 * Check if the device ID is set to the default value.
 *
 * @retval true if value is default.
 * @retval false if value is not default.
 */
bool etc_device_id_is_default(void);

/**
 * @brief Get the PSK key used for cloud authentication
 *
 * @param psk_buf Buffer to save the PSK key. Buffer must be
 * of size ETC_SETTING_PSK_LEN or larger.
 * @param buf_len Size of psk_buf
 *
 * @return Size of PSK (number of bytes written to psk_buf)
 */
int etc_get_psk(uint8_t *psk_buf, uint8_t buf_len);

enum etc_device_mode etc_get_device_mode(void);
enum etc_power_mode_e etc_get_power_mode(void);
enum etc_alarm_direction etc_get_alarm_direction(void);
uint32_t etc_get_log_interval_secs(void);
uint16_t etc_get_log_interval_alarm_secs(void);
uint32_t etc_get_tx_interval_secs(void);
uint32_t etc_get_tx_interval_alarm_secs(void);
uint32_t etc_get_tx_probe_secs(void);
uint16_t etc_get_wake_early_secs(void);
uint16_t etc_get_tx_delay_msec(void);
uint16_t etc_get_rx_duration_secs(void);
uint16_t etc_get_alarm_threshold(void);
int etc_get_relay_iccid(char *buf, int buf_len);

/**
 * @brief Retrieves the current LoRa probe offset seconds.
 * This function returns the currently set offset time used for LoRa probing operations.
 *
 * @return uint16_t The current LoRa probe offset time in seconds.
 */
uint16_t etc_get_lora_probe_offset_secs(void);

/**
 * @brief Retrieves the current LTE probe offset seconds.
 * This function returns the currently set offset time used for LTE probing operations.
 *
 * @return uint16_t The current LTE probe offset time in seconds.
 */
uint16_t etc_get_lte_probe_offset_secs(void);
 
/** 
 * Get the current GNSS request interval in seconds.
 *
 * @return Current GNSS request interval in seconds.
*/
uint32_t etc_get_gnss_interval_secs(void);

/**
 * Get the current GNSS timeout value.
 *
 * @return Current GNSS timoeut in seconds.
*/
uint16_t etc_get_gnss_timeout_secs(void);

/**
 * Get the Rr value for temperature compensation
 *
 * @return The Rr value that stores in NVS @ref ETC_ADC_TEMPERATURE_REFERENCE
 */
uint16_t etc_get_rr_value(void);

/**
 * Get the soft watchdog timeout in seconds to support ticket HW-1724.
 *
 * @return The timeout in seconds or -1 when invalid mode.
 */
int etc_get_soft_watchdog_timeout_secs(void);

/**
 * Get RSRP value used by functional test
 *
 * @return the functional test rsrp value
 */
int16_t etc_get_functional_test_rsrp_value(void);

#endif /* ETC_SETTINGS_H__ */
