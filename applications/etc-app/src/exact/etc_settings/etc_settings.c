#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_settings, CONFIG_ETC_SETTINGS_LOG_LEVEL);
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "app_version.h"
#include "etc_settings.h"
#include "etc_device.h"

#define SETTINGS_HW_VERSION ETC_SETTING_HW_VERSION_ID
#define SETTINGS_FW_VERSION ETC_SETTING_FW_VERSION_ID
#define SETTINGS_DEVICE_ID ETC_SETTING_DEVICE_ID
#define SETTINGS_TIME_MEASUREMENT ETC_SETTING_TIME_MEASURE_INTERVAL_ID
#define SETTINGS_TIME_TRANSMISSION ETC_SETTING_TIME_TRANSMISSION_INTERVAL_ID

static char saved_hw_version[ETC_SETTING_HW_VER_LEN];
static char saved_fw_version[ETC_SETTING_FW_VER_LEN];
static char saved_device_id[ETC_SETTINGS_DEVICE_ID_LEN];
static int saved_time_measurement;
static int saved_time_transmission;
static char tmp_saved_value[ETC_SETTINGS_DEVICE_ID_LEN];

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
	etc_device_read_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));
	etc_device_read_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
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
	etc_device_write_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));
	k_mutex_unlock(&time_meas_mutex);
}

void etc_set_time_transmission_interval(int time_in_sec)
{
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	saved_time_transmission = time_in_sec;
	etc_device_write_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
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

static int etc_settings_init(const struct device *unused)
{
	int ret;
	ARG_UNUSED(unused);
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
	memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
	ret = etc_device_read_setting(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	if (ret)
	{
		etc_set_hw_version("0.0.0");
	}

	ret = etc_device_read_setting(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	if (ret)
	{
		etc_set_fw_version(APP_VERSION_STR);
	}

	ret = etc_device_read_setting(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	if (ret)
	{
		snprintf(tmp_saved_value, sizeof(tmp_saved_value),
				 "%X%X", NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
		LOG_INF("Set default device ID %s", tmp_saved_value);
		etc_set_device_id(tmp_saved_value);
	}

	ret = etc_device_read_setting(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));
	if (ret)
	{
		etc_set_time_measurement_interval(CONFIG_INTERVAL_TIME_MEASUREMENT_IN_SECONDS);
	}

	ret = etc_device_read_setting(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
	if (ret)
	{
		etc_set_time_transmission_interval(CONFIG_INTERVAL_TIME_TRANSMISSION_IN_SECONDS);
	}

	return 0;
}

SYS_INIT(etc_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

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
	if ((argc == 2) && (strlen(argv[1]) != 0))
	{
		etc_set_hw_version(argv[1]);
	}
	else
	{
		shell_error(shell, "Invalid input hardware version");
	}
	return 0;
}

static int cmd_set_firmware_version(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0))
	{
		etc_set_fw_version(argv[1]);
	}
	else
	{
		shell_error(shell, "Invalid input firmware version");
	}

	return 0;
}

static int cmd_set_device_id(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0))
	{
		etc_set_device_id(argv[1]);
	}
	else
	{
		shell_error(shell, "Invalid device id");
	}

	return 0;
}

static int cmd_set_measurement_time(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0))
	{
		etc_set_time_measurement_interval(atoi(argv[1]));
	}
	else
	{
		shell_error(shell, "Invalid parameter for setting measurement interval");
	}

	return 0;
}

static int cmd_set_transmission_time(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0))
	{
		etc_set_time_transmission_interval(atoi(argv[1]));
	}
	else
	{
		shell_error(shell, "Invalid parameter for setting transmission interval");
	}

	return 0;
}

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_settings,
							   SHELL_CMD(info, NULL, "Get ETC settings.", cmd_info),
							   SHELL_CMD(hardware, NULL, "Set hardware version", cmd_set_hardware_version),
							   SHELL_CMD(firmware, NULL, "Set firmware version", cmd_set_firmware_version),
							   SHELL_CMD(device, NULL, "Set device ID", cmd_set_device_id),
							   SHELL_CMD(measurement, NULL, "Set measurement interval time", cmd_set_measurement_time),
							   SHELL_CMD(transmission, NULL, "Set transmission interval time", cmd_set_transmission_time),
							   SHELL_SUBCMD_SET_END);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(settings, &sub_settings, "ETC Settings", NULL);
#endif
