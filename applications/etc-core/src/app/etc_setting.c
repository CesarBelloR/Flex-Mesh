/***************************************************************************/
/*!
\file       etc_setting.c
\brief      Setting service

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <zephyr.h>
#include <power/reboot.h>
#include <device.h>
#include <string.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <fs/nvs.h>

#include "etc_setting.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(etc_setting, CONFIG_ETC_APP_LOG_LEVEL);

#define ETC_STORAGE_OFFSET DT_REG_ADDR(DT_NODELABEL(storage_partition))
#define ETC_STORAGE_SIZE DT_REG_SIZE(DT_NODELABEL(storage_partition))

enum {
    ETC_CONFIG_ID = 0x01,
};

static int etc_nvs_write(int element_id, const void* data, size_t len);
static int etc_nvs_read(int element_id, void* data, size_t len);

typedef struct {
    etc_config_t etc_config;
    bool is_loaded;
} config_t;

static config_t etc_config;
static struct nvs_fs etc_fs;

etc_config_t* p_etc_config = &etc_config.etc_config;

void etc_nvs_init(void) {
    int rc = 0;
	struct flash_pages_info info;
	etc_fs.offset = ETC_STORAGE_OFFSET;
	rc = flash_get_page_info_by_offs(
		device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL),
		etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}
	etc_fs.sector_size = info.size;
	etc_fs.sector_count = (ETC_STORAGE_SIZE / info.size);

	rc = nvs_init(&etc_fs, DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
	if (rc) {
		LOG_ERR("NVS failed to initialize with error code %d", rc);
        return;
	}
    LOG_INF("Initialised etc setting successfully");
}

static int etc_nvs_write(int element_id, const void* data, size_t len) {
    size_t write_len = 0;
    write_len = nvs_write(&etc_fs, element_id, data, len);
    if (write_len != len) {
        LOG_ERR("Failed in write data %d %d", len, write_len);
        return -EINVAL;
    }
    return 0;
}

static int etc_nvs_read(int element_id, void* data, size_t len) {
    size_t read_len = 0;
    read_len = nvs_read(&etc_fs, element_id, data, len);
    if (read_len < 0) {
        return -EINVAL;
    }
    
    if (read_len > len) {
        LOG_ERR("Read length is higher than request read %d %d", len, read_len);
        return -EINVAL;
    }
    return read_len;
}

int etc_setting_get_config(etc_config_t *config) {
    if (etc_config.is_loaded) {
        memcpy(config, &etc_config.etc_config, sizeof(etc_config_t));
        return 0;
    }

    memset(&etc_config.etc_config, 0, sizeof(etc_config_t));
    int read_len = etc_nvs_read(ETC_CONFIG_ID, &etc_config.etc_config, sizeof(etc_config_t));
    if (read_len < 0) {
        LOG_ERR("Failed to read ETC Config");
        return -EINVAL;
    } 
    memcpy(config, &etc_config.etc_config, sizeof(etc_config_t));
    etc_config.is_loaded = true;
    return 0;
}

int etc_setting_set_config(etc_config_t *config) {
    etc_config.is_loaded = true;
    memcpy(&etc_config.etc_config, config, sizeof(etc_config_t));
    int rc = etc_nvs_write(ETC_CONFIG_ID, &etc_config.etc_config, sizeof(etc_config_t));
    if (rc != 0) {
        LOG_ERR("Failed to write ETC Config");
        return -EINVAL;
    } else {
        LOG_DBG("Wrote successful ETC Config");
    }

    return 0;
}