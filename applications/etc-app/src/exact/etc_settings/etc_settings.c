#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_settings, CONFIG_ETC_SETTINGS_LOG_LEVEL);
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include "app_version.h"

#define ETC_SETTINGS_DEVICE_ID_LEN (32)
#define ETC_SETTING_FW_VER_LEN (8)
#define ETC_SETTING_HW_VER_LEN (8)

#define SETTINGS_HW_VERSION	"/lfs1/hw"
#define SETTINGS_FW_VERSION	"/lfs1/fw"
#define SETTINGS_DEVICE_ID	"/lfs1/id"

static char saved_hw_version[ETC_SETTING_HW_VER_LEN];
static char saved_fw_version[ETC_SETTING_FW_VER_LEN];
static char saved_device_id[ETC_SETTINGS_DEVICE_ID_LEN];

K_MUTEX_DEFINE(hw_mutex);
K_MUTEX_DEFINE(fw_mutex);
K_MUTEX_DEFINE(device_mutex);

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
		LOG_WRN("No wifi credentials loaded");
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
	memset(saved_hw_version, 0, ETC_SETTING_HW_VER_LEN);
	memset(saved_fw_version, 0, ETC_SETTING_FW_VER_LEN);
        memset(saved_device_id, 0, ETC_SETTINGS_DEVICE_ID_LEN);
        read_file(SETTINGS_HW_VERSION, saved_hw_version, ETC_SETTING_HW_VER_LEN);
	read_file(SETTINGS_FW_VERSION, saved_fw_version, ETC_SETTING_FW_VER_LEN);
	read_file(SETTINGS_DEVICE_ID, saved_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
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

char* etc_get_hw_version(void) {
	char* res = NULL;
	k_mutex_lock(&device_mutex, K_FOREVER);
	res = saved_hw_version;
	k_mutex_unlock(&device_mutex);
	return res;
}

char* etc_get_fw_version(void) {
	char* res = NULL;
	k_mutex_lock(&device_mutex, K_FOREVER);
	res = saved_fw_version;
	k_mutex_unlock(&device_mutex);
	return res;
}

char* etc_get_device_id(void) {
	char* res = NULL;
	k_mutex_lock(&device_mutex, K_FOREVER);
	res = saved_device_id;
	k_mutex_unlock(&device_mutex);
	return res;
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
		etc_set_device_id("N.A");
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
	return 0;
}

static int cmd_set_hardware_version(const struct shell *shell, size_t argc, char **argv)
{
	if (strlen(argv[1] == 0)) {
		shell_error(shell, "Invalid input hardware");
		return 0;
	}
	etc_set_hw_version(argv[1]);
	return 0;
}

static int cmd_set_firmware_version(const struct shell *shell, size_t argc, char **argv)
{
	if (strlen(argv[1] == 0)) {
		shell_error(shell, "Invalid input hardware");
		return 0;
	}
	etc_set_fw_version(argv[1]);
	return 0;
}

static int cmd_set_device_id(const struct shell *shell, size_t argc, char **argv)
{
	if (strlen(argv[1] == 0)) {
		shell_error(shell, "Invalid input hardware");
		return 0;
	}
	etc_set_device_id(argv[1]);
	return 0;
}

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_settings,
	SHELL_CMD(info,   NULL, "Get ETC settings.", cmd_info),
	SHELL_CMD(hardware, NULL, "Set hardware version", cmd_set_hardware_version),
	SHELL_CMD(firmware, NULL, "Set firmware version", cmd_set_firmware_version),
	SHELL_CMD(device, NULL, "Set device ID", cmd_set_device_id),
	SHELL_SUBCMD_SET_END
);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(settings, &sub_settings, "ETC Settings", NULL);
#endif
