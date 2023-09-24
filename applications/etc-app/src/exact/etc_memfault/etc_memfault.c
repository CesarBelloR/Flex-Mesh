#include <stdint.h>
#include <stdbool.h>

#include "etc_memfault.h"
#include "app_version.h"

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

#include <memfault/core/build_info.h>
#include <memfault/core/compiler.h>
#include <memfault/core/platform/device_info.h>
#include <memfault/http/http_client.h>
#include <memfault/ports/zephyr/http.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(memfault_etc, CONFIG_MEMFAULT_ETC_LOG_LEVEL);


static char device_serial[CONFIG_MEMFAULT_ETC_DEVICE_ID_MAX_LEN + 1];
static char device_hw_version[CONFIG_MEMFAULT_ETC_HW_VERSION_MAX_LEN + 1] = 
		CONFIG_MEMFAULT_ETC_HW_VERSION_PREFIX;

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	static bool is_init;
	int ret;

	static char fw_version[sizeof(APP_VERSION_STR) + 9] = APP_VERSION_STR;

	if (!is_init) {
		size_t version_len = strlen(fw_version);
		ret = snprintk(&fw_version[version_len],
			       sizeof(fw_version) - version_len, "+");
		if (ret >= 0) {
			version_len += ret;
		}
		/* 6 char build ID + '\0' */
		const size_t build_id_chars = 6 + 1;
		const size_t build_id_num_chars =
		    MIN(build_id_chars, sizeof(fw_version) - version_len - 1);

		memfault_build_id_get_string(&fw_version[version_len], build_id_num_chars);

		version_len = strlen(device_hw_version);
		snprintk(&device_hw_version[version_len],
			 sizeof(device_hw_version) - version_len,
			 CONFIG_BOARD_VERSION);

		is_init = true;
	}

	*info = (sMemfaultDeviceInfo) {
		.device_serial = device_serial,
		.software_type = CONFIG_MEMFAULT_ETC_FW_TYPE,
		.software_version = fw_version,
		.hardware_version = device_hw_version,
	};
}


int memfault_etc_device_id_set(const char *device_id, size_t len)
{
	if (device_id == NULL) {
		return -EINVAL;
	}

	if (len > (sizeof(device_serial) - 1)) {
		LOG_ERR("Device ID is longer than MEMFAULT_ETC_DEVICE_ID_MAX_LEN");
		LOG_WRN("The Memfault device ID will be truncated");
	}

	memcpy(device_serial, device_id, MIN(sizeof(device_serial) - 1, len));

	device_serial[MIN(sizeof(device_serial) - 1, len)] = '\0';

	return 0;
}
