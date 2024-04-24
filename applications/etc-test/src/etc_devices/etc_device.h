#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/* Define a enum to describe about device mode */
enum etc_device_mode {
	ETC_DEVICE_MODE_RELAY = 0x00,
	ETC_DEVICE_MODE_LORA_LOGGER = 0x01,
	ETC_DEVICE_MODE_LTE_LOGGER = 0x02,
	ETC_DEVICE_MODE_BLE = 0x03,
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
	ETC_ADC_TEMPERATURE_REFERENCE_ID,
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
	ETC_CALIBRATION_OFFSET_ID = 0xFF0,
	ETC_CALIBRATION_RAWHIGH_ID,
	ETC_CALIBRATION_REF_ID,
	ETC_RECORD_HEADER = 0x1000,

};

int etc_device_write_setting(uint16_t setting_id, void *setting, int setting_size);
int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size);

/**
 * Write a setting value and read back the value from flash.
 * 
 * @param setting_id ID of the setting to write @ref "enum etc_setting_id"
 * @param setting Pointer to the setting data to write
 * @param setting_size Size of the data to write
 * @param setting_readback Buffer for the read back value to be stored
 * @param setting_readback_size Size of the read back buffer
 * 
 * @retval 0 success
 * @retval -1 fail
*/
int etc_device_write_read_setting(uint16_t setting_id, void *setting, int setting_size,
				  void *setting_readback, int setting_readback_size);

#endif /* ETC_DEVICE_H_ */