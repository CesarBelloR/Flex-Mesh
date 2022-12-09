#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_settings, CONFIG_ETC_SETTINGS_LOG_LEVEL);
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include "app_version.h"
#include "etc_settings.h"

#define SETTINGS_HW_VERSION	"/lfs1/hw"
#define SETTINGS_FW_VERSION	"/lfs1/fw"
#define SETTINGS_DEVICE_ID	"/lfs1/id"
#define SETTINGS_TIME_MEASUREMENT "/lfs1/meas"
#define SETTINGS_TIME_TRANSMISSION "/lfs1/trans"

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

static void write_file(char *fname, char *buf, off_t len)
{
	struct fs_file_t file;
	int rc;
	fs_file_t_init(&file);

	rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		LOG_ERR("FAIL: open file: %d\n", rc);
		return;
	}

	rc = fs_truncate(&file, 0);
	if (rc) {
		LOG_ERR("Failed to truncate %s (%d)", fname, rc);
		return;
	}
	rc = fs_write(&file, buf, len);
	if (rc < 0) {
		LOG_ERR("FAIL: write file: %d\n", rc);
		return;
	}
	rc = fs_close(&file);
	if (rc < 0) {
		LOG_ERR("FAIL: close file: %d\n", rc);
		return;
	}
}

static int read_file(char *fname, char *buf, off_t len)
{
	struct fs_file_t file;
	int rc;
	fs_file_t_init(&file);
	rc = fs_open(&file, fname, FS_O_READ);
	if (rc < 0) {
		LOG_WRN("File %s not found", fname);
		memset(buf, 0, len);
		return rc;
	}

	rc = fs_read(&file, buf, len);
	if (rc < 0) {
		LOG_ERR("FAIL: read fail: %d", rc);
		return rc;
	}
	len = rc;

	rc = fs_close(&file);
	if (rc < 0) {
		LOG_ERR("FAIL: close fail: %d", rc);
		return rc;
	}
	return 0;
}

void etc_settings_refresh() {
	k_mutex_lock(&hw_mutex, K_FOREVER);
	k_mutex_lock(&fw_mutex, K_FOREVER);
	k_mutex_lock(&device_mutex, K_FOREVER);
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
        memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
        read_file(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	read_file(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	read_file(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	read_file(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));\
	read_file(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
	k_mutex_unlock(&device_mutex);
	k_mutex_unlock(&fw_mutex);
	k_mutex_unlock(&hw_mutex);
}

void etc_set_hw_version(const char* hw_version) {
        k_mutex_lock(&hw_mutex, K_FOREVER);
        strncpy(saved_hw_version, hw_version, ETC_SETTING_HW_VER_LEN);
	write_file(SETTINGS_HW_VERSION, (char *)hw_version, ETC_SETTING_HW_VER_LEN);
        k_mutex_unlock(&hw_mutex);
}

void etc_set_fw_version(const char* fw_version) {
        k_mutex_lock(&fw_mutex, K_FOREVER);
        strncpy(saved_fw_version, fw_version, ETC_SETTING_FW_VER_LEN);
	write_file(SETTINGS_FW_VERSION, (char *)fw_version, ETC_SETTING_FW_VER_LEN);
        k_mutex_unlock(&fw_mutex);
}

void etc_set_device_id(const char* device_id) {
        k_mutex_lock(&device_mutex, K_FOREVER);
        strncpy(saved_device_id, device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	write_file(SETTINGS_DEVICE_ID, (char *)device_id, ETC_SETTINGS_DEVICE_ID_LEN);
        k_mutex_unlock(&device_mutex);
}

void etc_set_time_measurement_interval(int time_in_sec) {
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	saved_time_measurement = time_in_sec;
	write_file(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));
	k_mutex_unlock(&time_meas_mutex);
}

void etc_set_time_transmission_interval(int time_in_sec) {
	k_mutex_lock(&time_trans_mutex, K_FOREVER);
	saved_time_transmission = time_in_sec;
	write_file(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
	k_mutex_unlock(&time_trans_mutex);
}

int etc_get_hw_version(char *buf, int buf_len) {
	int copy_size;

	k_mutex_lock(&hw_mutex, K_FOREVER);
  	copy_size = ETC_SETTING_HW_VER_LEN < buf_len ? 
		    ETC_SETTING_HW_VER_LEN : buf_len;
  	memcpy(buf, saved_hw_version, copy_size);
	k_mutex_unlock(&hw_mutex);
	return copy_size;
}

int etc_get_fw_version(char *buf, int buf_len) {
	int copy_size;

	k_mutex_lock(&fw_mutex, K_FOREVER);
  	copy_size = ETC_SETTING_FW_VER_LEN < buf_len ? 
		    ETC_SETTING_FW_VER_LEN : buf_len;
  	memcpy(buf, saved_fw_version, copy_size);
	k_mutex_unlock(&fw_mutex);
	return copy_size;
}

int etc_get_device_id(char *buf, int buf_len) {
	int copy_size;

	k_mutex_lock(&device_mutex, K_FOREVER);
  	copy_size = ETC_SETTINGS_DEVICE_ID_LEN < buf_len ? 
		    ETC_SETTINGS_DEVICE_ID_LEN : buf_len;
  	memcpy(buf, saved_device_id, copy_size);
	k_mutex_unlock(&device_mutex);
	return copy_size;
}

int etc_get_time_measurement_interval(void) {
	int interval = 0;
	k_mutex_lock(&time_meas_mutex, K_FOREVER);
	interval = saved_time_measurement;
	k_mutex_unlock(&time_meas_mutex);
	return interval;
}

int etc_get_time_transmission_interval(void) {
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
	ret = read_file(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	if (ret) {
		etc_set_hw_version("0.0.0");
	}
		
	ret = read_file(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	if (ret) {
		etc_set_fw_version(APP_VERSION_STR);
	}

	ret = read_file(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	if (ret) {
		snprintf(tmp_saved_value, sizeof(tmp_saved_value), 
			"%X%X", NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
		LOG_INF("Set default device ID %s", tmp_saved_value);
		etc_set_device_id(tmp_saved_value);
	}

	ret = read_file(SETTINGS_TIME_MEASUREMENT, (char *)&saved_time_measurement, sizeof(int));
	if (ret) {
		etc_set_time_measurement_interval(CONFIG_INTERVAL_TIME_MEASUREMENT_IN_SECONDS);
	}

	ret = read_file(SETTINGS_TIME_TRANSMISSION, (char *)&saved_time_transmission, sizeof(int));
	if (ret) {
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

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_settings,
	SHELL_CMD(info,   NULL, "Get ETC settings.", cmd_info),
	SHELL_CMD(hardware, NULL, "Set hardware version", cmd_set_hardware_version),
	SHELL_CMD(firmware, NULL, "Set firmware version", cmd_set_firmware_version),
	SHELL_CMD(device, NULL, "Set device ID", cmd_set_device_id),
	SHELL_CMD(measurement, NULL, "Set measurement interval time", cmd_set_measurement_time),
	SHELL_CMD(transmission, NULL, "Set transmission interval time", cmd_set_transmission_time),
	SHELL_SUBCMD_SET_END
);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(settings, &sub_settings, "ETC Settings", NULL);
#endif
