#include <stdio.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_settings, CONFIG_ETC_SETTINGS_LOG_LEVEL);
#include "app_version.h"
#include "etc_device.h"
#include "etc_settings.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define SETTINGS_CONFIG_READY_CODE 0xCAFEBEEF
#define SETTINGS_HW_VERSION	   ETC_SETTING_HW_VERSION_ID
#define SETTINGS_FW_VERSION	   ETC_SETTING_FW_VERSION_ID
#define SETTINGS_DEVICE_ID	   ETC_SETTING_DEVICE_ID

#define ETC_SETTING_DEVICE_MODE_DEFAULT		    	ETC_DEVICE_MODE_LTE_LOGGER
#define ETC_SETTING_POWER_MODE_DEFAULT		    	ETC_POWER_MODE_POWER_SAVER
#define ETC_SETTING_ALARM_DIRECTION_DEFAULT	    	ETC_ALARM_DIR_GREATER
#define ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT	    	900
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT 	900
#define ETC_SETTING_TX_INTERVAL_SECS_DEFAULT	    	900
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT 	86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT	    	840
#define ETC_SETTING_TX_DELAY_MSEC_DEFAULT	    	29500
#define ETC_SETTING_RX_DURATION_SECS_DEFAULT	    	120
#define ETC_SETTING_ALARM_THRESHOLD_DEFAULT	    	0

#define ETC_SETTING_LOG_INTERVAL_SECS_MAX		86400
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MAX		86400
#define ETC_SETTING_TX_INTERVAL_SECS_MAX		86400
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MAX		86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_MAX		840
#define ETC_SETTING_TX_DELAY_MSEC_MAX			29500
#define ETC_SETTING_RX_DURATION_SECS_MAX		120
#define ETC_SETTING_ALARM_THRESHOLD_MAX			120

#define ETC_SETTING_LOG_INTERVAL_SECS_MIN		60
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MIN		60
#define ETC_SETTING_TX_INTERVAL_SECS_MIN		60
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MIN		60
#define ETC_SETTING_WAKEUP_EARLY_SECS_MIN		0
#define ETC_SETTING_TX_DELAY_MSEC_MIN			0
#define ETC_SETTING_RX_DURATION_SECS_MIN		30
#define ETC_SETTING_ALARM_THRESHOLD_MIN			-20

static char saved_hw_version[ETC_SETTING_HW_VER_LEN];
static char saved_fw_version[ETC_SETTING_FW_VER_LEN];
static char saved_device_id[ETC_SETTINGS_DEVICE_ID_LEN];
static int saved_last_log_time;
static int saved_last_tx_time;
static int saved_last_log_interval;
static int saved_last_tx_interval;
static char tmp_saved_value[ETC_SETTINGS_DEVICE_ID_LEN];
static int flag_etc_config_load;
struct etc_config etc_cfg;

K_MUTEX_DEFINE(setting_mutex);

void etc_settings_refresh()
{
	k_mutex_lock(&setting_mutex, K_FOREVER);
	saved_last_log_time = -1;
	saved_last_tx_time = -1;
	saved_last_log_interval = -1;
	saved_last_tx_interval = -1;
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
	memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
	etc_device_read_setting(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	etc_device_read_setting(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	etc_device_read_setting(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	k_mutex_unlock(&setting_mutex);
}

void etc_set_hw_version(const char *hw_version)
{
	k_mutex_lock(&setting_mutex, K_FOREVER);
	strncpy(saved_hw_version, hw_version, ETC_SETTING_HW_VER_LEN);
	etc_device_write_setting(SETTINGS_HW_VERSION, (char *)hw_version, ETC_SETTING_HW_VER_LEN);
	k_mutex_unlock(&setting_mutex);
}

void etc_set_fw_version(const char *fw_version)
{
	k_mutex_lock(&setting_mutex, K_FOREVER);
	strncpy(saved_fw_version, fw_version, ETC_SETTING_FW_VER_LEN);
	etc_device_write_setting(SETTINGS_FW_VERSION, (char *)fw_version, ETC_SETTING_FW_VER_LEN);
	k_mutex_unlock(&setting_mutex);
}

void etc_set_device_id(const char *device_id)
{
	k_mutex_lock(&setting_mutex, K_FOREVER);
	strncpy(saved_device_id, device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	etc_device_write_setting(SETTINGS_DEVICE_ID, (char *)device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	k_mutex_unlock(&setting_mutex);
}

int etc_get_hw_version(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&setting_mutex, K_FOREVER);
	copy_size = ETC_SETTING_HW_VER_LEN < buf_len ? ETC_SETTING_HW_VER_LEN : buf_len;
	memcpy(buf, saved_hw_version, copy_size);
	k_mutex_unlock(&setting_mutex);
	return copy_size;
}

int etc_get_fw_version(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&setting_mutex, K_FOREVER);
	copy_size = ETC_SETTING_FW_VER_LEN < buf_len ? ETC_SETTING_FW_VER_LEN : buf_len;
	memcpy(buf, saved_fw_version, copy_size);
	k_mutex_unlock(&setting_mutex);
	return copy_size;
}

int etc_get_device_id(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&setting_mutex, K_FOREVER);
	copy_size = ETC_SETTINGS_DEVICE_ID_LEN < buf_len ? ETC_SETTINGS_DEVICE_ID_LEN : buf_len;
	memcpy(buf, saved_device_id, copy_size);
	k_mutex_unlock(&setting_mutex);
	return copy_size;
}

int etc_settings_init(void)
{
	int ret;
	bool flag_config_set_default = false;
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
	memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
	ret = etc_device_read_setting(SETTINGS_HW_VERSION, saved_hw_version,
				      ETC_SETTING_HW_VER_LEN);
	if (ret) {
		etc_set_hw_version("0.0.0");
	}

	ret = etc_device_read_setting(SETTINGS_FW_VERSION, saved_fw_version,
				      ETC_SETTING_FW_VER_LEN);
	if (ret) {
		etc_set_fw_version(APP_VERSION_STR);
	}

	ret = etc_device_read_setting(SETTINGS_DEVICE_ID, saved_device_id,
				      ETC_SETTINGS_DEVICE_ID_LEN);
	if (ret) {
		snprintf(tmp_saved_value, sizeof(tmp_saved_value), "%08X%08X", NRF_FICR->DEVICEID[0],
			 NRF_FICR->DEVICEID[1]);
		LOG_INF("Set default device ID %s", tmp_saved_value);
		etc_set_device_id(tmp_saved_value);
	}

	ret = etc_device_read_setting(ETC_SETTING_LAST_LOG_TIME_ID, (char *)&saved_last_log_time, sizeof(int));
	if (ret)
	{
		saved_last_log_time = -1;
		etc_set_time_last_log(-1);
	}

	ret = etc_device_read_setting(ETC_SETTING_LAST_TX_TIME_ID, (char *)&saved_last_tx_time, sizeof(int));
	if (ret)
	{
		saved_last_tx_time = -1;
		etc_set_time_last_tx(-1);
	}

	ret = etc_device_read_setting(ETC_SETTING_LAST_LOG_INTERVAL_ID, (char *)&saved_last_log_interval, sizeof(int));
	if (ret)
	{
		saved_last_log_interval = -1;
		etc_set_time_last_log(-1);
	}

	ret = etc_device_read_setting(ETC_SETTING_LAST_TX_INTERVAL_ID, (char *)&saved_last_tx_interval, sizeof(int));
	if (ret)
	{
		saved_last_tx_interval = -1;
		etc_set_time_last_tx(-1);
	}

	ret = etc_device_read_setting(ETC_SETTING_DEVICE_MODE_ID, &etc_cfg.device_mode,
				      sizeof(etc_cfg.device_mode));
	if (ret) {
		etc_set_device_mode(ETC_SETTING_DEVICE_MODE_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_POWER_MODE_ID, &etc_cfg.power_mode,
				      sizeof(etc_cfg.power_mode));
	if (ret) {
		etc_set_power_mode(ETC_SETTING_POWER_MODE_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_ALARM_DIRECTION_ID, &etc_cfg.alarm_direction,
				      sizeof(etc_cfg.alarm_direction));
	if (ret) {
		etc_set_alarm_direction(ETC_SETTING_ALARM_DIRECTION_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_LOG_INTERVAL_SECS_ID, &etc_cfg.log_interval_secs,
				      sizeof(etc_cfg.log_interval_secs));
	if (ret) {
		etc_set_log_interval_secs(ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_ID,
				      &etc_cfg.log_interval_alarm_secs,
				      sizeof(etc_cfg.log_interval_alarm_secs));
	if (ret) {
		etc_set_log_interval_alarm_secs(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_TX_INTERVAL_SECS_ID, &etc_cfg.tx_interval_secs,
				      sizeof(etc_cfg.tx_interval_secs));
	if (ret) {
		etc_set_tx_interval_secs(ETC_SETTING_TX_INTERVAL_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_ID,
				      &etc_cfg.tx_interval_alarm_secs,
				      sizeof(etc_cfg.tx_interval_alarm_secs));
	if (ret) {
		etc_set_tx_interval_alarm_secs(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_WAKEUP_EARLY_SECS_ID, &etc_cfg.wake_early_secs,
				      sizeof(etc_cfg.wake_early_secs));
	if (ret) {
		etc_set_wake_early_secs(ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_TX_DELAY_MSEC_ID, &etc_cfg.tx_delay_msec,
				      sizeof(etc_cfg.tx_delay_msec));
	if (ret) {
		etc_set_tx_delay_msec(ETC_SETTING_TX_DELAY_MSEC_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_RX_DURATION_SECS_ID, &etc_cfg.rx_duration_secs,
				      sizeof(etc_cfg.rx_duration_secs));
	if (ret) {
		etc_set_rx_duration_secs(ETC_SETTING_RX_DURATION_SECS_DEFAULT);
	}

	ret = etc_device_read_setting(ETC_SETTING_ALARM_THRESHOLD_ID, &etc_cfg.alarm_threshold,
				      sizeof(etc_cfg.alarm_threshold));
	if (ret) {
		etc_set_alarm_threshold(ETC_SETTING_ALARM_THRESHOLD_DEFAULT);
	}
	
	LOG_DBG("Load setting successfully");
	return 0;
}

void etc_set_time_last_log(int time) {
	k_mutex_lock(&setting_mutex, K_FOREVER);
	saved_last_log_time = time;
	etc_device_write_setting(ETC_SETTING_LAST_LOG_TIME_ID, (char *)&saved_last_log_time, sizeof(int));
	k_mutex_unlock(&setting_mutex);
	
}

void etc_set_time_last_tx(int time) {
	k_mutex_lock(&setting_mutex, K_FOREVER);
	saved_last_tx_time = time;
	etc_device_write_setting(ETC_SETTING_LAST_TX_TIME_ID, (char *)&saved_last_tx_time, sizeof(int));
	k_mutex_unlock(&setting_mutex);
}

void etc_set_interval_last_log(int time) {
	k_mutex_lock(&setting_mutex, K_FOREVER);
	saved_last_log_interval = time;
	etc_device_write_setting(ETC_SETTING_LAST_LOG_INTERVAL_ID, (char *)&saved_last_log_interval, sizeof(int));
	k_mutex_unlock(&setting_mutex);
}

void etc_set_interval_last_tx(int time) {
	k_mutex_lock(&setting_mutex, K_FOREVER);
	saved_last_tx_interval = time;
	etc_device_write_setting(ETC_SETTING_LAST_TX_INTERVAL_ID, (char *)&saved_last_tx_interval, sizeof(int));
	k_mutex_unlock(&setting_mutex);
}

int etc_get_time_last_log(void) {
	int time = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	time = saved_last_log_time;
	k_mutex_unlock(&setting_mutex);
	return time;
}

int etc_get_time_last_tx(void) {
	int time = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	time = saved_last_tx_time;
	k_mutex_unlock(&setting_mutex);
	return time;
}

int etc_get_interval_last_log(void) {
	int time = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	time = saved_last_log_interval;
	k_mutex_unlock(&setting_mutex);
	return time;
}

int etc_get_interval_last_tx(void) {
	int time = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	time = saved_last_tx_interval;
	k_mutex_unlock(&setting_mutex);
	return time;
}

int etc_settings_get_config(struct etc_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}

	memcpy(config, &etc_cfg, sizeof(etc_cfg));
	return 0;
}

void etc_settings_update(const struct etc_config *new_config)
{
	int rc = 1;
	if (etc_cfg.device_mode != new_config->device_mode) {
		rc = etc_set_device_mode(new_config->device_mode);
	}
	if (etc_cfg.power_mode != new_config->power_mode) {
		rc = etc_set_power_mode(new_config->power_mode);
	} 
	if (etc_cfg.log_interval_secs != new_config->log_interval_secs) {
		rc = etc_set_log_interval_secs(new_config->log_interval_secs);
	}
	if (etc_cfg.log_interval_alarm_secs != new_config->log_interval_alarm_secs) {
		rc = etc_set_log_interval_alarm_secs(new_config->log_interval_alarm_secs);
	}
	if (etc_cfg.tx_interval_secs != new_config->tx_interval_secs) {
		rc = etc_set_tx_interval_secs(new_config->tx_interval_secs);
	}
	if (etc_cfg.tx_interval_alarm_secs != new_config->tx_interval_alarm_secs) {
		rc = etc_set_tx_interval_alarm_secs(new_config->tx_interval_alarm_secs);
	}
	if (etc_cfg.wake_early_secs != new_config->wake_early_secs) {
		rc = etc_set_wake_early_secs(new_config->wake_early_secs);
	} 
	if (etc_cfg.tx_delay_msec != new_config->tx_delay_msec) {
		rc = etc_set_tx_delay_msec(new_config->tx_delay_msec);
	} 
	if (etc_cfg.rx_duration_secs != new_config->rx_duration_secs) {
		rc = etc_set_rx_duration_secs(new_config->rx_duration_secs);
	} 
	if (rc == 1) {
		LOG_DBG("No value changed");
	} else if (rc == 0) {
		LOG_DBG("Value changed success");
	} else {
		LOG_ERR("Error changing value: %d", rc);
	}
}

int etc_set_device_mode(enum etc_device_mode mode)
{
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.device_mode = mode;
	rc = etc_device_write_setting(ETC_SETTING_DEVICE_MODE_ID, &etc_cfg.device_mode,
				      sizeof(etc_cfg.device_mode));
	if (rc == 0) {
		LOG_DBG("set %u", mode);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_power_mode(enum etc_power_mode_e power)
{
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.power_mode = power;
	rc = etc_device_write_setting(ETC_SETTING_POWER_MODE_ID, &etc_cfg.power_mode,
				      sizeof(etc_cfg.power_mode));
	k_mutex_unlock(&setting_mutex);
	if (rc == 0) {
		LOG_DBG("set %u", power);
	}
	return rc;
}

int etc_set_alarm_direction(enum etc_alarm_direction alarm)
{
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.alarm_direction = alarm;
	rc = etc_device_write_setting(ETC_SETTING_ALARM_DIRECTION_ID, &etc_cfg.alarm_direction,
				      sizeof(etc_cfg.alarm_direction));
	if (rc == 0) {
		LOG_DBG("set %u", alarm);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_log_interval_secs(uint32_t second)
{
	if ((second > ETC_SETTING_LOG_INTERVAL_SECS_MAX) ||
	    (second < ETC_SETTING_LOG_INTERVAL_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.log_interval_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_LOG_INTERVAL_SECS_ID, &etc_cfg.log_interval_secs,
				      sizeof(etc_cfg.log_interval_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_log_interval_alarm_secs(uint32_t second)
{
	if ((second > ETC_SETTING_LOG_INTERVAL_ALARM_SECS_MAX) ||
	    (second < ETC_SETTING_LOG_INTERVAL_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.log_interval_alarm_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_ID,
				      &etc_cfg.log_interval_alarm_secs,
				      sizeof(etc_cfg.log_interval_alarm_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_tx_interval_secs(uint32_t second)
{
	if ((second > ETC_SETTING_TX_INTERVAL_SECS_MAX) ||
	    (second < ETC_SETTING_TX_INTERVAL_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);

	etc_cfg.tx_interval_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_TX_INTERVAL_SECS_ID, &etc_cfg.tx_interval_secs,
				      sizeof(etc_cfg.tx_interval_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_tx_interval_alarm_secs(uint32_t second)
{
	if ((second > ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MAX) ||
	    (second < ETC_SETTING_TX_INTERVAL_ALARMS_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.tx_interval_alarm_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_ID,
				      &etc_cfg.tx_interval_alarm_secs,
				      sizeof(etc_cfg.tx_interval_alarm_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_wake_early_secs(uint16_t second)
{
	if ((second > ETC_SETTING_WAKEUP_EARLY_SECS_MAX) ||
	    (second < ETC_SETTING_WAKEUP_EARLY_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.wake_early_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_WAKEUP_EARLY_SECS_ID, &etc_cfg.wake_early_secs,
				      sizeof(etc_cfg.wake_early_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_tx_delay_msec(uint16_t msecond)
{
	if ((msecond > ETC_SETTING_TX_DELAY_MSEC_MAX) ||
	    (msecond < ETC_SETTING_TX_DELAY_MSEC_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.tx_delay_msec = msecond;
	rc = etc_device_write_setting(ETC_SETTING_TX_DELAY_MSEC_ID, &etc_cfg.tx_delay_msec,
				      sizeof(etc_cfg.tx_delay_msec));
	if (rc == 0) {
		LOG_DBG("set %u", msecond);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_rx_duration_secs(uint16_t second)
{
	if ((second > ETC_SETTING_RX_DURATION_SECS_MAX) ||
	    (second < ETC_SETTING_RX_DURATION_SECS_MIN)) {
		return -EINVAL;
	}
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.rx_duration_secs = second;
	rc = etc_device_write_setting(ETC_SETTING_RX_DURATION_SECS_ID, &etc_cfg.rx_duration_secs,
				      sizeof(etc_cfg.rx_duration_secs));
	if (rc == 0) {
		LOG_DBG("set %u", second);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

int etc_set_alarm_threshold(uint16_t threshold)
{
	int rc = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	etc_cfg.alarm_threshold = threshold;
	rc = etc_device_write_setting(ETC_SETTING_ALARM_THRESHOLD_ID, &etc_cfg.alarm_threshold,
				      sizeof(etc_cfg.alarm_threshold));
	if (rc == 0) {
		LOG_DBG("set %u", threshold);
	}
	k_mutex_unlock(&setting_mutex);
	return rc;
}

enum etc_device_mode etc_get_device_mode(void)
{
	enum etc_device_mode mode;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	mode = etc_cfg.device_mode;
	k_mutex_unlock(&setting_mutex);
	return mode;
}

enum etc_power_mode_e etc_get_power_mode(void)
{
	enum etc_power_mode_e mode;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	mode = etc_cfg.power_mode;
	k_mutex_unlock(&setting_mutex);
	return mode;
}

enum etc_alarm_direction etc_get_alarm_direction(void)
{
	enum etc_alarm_direction alarm;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	alarm = etc_cfg.alarm_direction;
	k_mutex_unlock(&setting_mutex);
	return alarm;
}

uint32_t etc_get_log_interval_secs(void)
{
	uint32_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.log_interval_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint16_t etc_get_log_interval_alarm_secs(void)
{
	uint16_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.log_interval_alarm_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint32_t etc_get_tx_interval_secs(void)
{
	uint16_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.tx_interval_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint32_t etc_get_tx_interval_alarm_secs(void)
{
	uint16_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.tx_interval_alarm_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint16_t etc_get_wake_early_secs(void)
{
	uint16_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.wake_early_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint16_t etc_get_tx_delay_msec(void)
{
	uint16_t msecond = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	msecond = etc_cfg.tx_delay_msec;
	k_mutex_unlock(&setting_mutex);
	return msecond;
}

uint16_t etc_get_rx_duration_secs(void)
{
	uint16_t second = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	second = etc_cfg.rx_duration_secs;
	k_mutex_unlock(&setting_mutex);
	return second;
}

uint16_t etc_get_alarm_threshold(void)
{
	uint16_t threshold = 0;
	k_mutex_lock(&setting_mutex, K_FOREVER);
	threshold = etc_cfg.alarm_threshold;
	k_mutex_unlock(&setting_mutex);
	return threshold;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_info(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Hardware: %s", saved_hw_version);
	shell_print(shell, "Firmware: %s", saved_fw_version);
	shell_print(shell, "Device ID: %s", saved_device_id);
	return 0;
}

static int cmd_set_hardware_version(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		etc_set_hw_version(argv[1]);
	} else {
		shell_error(shell, "Invalid input hardware version");
	}
	return 0;
}

static int cmd_set_firmware_version(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		etc_set_fw_version(argv[1]);
	} else {
		shell_error(shell, "Invalid input firmware version");
	}

	return 0;
}

static int cmd_set_device_id(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		etc_set_device_id(argv[1]);
	} else {
		shell_error(shell, "Invalid device id");
	}

	return 0;
}

static int cmd_set_device(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_device_mode((enum etc_device_mode)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting device mode");
	return 0;
}

static int cmd_set_power(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_power_mode((enum etc_power_mode_e)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting power mode");

	return 0;
}

static int cmd_set_alarm_direction(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_alarm_direction((enum etc_alarm_direction)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting alarm direction");

	return 0;
}

static int cmd_set_log_interval(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {

		if (etc_set_log_interval_secs((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting log interval");
	return 0;
}

static int cmd_set_log_interval_alarm(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_log_interval_alarm_secs((uint16_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting log interval alarm");
	return 0;
}

static int cmd_set_tx_interval(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_tx_interval_secs((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting tx interval");
	return 0;
}

static int cmd_set_tx_interval_alarm(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_tx_interval_alarm_secs((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting tx interval alarm");
	return 0;
}

static int cmd_set_wakeup_early(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_wake_early_secs((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting wakeup early");
	return 0;
}

static int cmd_set_tx_delay(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_tx_delay_msec((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting tx delay");
	return 0;
}

static int cmd_set_rx_duration(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_rx_duration_secs((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting rx duration");
	return 0;
}

static int cmd_set_alarm_threshold(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_alarm_threshold((uint32_t)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting alarm threshold");
	return 0;
}

static int cmd_get_device(const struct shell *shell, size_t argc, char **argv)
{
	enum etc_device_mode mode = etc_get_device_mode();
	shell_print(shell, "Device mode %d", mode);
	return 0;
}

static int cmd_get_power(const struct shell *shell, size_t argc, char **argv)
{
	enum etc_power_mode_e mode = etc_get_power_mode();
	shell_print(shell, "Power mode %d", mode);
	return 0;
}

static int cmd_get_alarm_direction(const struct shell *shell, size_t argc, char **argv)
{
	enum etc_alarm_direction mode = etc_get_alarm_direction();
	shell_print(shell, "Alarm direction %d", mode);
	return 0;
}

static int cmd_get_log_interval(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t second = etc_get_log_interval_secs();
	shell_print(shell, "Log interval in seconds %d", second);
	return 0;
}

static int cmd_get_log_interval_alarm(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t second = etc_get_log_interval_alarm_secs();
	shell_print(shell, "Log interval alarm in seconds %d", second);
	return 0;
}

static int cmd_get_tx_interval(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t second = etc_get_tx_interval_secs();
	shell_print(shell, "Tx interval in seconds %d", second);
	return 0;
}

static int cmd_get_tx_interval_alarm(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t second = etc_get_tx_interval_alarm_secs();
	shell_print(shell, "Tx interval alarm in seconds %d", second);
	return 0;
}

static int cmd_get_wakeup_early(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t second = etc_get_wake_early_secs();
	shell_print(shell, "Wakeup early in seconds %d", second);
	return 0;
}

static int cmd_get_tx_delay(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t second = etc_get_tx_delay_msec();
	shell_print(shell, "Tx delay in milisecond %d", second);
	return 0;
}

static int cmd_get_rx_duration(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t second = etc_get_rx_duration_secs();
	shell_print(shell, "Rx duration in second %d", second);
	return 0;
}

static int cmd_get_alarm_threshold(const struct shell *shell, size_t argc, char **argv)
{
	uint16_t alarm = etc_get_alarm_threshold();
	shell_print(shell, "Alarm threshold %d", alarm);
	return 0;
}

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_settings, SHELL_CMD(info, NULL, "Get ETC settings.", cmd_info),
	SHELL_CMD(hardware, NULL, "Set hardware version", cmd_set_hardware_version),
	SHELL_CMD(firmware, NULL, "Set firmware version", cmd_set_firmware_version),
	SHELL_CMD(device, NULL, "Set device ID", cmd_set_device_id),
	SHELL_CMD(set_device, NULL, "Set device mode", cmd_set_device),
	SHELL_CMD(set_power, NULL, "Set power mode", cmd_set_power),
	SHELL_CMD(set_alarm_direction, NULL, "Set alarm direction", cmd_set_alarm_direction),
	SHELL_CMD(set_log_interval, NULL, "Set log interval in second", cmd_set_log_interval),
	SHELL_CMD(set_log_interval_alarm, NULL, "Set log interval alarm in second",
		  cmd_set_log_interval_alarm),
	SHELL_CMD(set_tx_interval, NULL, "Set tx interval in second", cmd_set_tx_interval),
	SHELL_CMD(set_tx_interval_alarm, NULL, "Set tx interval alarm in second",
		  cmd_set_tx_interval_alarm),
	SHELL_CMD(set_wakeup_early, NULL, "Set wakeup early in second", cmd_set_wakeup_early),
	SHELL_CMD(set_tx_delay, NULL, "Set tx delay in milisecond", cmd_set_tx_delay),
	SHELL_CMD(set_rx_duration, NULL, "Set rx duration in second", cmd_set_rx_duration),
	SHELL_CMD(set_alarm_threshold, NULL, "Set alarm threshold", cmd_set_alarm_threshold),
	SHELL_CMD(get_device, NULL, "Get device mode", cmd_get_device),
	SHELL_CMD(get_power, NULL, "Get power mode", cmd_get_power),
	SHELL_CMD(get_alarm_direction, NULL, "Get alarm direction", cmd_get_alarm_direction),
	SHELL_CMD(get_log_interval, NULL, "Get log interval in second", cmd_get_log_interval),
	SHELL_CMD(get_log_interval_alarm, NULL, "Get log interval alarm in second",
		  cmd_get_log_interval_alarm),
	SHELL_CMD(get_tx_interval, NULL, "Get tx interval in second", cmd_get_tx_interval),
	SHELL_CMD(get_tx_interval_alarm, NULL, "Get tx interval alarm in second",
		  cmd_get_tx_interval_alarm),
	SHELL_CMD(get_wakeup_early, NULL, "Get wakeup early in second", cmd_get_wakeup_early),
	SHELL_CMD(get_tx_delay, NULL, "Get tx delay in milisecond", cmd_get_tx_delay),
	SHELL_CMD(get_rx_duration, NULL, "Get rx duration in second", cmd_get_rx_duration),
	SHELL_CMD(get_alarm_threshold, NULL, "Get alarm threshold", cmd_get_alarm_threshold),
	SHELL_SUBCMD_SET_END);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(settings, &sub_settings, "ETC Settings", NULL);
#endif
