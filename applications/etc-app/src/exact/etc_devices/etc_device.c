#include <zephyr.h>
#include <sys/reboot.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <fs/nvs.h>

#include "etc_device.h"
#include <logging/log.h>
LOG_MODULE_REGISTER(etc_setting, CONFIG_ETC_APP_LOG_LEVEL);

#define STORAGE_NODE_LABEL storage
#define RECORD_NODE_LABEL record_storage

// Max sector = fit sector + 1 free sector for swap
#define ETC_RECORD_MAX_SECTOR (79)
// Sector size / Element size
#define ETC_RECORD_MAX_PER_SECTOR (113) 
// Max record 
#define ETC_RECORD_MAX_RECORD (90 * 24 * 4) 

typedef struct {
    etc_config_t etc_config;
    bool is_loaded;
} config_t;

typedef struct {
    int8_t sector_idx;
    int8_t element_idx;
} etc_device_record_index_t;

typedef struct {
    etc_device_record_index_t oldest;
    etc_device_record_index_t newest;
    int16_t total;
} etc_device_record_table_t;

static etc_device_record_table_t etc_device_record_table;
static int etc_nvs_write(int element_id, const void* data, size_t len);
static int etc_nvs_read(int element_id, void* data, size_t len);
static etc_device_record_index_t etc_device_get_next_index(void);
static config_t etc_config;
static struct nvs_fs etc_fs;
static struct nvs_fs record_fs;

etc_config_t* p_etc_config = &etc_config.etc_config;

static void etc_nvs_init(void) {
    int rc = 0;
	struct flash_pages_info info;
	etc_fs.flash_device = FLASH_AREA_DEVICE(STORAGE_NODE_LABEL);
	if (!device_is_ready(etc_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", etc_fs.flash_device->name);
		return;
	}

    record_fs.flash_device = FLASH_AREA_DEVICE(RECORD_NODE_LABEL);
	if (!device_is_ready(record_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", record_fs.flash_device->name);
		return;
	}

    record_fs.offset = FLASH_AREA_OFFSET(RECORD_NODE_LABEL);
	etc_fs.offset = FLASH_AREA_OFFSET(STORAGE_NODE_LABEL);
	rc = flash_get_page_info_by_offs(etc_fs.flash_device, etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}

	etc_fs.sector_size = info.size;
    record_fs.sector_size = info.size;

	etc_fs.sector_count = (FLASH_AREA_SIZE(STORAGE_NODE_LABEL) / info.size);
    record_fs.sector_count = (FLASH_AREA_SIZE(RECORD_NODE_LABEL) / info.size);
    LOG_INF("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)etc_fs.offset, FLASH_AREA_SIZE(STORAGE_NODE_LABEL), info.size, etc_fs.sector_count);
    LOG_INF("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)record_fs.offset, FLASH_AREA_SIZE(RECORD_NODE_LABEL), info.size, record_fs.sector_count);
	rc = nvs_mount(&etc_fs);
	if (rc) {
		LOG_ERR("Flash Init failed");
		return;
	}

    LOG_INF("Initialised etc setting successfully");

    rc = etc_nvs_read(ETC_RECORD_STAT, &etc_device_record_table, sizeof(etc_device_record_table));
    if (rc != 0) {
        etc_device_record_table.newest.sector_idx = 0;
        etc_device_record_table.oldest.sector_idx = 0;
        etc_device_record_table.newest.element_idx = 0;
        etc_device_record_table.oldest.element_idx = 0;
        etc_device_record_table.total = 0;
        rc = etc_nvs_write(ETC_RECORD_STAT, &etc_device_record_table, sizeof(etc_device_record_table));
        if (rc != 0) {
            LOG_ERR("Failed to write record stat");
        } else {
            LOG_INF("Initialized the table record successful");
        }
    } 

    LOG_INF("Last record stat as below: ");
    LOG_INF("\tNewest record (%d,%d)", etc_device_record_table.newest.sector_idx, etc_device_record_table.newest.element_idx);
    LOG_INF("\tOldest record (%d,%d)", etc_device_record_table.oldest.sector_idx, etc_device_record_table.oldest.element_idx);
    LOG_INF("\tTotal record %d", etc_device_record_table.total);
}

static int etc_nvs_write(int element_id, const void* data, size_t len) {
    size_t write_len = 0;
    write_len = nvs_write(&etc_fs, element_id, data, len);
    if (write_len != len && write_len != 0) {
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
    return 0;
}

void etc_device_init(void) {
    etc_nvs_init();
}

int etc_device_get_config(etc_config_t *config) {
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

int etc_device_set_config(etc_config_t *config) {
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

bool etc_device_buffer_is_erased(uint8_t* buf, uint8_t length) {
    for (int i = 0; i < length; i++) {
        if (buf[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

int etc_device_write_record(etc_device_record_t* record) {
    uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
    etc_device_record_index_t record_index;
    if (etc_device_record_table.total == 0) {
        record_index = etc_device_record_table.newest;
        etc_device_record_table.total += 1;
    } else {
        record_index = etc_device_get_next_index();
    }

    LOG_DBG("Record to write data (%d,%d)", record_index.sector_idx, record_index.element_idx);
    uint32_t record_addr = (record_fs.offset) + record_index.sector_idx * record_fs.sector_size 
        + record_index.element_idx * ETC_DEVICE_RECORD_SIZE;
    LOG_DBG("Write address 0x%08x", record_addr);
    int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
    if (rc != 0) {
        LOG_ERR("Error in reading flash err %d", rc);
        return rc;
    }

    if (etc_device_buffer_is_erased(buf, ETC_DEVICE_RECORD_SIZE) == false) {
        /* Need to erase flash */
        LOG_WRN("Data in address is not empty");
        LOG_HEXDUMP_DBG(buf, ETC_DEVICE_RECORD_SIZE, "DUMP");
        rc = flash_erase(record_fs.flash_device, record_addr, record_fs.sector_size);
        if (rc != 0) {
            LOG_ERR("Error in erasing flash err %d", rc);
            return rc;
        }
    }

    rc = flash_write(record_fs.flash_device, record_addr, record, ETC_DEVICE_RECORD_SIZE);
    if (rc != 0) {
        LOG_ERR("Error in writing flash err %d", rc);
        return rc;
    }
    LOG_DBG("Write data success");
    return 0;
 }

int etc_device_read_record(etc_device_record_t* record, int index) {
    uint8_t buf[ETC_DEVICE_RECORD_SIZE] = {0x00};
    uint32_t record_addr = (record_fs.offset) + index * ETC_DEVICE_RECORD_SIZE;
    LOG_DBG("Read address 0x%08x", record_addr);
    int rc = flash_read(record_fs.flash_device, record_addr, buf, ETC_DEVICE_RECORD_SIZE);
    if (rc != 0) {
        LOG_ERR("Error in reading flash err %d", rc);
        return rc;
    }

    memcpy(record->data, buf, ETC_DEVICE_RECORD_SIZE);
    return 0;
}

static etc_device_record_index_t etc_device_get_next_index(void) {
    if (etc_device_record_table.newest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
        etc_device_record_table.newest.element_idx += 1;
    } else {
        etc_device_record_table.newest.element_idx = 0;
        if (etc_device_record_table.newest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
            etc_device_record_table.newest.sector_idx += 1;
        } else {
            etc_device_record_table.newest.sector_idx = 0;
        }
    }

    if (etc_device_record_table.total < ETC_RECORD_MAX_RECORD - 1) {
        etc_device_record_table.oldest.element_idx = 0;
        etc_device_record_table.oldest.sector_idx = 0;
        etc_device_record_table.total += 1;
    } else {
        if (etc_device_record_table.oldest.element_idx < ETC_RECORD_MAX_PER_SECTOR - 1) {
            etc_device_record_table.oldest.element_idx += 1;
        } else {
            etc_device_record_table.oldest.element_idx = 0;
            if (etc_device_record_table.oldest.sector_idx < ETC_RECORD_MAX_SECTOR - 1) {
                etc_device_record_table.oldest.sector_idx += 1;
            } else {
                etc_device_record_table.oldest.sector_idx = 0;
            }
        }
    }

    return etc_device_record_table.newest;
}

int etc_device_write_setting(int setting_id, void* setting, int setting_size) {
    return etc_nvs_write(setting_id, setting, setting_size);
}

int etc_device_read_setting(int setting_id, void* setting, int setting_size) {
    return etc_nvs_read(setting_id, setting, setting_size);
}