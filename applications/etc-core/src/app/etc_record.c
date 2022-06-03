/***************************************************************************/
/*!
\file       etc_record.c
\brief      Record manager

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

#include "etc_record.h"
#include "etc_setting.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(etc_record, CONFIG_ETC_APP_LOG_LEVEL);

#define ETC_RECORD_OFFSET DT_REG_ADDR(DT_NODELABEL(etc_partition))
#define ETC_RECORD_SIZE DT_REG_SIZE(DT_NODELABEL(etc_partition))
K_MUTEX_DEFINE(etc_record_mtx);

enum {
    ETC_RECORD_HEADER_ID = 1000,
	ETC_RECORD_DATA_OFFSET_ID,
};

static void record_work_handler(struct k_work *work);
static struct k_work_delayable record_work;

static struct nvs_fs etc_record_fs;
static record_header_t etc_record_header;
static record_header_t *p_header = &etc_record_header;
static int etc_record_write(int element_id, const void* data, size_t len);
static int etc_record_read(int element_id, void* data, size_t len);

int etc_record_init(void) {
    int rc = 0;
	struct flash_pages_info info;
	etc_record_fs.offset = ETC_RECORD_OFFSET;
	rc = flash_get_page_info_by_offs(
		device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL),
		etc_record_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}
	
	etc_record_fs.sector_size = info.size;
	etc_record_fs.sector_count = (ETC_RECORD_SIZE / info.size);

	rc = nvs_init(&etc_record_fs, DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
	if (rc) {
		LOG_ERR("NVS failed to initialize with error code %d", rc);
        return rc;
	}
    LOG_INF("Initialised etc record successfully");

	rc = etc_record_read(ETC_RECORD_HEADER_ID, (void*)&etc_record_header, sizeof(etc_record_header));
	if (rc < 0) {
		LOG_DBG("Header is not ready");
		memset(&etc_record_header, 0, sizeof(etc_record_header));
		etc_record_write(ETC_RECORD_HEADER_ID, (void*)&etc_record_header, sizeof(etc_record_header));
	} else if (rc != sizeof(etc_record_header)){
		LOG_WRN("Header length is different structure header");
		memset(&etc_record_header, 0, sizeof(etc_record_header));
		etc_record_write(ETC_RECORD_HEADER_ID, (void*)&etc_record_header, sizeof(etc_record_header));
	} else {
		LOG_INF("Loaded header successfully");
		LOG_HEXDUMP_INF(&etc_record_header, 32, "Header");
	}

	k_work_init_delayable(&record_work, record_work_handler);
	k_work_schedule(&record_work, K_SECONDS(p_etc_config->log_interval_secs));
	return 0;
}

static int etc_record_write(int element_id, const void* data, size_t len) {
    size_t write_len = 0;
    write_len = nvs_write(&etc_record_fs, element_id, data, len);
    if (write_len != len) {
        LOG_ERR("Failed in write data %d %d", len, write_len);
        return -EINVAL;
    }
    return 0;
}

static int etc_record_read(int element_id, void* data, size_t len) {
    size_t read_len = 0;
    read_len = nvs_read(&etc_record_fs, element_id, data, len);
    if (read_len < 0) {
        return -EINVAL;
    }
    
    if (read_len > len) {
        LOG_ERR("Read length is higher than request read %d %d", len, read_len);
        return -EINVAL;
    }
    return read_len;
}

int etc_record_put(void* data, int length) {
	int flag_over_flow = 0;
	k_mutex_lock(&etc_record_mtx, K_FOREVER);
	if (p_header->index < RECORD_MANAGER_MAX_ELEMENTS) {
		uint8_t id = ETC_RECORD_DATA_OFFSET_ID + p_header->write;
		int rc = etc_record_write(id, data, length);
		if (rc != length) {
			LOG_ERR("Failed to write data");
		} else {
			if (++p_header->write == RECORD_MANAGER_MAX_ELEMENTS) {
				p_header->write = 0;
			} 
			p_header->flag[p_header->index].is_data_ready = 1;
			p_header->index += 1;
			p_header->change = 1;
		}
	} else {
		LOG_ERR("Over Flow");
		flag_over_flow = 1;
	}
	k_mutex_unlock(&etc_record_mtx);
	return flag_over_flow;
}

int etc_record_pop(void* data, int length) {
	k_mutex_lock(&etc_record_mtx, K_FOREVER);
	if (0 == p_header->index) {
		k_mutex_unlock(&etc_record_mtx);
		return 0;
	}
	uint8_t id = ETC_RECORD_DATA_OFFSET_ID + p_header->read;
	int rc = etc_record_read(id, data, length);
	if ((rc != length) || (rc < 0)) {
		LOG_ERR("Can't read data at ID %d - error %d", id, rc);
	} else {
		if (++p_header->read == RECORD_MANAGER_MAX_ELEMENTS) {
			p_header->read += 1;
		}
		p_header->flag[p_header->index].is_data_ready = 0;
		p_header->index -= 1;
		p_header->change = 1;
		rc = 0;
	}
	k_mutex_unlock(&etc_record_mtx);
	return rc;
}

int etc_record_is_empty(void) {
	int num_index = 0;
	k_mutex_lock(&etc_record_mtx, K_FOREVER);
	num_index = p_header->index;
	k_mutex_unlock(&etc_record_mtx);
	return num_index;
}

void etc_record_force_save(void) {
	int any_change = 0;
	k_mutex_lock(&etc_record_mtx, K_FOREVER);
	any_change = p_header->change;
	k_mutex_unlock(&etc_record_mtx);
	if (any_change) {
		int rc = etc_record_write(ETC_RECORD_HEADER_ID, (void*)&etc_record_header, sizeof(etc_record_header));
		if (rc < 0) {
			LOG_ERR("Failed to write record");
		} else if (rc != sizeof(etc_record_header)) {
			LOG_ERR("Invalid length written");
		} else {
			LOG_INF("Write header successful");
		}
	} 
}

static void record_work_handler(struct k_work *work) {
	etc_record_force_save();
	k_work_schedule(&record_work, K_SECONDS(p_etc_config->log_interval_secs));
}
