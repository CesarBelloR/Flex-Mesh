#include "etc_device.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(etc_device, CONFIG_ETC_DEVICE_LOG_LEVEL);

#define ETC_SETTINGS_NODE_LABEL etc_settings_storage

static struct nvs_fs etc_fs;

void etc_device_nvs_init(void)
{
	int rc = 0;
	struct flash_pages_info info;
	etc_fs.flash_device = FLASH_AREA_DEVICE(ETC_SETTINGS_NODE_LABEL);
	if (!device_is_ready(etc_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", etc_fs.flash_device->name);
		return;
	}

	etc_fs.offset = FLASH_AREA_OFFSET(ETC_SETTINGS_NODE_LABEL);
	rc = flash_get_page_info_by_offs(etc_fs.flash_device, etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}

	etc_fs.sector_size = info.size;

	etc_fs.sector_count = (FLASH_AREA_SIZE(ETC_SETTINGS_NODE_LABEL) / info.size);
	rc = nvs_mount(&etc_fs);
	if (rc) {
		LOG_ERR("Flash Init failed");
		return;
	}

	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)etc_fs.offset,
		FLASH_AREA_SIZE(ETC_SETTINGS_NODE_LABEL), info.size, etc_fs.sector_count);
	LOG_DBG("Initialised etc setting successfully");
}

static int etc_nvs_write(uint16_t element_id, const void *data, size_t len)
{
	size_t write_len = 0;
	write_len = nvs_write(&etc_fs, element_id, data, len);
	if (write_len != len && write_len != 0) {
		LOG_ERR("Failed in write data %d %d", len, write_len);
		return -EINVAL;
	}
	return 0;
}

static int etc_nvs_read(uint16_t element_id, void *data, size_t len)
{
	ssize_t read_len = 0;
	read_len = nvs_read(&etc_fs, element_id, data, len);
	if (read_len < 0) {
		LOG_ERR("Failed in reading NVS %d", read_len);
		return read_len;
	}

	if (read_len > len) {
		LOG_ERR("Read length is higher than request read %d %d %d", element_id, len,
			read_len);
		return read_len;
	}

	if (read_len == len) {
		return 0;
	}

	return -EINVAL;
}

int etc_device_write_calib(uint16_t calib_id, void *calib, int calib_size)
{
	return etc_nvs_write(calib_id, calib, calib_size);
}

int etc_device_read_calib(uint16_t calib_id, void *calib, int calib_size)
{
	LOG_DBG("Read calibration ID %d", calib_id);
	return etc_nvs_read(calib_id, calib, calib_size);
}
