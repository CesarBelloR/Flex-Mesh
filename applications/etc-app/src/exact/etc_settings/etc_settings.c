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
#define SETTINGS_TIME_MEASUREMENT  ETC_SETTING_TIME_MEASURE_INTERVAL_ID
#define SETTINGS_TIME_TRANSMISSION ETC_SETTING_TIME_TRANSMISSION_INTERVAL_ID

#define ETC_SETTING_DEVICE_MODE_DEFAULT		    ETC_DEVICE_MODE_RELAY
#define ETC_SETTING_RADIO_MODE_DEFAULT		    ETC_RADIO_MODE_LORA
#define ETC_SETTING_POWER_MODE_DEFAULT		    ETC_POWER_MODE_POWER_SAVER
#define ETC_SETTING_ALARM_DIRECTION_DEFAULT	    ETC_ALARM_DIR_GREATER
#define ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT	    900
#define ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT 900
#define ETC_SETTING_TX_INTERVAL_SECS_DEFAULT	    86400
#define ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT 86400
#define ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT	    840
#define ETC_SETTING_TX_DELAY_MSEC_DEFAULT	    29500
#define ETC_SETTING_RX_DURATION_SECS_DEFAULT	    120
#define ETC_SETTING_ALARM_THRESHOLD_DEFAULT	    0

static char saved_hw_version[ETC_SETTING_HW_VER_LEN];
static char saved_fw_version[ETC_SETTING_FW_VER_LEN];
static char saved_device_id[ETC_SETTINGS_DEVICE_ID_LEN];
static int saved_time_measurement;
static int saved_time_transmission;
static char tmp_saved_value[ETC_SETTINGS_DEVICE_ID_LEN];
static int flag_etc_config_load;
union etc_config etc_cfg;

K_MUTEX_DEFINE(hw_mutex);
K_MUTEX_DEFINE(fw_mutex);
K_MUTEX_DEFINE(device_mutex);
K_MUTEX_DEFINE(time_meas_mutex);
K_MUTEX_DEFINE(time_trans_mutex);

void etc_settings_refresh()
{
	k_mutex_lock(&hw_mutex, K_FOREVER);
	k_mutex_lock(&fw_mutex, K_FOREVER);
	k_mutex_lock(&device_mutex, K_FOREVER);
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
	memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
	etc_device_read_setting(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	etc_device_read_setting(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	etc_device_read_setting(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	etc_device_read_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement,
				sizeof(int));
	etc_device_read_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission,
				sizeof(int));
	k_mutex_unlock(&device_mutex);
	k_mutex_unlock(&fw_mutex);
	k_mutex_unlock(&hw_mutex);
	k_mutex_unlock(&time_meas_mutex);
	k_mutex_unlock(&time_trans_mutex);
}

void etc_set_hw_version(const char *hw_version)
{
	k_mutex_lock(&hw_mutex, K_FOREVER);
	strncpy(saved_hw_version, hw_version, ETC_SETTING_HW_VER_LEN);
	etc_device_write_setting(SETTINGS_HW_VERSION, (char *)hw_version, ETC_SETTING_HW_VER_LEN);
	k_mutex_unlock(&hw_mutex);
}

void etc_set_fw_version(const char *fw_version)
{
	k_mutex_lock(&fw_mutex, K_FOREVER);
	strncpy(saved_fw_version, fw_version, ETC_SETTING_FW_VER_LEN);
	etc_device_write_setting(SETTINGS_FW_VERSION, (char *)fw_version, ETC_SETTING_FW_VER_LEN);
	k_mutex_unlock(&fw_mutex);
}

void etc_set_device_id(const char *device_id)
{
	k_mutex_lock(&device_mutex, K_FOREVER);
	strncpy(saved_device_id, device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	etc_device_write_setting(SETTINGS_DEVICE_ID, (char *)device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	k_mutex_unlock(&device_mutex);
}

void etc_set_time_measurement_interval(int time_in_sec)
{
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	saved_time_measurement = time_in_sec;
	etc_device_write_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement,
				 sizeof(int));
	k_mutex_unlock(&time_meas_mutex);
}

void etc_set_time_transmission_interval(int time_in_sec)
{
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	saved_time_transmission = time_in_sec;
	etc_device_write_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission,
				 sizeof(int));
	k_mutex_unlock(&time_trans_mutex);
}

int etc_get_hw_version(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&hw_mutex, K_FOREVER);
	copy_size = ETC_SETTING_HW_VER_LEN < buf_len ? ETC_SETTING_HW_VER_LEN : buf_len;
	memcpy(buf, saved_hw_version, copy_size);
	k_mutex_unlock(&hw_mutex);
	return copy_size;
}

int etc_get_fw_version(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&fw_mutex, K_FOREVER);
	copy_size = ETC_SETTING_FW_VER_LEN < buf_len ? ETC_SETTING_FW_VER_LEN : buf_len;
	memcpy(buf, saved_fw_version, copy_size);
	k_mutex_unlock(&fw_mutex);
	return copy_size;
}

int etc_get_device_id(char *buf, int buf_len)
{
	int copy_size;

	k_mutex_lock(&device_mutex, K_FOREVER);
	copy_size = ETC_SETTINGS_DEVICE_ID_LEN < buf_len ? ETC_SETTINGS_DEVICE_ID_LEN : buf_len;
	memcpy(buf, saved_device_id, copy_size);
	k_mutex_unlock(&device_mutex);
	return copy_size;
}

int etc_get_time_measurement_interval(void)
{
	int interval = 0;
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	interval = saved_time_measurement;
	k_mutex_unlock(&time_meas_mutex);
	return interval;
}

int etc_get_time_transmission_interval(void)
{
	int interval = 0;
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	interval = saved_time_transmission;
	k_mutex_unlock(&time_trans_mutex);
	return interval;
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
		snprintf(tmp_saved_value, sizeof(tmp_saved_value), "%X%X", NRF_FICR->DEVICEID[0],
			 NRF_FICR->DEVICEID[1]);
		LOG_INF("Set default device ID %s", tmp_saved_value);
		etc_set_device_id(tmp_saved_value);
	}

	ret = etc_device_read_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement,
				      sizeof(int));
	if (ret) {
		etc_set_time_measurement_interval(CONFIG_INTERVAL_TIME_MEASUREMENT_IN_SECONDS);
	}

	ret = etc_device_read_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission,
				      sizeof(int));
	if (ret) {
		etc_set_time_transmission_interval(CONFIG_INTERVAL_TIME_TRANSMISSION_IN_SECONDS);
	}

	ret = etc_device_read_setting(ETC_CONFIG_ID, &flag_etc_config_load,
				      sizeof(flag_etc_config_load));
	if (ret) {
		/* Configuration is not ready. Need to load the default value */
		flag_etc_config_load = SETTINGS_CONFIG_READY_CODE;
		etc_device_write_setting(ETC_CONFIG_ID, &flag_etc_config_load,
					 sizeof(flag_etc_config_load));
		flag_config_set_default = true;
	} else {
		if (flag_etc_config_load != SETTINGS_CONFIG_READY_CODE) {
			etc_device_write_setting(ETC_CONFIG_ID, &flag_etc_config_load,
						 sizeof(flag_etc_config_load));
			flag_config_set_default = true;
		}
	}

	if (flag_config_set_default) {
		etc_set_device_mode(ETC_SETTING_DEVICE_MODE_DEFAULT);
		etc_set_radio_mode(ETC_SETTING_RADIO_MODE_DEFAULT);
		etc_set_power_mode(ETC_SETTING_POWER_MODE_DEFAULT);
		etc_set_alarm_direction(ETC_SETTING_ALARM_DIRECTION_DEFAULT);
		etc_set_log_interval_secs(ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT);
		etc_set_log_interval_alarm_secs(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT);
		etc_set_tx_interval_secs(ETC_SETTING_TX_INTERVAL_SECS_DEFAULT);
		etc_set_tx_interval_alarm_secs(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT);
		etc_set_wake_early_secs(ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT);
		etc_set_tx_delay_msec(ETC_SETTING_TX_DELAY_MSEC_DEFAULT);
		etc_set_rx_duration_secs(ETC_SETTING_RX_DURATION_SECS_DEFAULT);
		etc_set_alarm_threshold(ETC_SETTING_ALARM_THRESHOLD_DEFAULT);
	}
	return 0;
}

int etc_set_device_mode(enum etc_device_mode mode)
{
	etc_cfg.device_mode = mode;
	return etc_device_write_setting(ETC_SETTING_DEVICE_MODE_ID, &etc_cfg.device_mode,
					sizeof(etc_cfg.device_mode));
}

int etc_set_radio_mode(enum etc_radio_mode mode)
{
	etc_cfg.radio_mode = mode;
	return etc_device_write_setting(ETC_SETTING_RADIO_MODE_ID, &etc_cfg.radio_mode,
					sizeof(etc_cfg.radio_mode));
}

int etc_set_power_mode(enum etc_power_mode_e power)
{
	etc_cfg.power_mode = power;
	return etc_device_write_setting(ETC_SETTING_POWER_MODE_ID, &etc_cfg.power_mode,
					sizeof(etc_cfg.power_mode));
}

int etc_set_alarm_direction(enum etc_alarm_direction alarm)
{
	etc_cfg.alarm_direction = alarm;
	return etc_device_write_setting(ETC_SETTING_ALARM_DIRECTION_ID, &etc_cfg.alarm_direction,
					sizeof(etc_cfg.alarm_direction));
}

int etc_set_log_interval_secs(uint32_t second)
{
	if (second > ETC_SETTING_LOG_INTERVAL_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.log_interval_secs = second;
	return etc_device_write_setting(ETC_SETTING_LOG_INTERVAL_SECS_ID,
					&etc_cfg.log_interval_secs,
					sizeof(etc_cfg.log_interval_secs));
}

int etc_set_log_interval_alarm_secs(uint16_t second)
{
	if (second > ETC_SETTING_LOG_INTERVAL_ALARM_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.log_interval_alarm_secs = second;
	return etc_device_write_setting(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_ID,
					&etc_cfg.log_interval_alarm_secs,
					sizeof(etc_cfg.log_interval_alarm_secs));
}

int etc_set_tx_interval_secs(uint32_t second)
{
	if (second > ETC_SETTING_TX_INTERVAL_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.tx_interval_secs = second;
	return etc_device_write_setting(ETC_SETTING_TX_INTERVAL_SECS_ID, &etc_cfg.tx_interval_secs,
					sizeof(etc_cfg.tx_interval_secs));
}

int etc_set_tx_interval_alarm_secs(uint32_t second)
{
	if (second > ETC_SETTING_TX_INTERVAL_ALARMS_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.tx_interval_alarm_secs = second;
	return etc_device_write_setting(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_ID,
					&etc_cfg.tx_interval_alarm_secs,
					sizeof(etc_cfg.tx_interval_alarm_secs));
}

int etc_set_wake_early_secs(uint16_t second)
{
	if (second > ETC_SETTING_WAKEUP_EARLY_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.wake_early_secs = second;
	return etc_device_write_setting(ETC_SETTING_WAKEUP_EARLY_SECS_ID, &etc_cfg.wake_early_secs,
					sizeof(etc_cfg.wake_early_secs));
}

int etc_set_tx_delay_msec(uint16_t msecond)
{
	if (msecond > ETC_SETTING_TX_DELAY_MSEC_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.tx_delay_msec = msecond;
	return etc_device_write_setting(ETC_SETTING_TX_DELAY_MSEC_ID, &etc_cfg.tx_delay_msec,
					sizeof(etc_cfg.tx_delay_msec));
}

int etc_set_rx_duration_secs(uint16_t second)
{
	if (second > ETC_SETTING_RX_DURATION_SECS_DEFAULT) {
		return -EINVAL;
	}
	etc_cfg.rx_duration_secs = second;
	return etc_device_write_setting(ETC_SETTING_RX_DURATION_SECS_ID, &etc_cfg.rx_duration_secs,
					sizeof(etc_cfg.rx_duration_secs));
}

int etc_set_alarm_threshold(uint16_t threshold)
{
	etc_cfg.alarm_threshold = threshold;
	return etc_device_write_setting(ETC_SETTING_ALARM_THRESHOLD_ID, &etc_cfg.alarm_threshold,
					sizeof(etc_cfg.alarm_threshold));
}

enum etc_device_mode etc_get_device_mode(void)
{
	enum etc_device_mode mode;
	int rc = etc_device_read_setting(ETC_SETTING_DEVICE_MODE_ID, &mode, sizeof(mode));
	if (rc == 0) {
		etc_cfg.device_mode = mode;
	}
	return etc_cfg.device_mode;
}

enum etc_radio_mode etc_get_radio_mode(void)
{
	enum etc_radio_mode mode;
	int rc = etc_device_read_setting(ETC_SETTING_RADIO_MODE_ID, &mode, sizeof(mode));
	if (rc == 0) {
		etc_cfg.radio_mode = mode;
	}
	return etc_cfg.radio_mode;
}

enum etc_power_mode_e etc_get_power_mode(void)
{
	enum etc_power_mode_e mode;
	int rc = etc_device_read_setting(ETC_SETTING_POWER_MODE_ID, &mode, sizeof(mode));
	if (rc == 0) {
		etc_cfg.power_mode = mode;
	}
	return etc_cfg.power_mode;
}

enum etc_alarm_direction etc_get_alarm_direction(void)
{
	enum etc_alarm_direction alarm;
	int rc = etc_device_read_setting(ETC_SETTING_ALARM_DIRECTION_ID, &alarm, sizeof(alarm));
	if (rc == 0) {
		etc_cfg.alarm_direction = alarm;
	}
	return etc_cfg.alarm_direction;
}

uint32_t etc_get_log_interval_secs(void)
{
	uint32_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_LOG_INTERVAL_SECS_ID, &second, sizeof(second));
	if (rc == 0) {
		etc_cfg.log_interval_secs = second;
	}
	return etc_cfg.log_interval_secs;
}

uint16_t etc_get_log_interval_alarm_secs(void)
{
	uint16_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_LOG_INTERVAL_ALARM_SECS_ID, &second,
					 sizeof(second));
	if (rc == 0) {
		etc_cfg.log_interval_alarm_secs = second;
	}
	return etc_cfg.log_interval_alarm_secs;
}

uint32_t etc_get_tx_interval_secs(void)
{
	uint32_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_TX_INTERVAL_SECS_ID, &second, sizeof(second));
	if (rc == 0) {
		etc_cfg.tx_interval_secs = second;
	}
	return etc_cfg.tx_interval_secs;
}

uint32_t etc_get_tx_interval_alarm_secs(void)
{
	uint32_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_TX_INTERVAL_ALARMS_SECS_ID, &second,
					 sizeof(second));
	if (rc == 0) {
		etc_cfg.tx_interval_alarm_secs = second;
	}
	return etc_cfg.tx_interval_alarm_secs;
}

uint16_t etc_get_wake_early_secs(void)
{
	uint16_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_WAKEUP_EARLY_SECS_ID, &second, sizeof(second));
	if (rc == 0) {
		etc_cfg.wake_early_secs = second;
	}
	return etc_cfg.wake_early_secs;
}

uint16_t etc_get_tx_delay_msec(void)
{
	uint16_t msecond = 0;
	int rc = etc_device_read_setting(ETC_SETTING_TX_DELAY_MSEC_ID, &msecond, sizeof(msecond));
	if (rc == 0) {
		etc_cfg.tx_delay_msec = msecond;
	}
	return etc_cfg.tx_delay_msec;
}

uint16_t etc_get_rx_duration_secs(void)
{
	uint16_t second = 0;
	int rc = etc_device_read_setting(ETC_SETTING_RX_DURATION_SECS_ID, &second, sizeof(second));
	if (rc == 0) {
		etc_cfg.rx_duration_secs = second;
	}
	return etc_cfg.rx_duration_secs;
}

uint16_t etc_get_alarm_threshold(void)
{
	uint16_t threshold = 0;
	int rc = etc_device_read_setting(ETC_SETTING_ALARM_THRESHOLD_ID, &threshold,
					 sizeof(threshold));
	if (rc == 0) {
		etc_cfg.alarm_threshold = threshold;
	}
	return etc_cfg.alarm_threshold;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_info(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Hardware: %s", saved_hw_version);
	shell_print(shell, "Firmware: %s", saved_fw_version);
	shell_print(shell, "Device ID: %s", saved_device_id);
	shell_print(shell, "Time measurement (s): %d", saved_time_measurement);
	shell_print(shell, "Time transmission (s) %d", saved_time_transmission);
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

static int cmd_set_measurement_time(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		etc_set_time_measurement_interval(atoi(argv[1]));
	} else {
		shell_error(shell, "Invalid parameter for setting measurement interval");
	}

	return 0;
}

static int cmd_set_transmission_time(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		etc_set_time_transmission_interval(atoi(argv[1]));
	} else {
		shell_error(shell, "Invalid parameter for setting transmission interval");
	}

	return 0;
}

static int cmd_set_device(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_device_mode((enum etc_radio_mode)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting device mode");
	return 0;
}

static int cmd_set_radio(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		if (etc_set_radio_mode((enum etc_radio_mode)atoi(argv[1])) == 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting radio mode");
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
		if (etc_set_log_interval_secs((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_log_interval_alarm_secs((uint16_t)atoi(argv[1])) != 0) {
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
		if (etc_set_tx_interval_secs((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_tx_interval_alarm_secs((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_wake_early_secs((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_tx_delay_msec((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_rx_duration_secs((uint32_t)atoi(argv[1])) != 0) {
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
		if (etc_set_alarm_threshold((uint32_t)atoi(argv[1])) != 0) {
			shell_print(shell, "OK");
			return 0;
		}
	}
	shell_error(shell, "Invalid parameter for setting alarm threshold");
	return 0;
}

static int cmd_get_device(const struct shell *shell, size_t argc, char **argv)
{
	enum etc_radio_mode mode = etc_get_device_mode();
	shell_print(shell, "Device mode %d", mode);
	return 0;
}

static int cmd_get_radio(const struct shell *shell, size_t argc, char **argv)
{
	enum etc_radio_mode mode = etc_get_radio_mode();
	shell_print(shell, "Radio mode %d", mode);
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
	SHELL_CMD(measurement, NULL, "Set measurement interval time", cmd_set_measurement_time),
	SHELL_CMD(set_device, NULL, "Set device mode", cmd_set_device),
	SHELL_CMD(set_radio, NULL, "Set radio mode", cmd_set_radio),
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
	SHELL_CMD(get_radio, NULL, "Get radio mode", cmd_get_radio),
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
