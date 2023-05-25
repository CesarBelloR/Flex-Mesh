/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/flash.h>
#include <dfu/dfu_target.h>
#include <dfu/dfu_target_stream.h>
#include <dfu/dfu_target_mcuboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/lwm2m.h>
#include <net/fota_download.h>
#include <lwm2m_util.h>
#include <zephyr/settings/settings.h>
/* Firmware update needs access to internal functions as well */
#include <lwm2m_engine.h>

#include <ncs_version.h>
#include <pm_config.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lwm2m_firmware, CONFIG_CLOUD_INTEGRATION_LOG_LEVEL);

#include "lwm2m_firmware.h"

#define BYTE_PROGRESS_STEP (1024 * 10)

#define LWM2M_FIRM_PREFIX "lwm2m:fir"

#define FOTA_PULL_SUPPORTED_COUNT 4
#define FOTA_INSTANCE_COUNT 1
#define ENABLED_LWM2M_FIRMWARE_OBJECT LWM2M_OBJECT_FIRMWARE_ID
static struct lwm2m_engine_res_inst pull_protocol_buff[FOTA_PULL_SUPPORTED_COUNT];
/* FOTA resource LWM2M_FOTA_UPDATE_PROTO_SUPPORT_ID  definition */
#define FOTA_PULL_COAP 0
#define FOTA_PULL_COAPS 1
/* Only supported when Server root certicate SEC tag is available*/
#define FOTA_PULL_HTTP 2
#define FOTA_PULL_HTTPS 3

static lwm2m_firmware_get_update_state_cb_t update_state_cb;
static uint8_t firmware_buf[CONFIG_LWM2M_COAP_BLOCK_SIZE];

/* Supported Protocols */
static uint8_t pull_protocol_support[FOTA_PULL_SUPPORTED_COUNT] = { FOTA_PULL_COAP, FOTA_PULL_HTTP,
								    FOTA_PULL_COAPS,
								    FOTA_PULL_HTTPS };

#ifdef CONFIG_DFU_TARGET_MCUBOOT
static uint8_t mcuboot_buf[CONFIG_LWM2M_INTEGRATION_MCUBOOT_FLASH_BUF_SIZE] __aligned(4);
#endif
#define UNUSED_OBJ_ID 0xffff
#define PENDING_DELAY K_MSEC(10)
static uint16_t ongoing_obj_id;
static char *fota_path;
static char *fota_host;
static int fota_sec_tag;
static uint8_t percent_downloaded;
static uint32_t bytes_downloaded;
static int application_obj_id;
static int modem_obj_id;
static int target_image_type[FOTA_INSTANCE_COUNT];


static void dfu_target_cb(enum dfu_target_evt_id evt);
static void start_fota_download(struct k_work *work);
static void start_pending_fota_download(struct k_work *work);

static K_WORK_DEFINE(download_work, start_fota_download);
static K_WORK_DELAYABLE_DEFINE(pending_download_work, start_pending_fota_download);
static struct update_data {
	struct k_work_delayable work;
	enum {APP, MODEM_DELTA} type;
} update_data;

void client_acknowledge(void);

/* Initialized to value different than success (0) */
static int modem_lib_init_result = -1;


static int *target_image_type_buffer_get(uint16_t instance)
{
	if (instance >= FOTA_INSTANCE_COUNT) {
		return NULL;
	}

	return &target_image_type[instance];
}

static int target_image_type_get(uint16_t instance)
{
	if (instance >= FOTA_INSTANCE_COUNT) {
		return DFU_TARGET_IMAGE_TYPE_NONE;
	}

	return target_image_type[instance];
}


static int set(const char *key, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
	uint16_t len;
	void *buf;
	struct lwm2m_obj_path path;
	int ret;

	if (!key) {
		return -ENOENT;
	}

	LOG_DBG("Loading \"%s\"", key);

	ret = lwm2m_string_to_path(key, &path, '/');
	if (ret) {
		return ret;
	}

	if (path.level == LWM2M_PATH_LEVEL_RESOURCE) {
		if (lwm2m_get_res_buf(&path, &buf, &len, NULL, NULL) != 0) {
			return -ENOENT;
		}
	} else if (path.level == LWM2M_PATH_LEVEL_OBJECT_INST) {
		/* Image Type */
		buf = target_image_type_buffer_get(path.obj_inst_id);
		if (!buf) {
			return -ENOENT;
		}
		len = sizeof(int);
	} else {
		return -ENOENT;
	}

	len = read_cb(cb_arg, buf, len);
	if (len <= 0) {
		LOG_ERR("Failed to read data");
		return -ENOENT;
	}

	if (path.level == LWM2M_PATH_LEVEL_RESOURCE) {
		lwm2m_set_res_data_len(&path, len);
	}

	return 0;
}

static struct settings_handler lwm2m_firm_settings = {
	.name = LWM2M_FIRM_PREFIX,
	.h_set = set,
};

static int write_resource_to_settings(int inst, int res, uint8_t *data, uint16_t data_len)
{
	char path[sizeof(LWM2M_FIRM_PREFIX "/65535/0/10")];

	snprintk(path, sizeof(path), LWM2M_FIRM_PREFIX "/%d/%d/%d", ENABLED_LWM2M_FIRMWARE_OBJECT,
		 inst, res);
	if (settings_save_one(path, data, data_len)) {
		LOG_ERR("Failed to store %s", path);
	}
	LOG_DBG("Permanently stored %s", path);
	return 0;
}

static int write_image_type_to_settings(int inst, int img_type)
{
	char path[sizeof(LWM2M_FIRM_PREFIX "/65632/0")];

	snprintk(path, sizeof(path), LWM2M_FIRM_PREFIX "/%d/%d", ENABLED_LWM2M_FIRMWARE_OBJECT,
		 inst);
	if (settings_save_one(path, &img_type, sizeof(int))) {
		LOG_ERR("Failed to store %s", path);
	}
	LOG_DBG("Permanently stored %s", path);
	return 0;
}

static void target_image_type_store(uint16_t instance, int img_type)
{
	if (instance >= FOTA_INSTANCE_COUNT) {
		LOG_WRN("Image type storage failure");
		return;
	}
	target_image_type[instance] = img_type;
	write_image_type_to_settings(instance, img_type);
}

static void reboot_work_handler(void)
{
	if (!IS_ENABLED(CONFIG_ZTEST)) {
		/* Call reboot execute */
		struct lwm2m_engine_res *dev_res;
		struct lwm2m_obj_path path;
		uint8_t reboot_src = REBOOT_SOURCE_FOTA_OBJ;

		path.level = LWM2M_PATH_LEVEL_RESOURCE;
		path.obj_id = LWM2M_OBJECT_DEVICE_ID;
		path.obj_inst_id = 0;
		path.res_id = 4;
		dev_res = lwm2m_engine_get_res(&path);

		if (dev_res && dev_res->execute_cb) {
			dev_res->execute_cb(0, &reboot_src, 1);
		} else {
			LOG_PANIC();
			sys_reboot(SYS_REBOOT_COLD);
			LOG_WRN("Rebooting");
		}
	}
}

/************** Wrappers between normal FOTA object and Advanced FOTA object ********/
static uint8_t get_state(uint16_t id)
{
	if (id >= FOTA_INSTANCE_COUNT) {
		LOG_WRN("Get state blocked by id, state");
		return 0;
	}
#if defined(CONFIG_LWM2M_FIRMWARE_UPDATE_OBJ_SUPPORT)
	return lwm2m_firmware_get_update_state_inst(id);
#else
	return 0;
#endif
}

static void set_state(uint16_t id, uint8_t state)
{
	if (id >= FOTA_INSTANCE_COUNT) {
		LOG_WRN("Set state blocked by id, state %d", state);
		return;
	}
#if defined(CONFIG_LWM2M_FIRMWARE_UPDATE_OBJ_SUPPORT)
		lwm2m_firmware_set_update_state_inst(id, state);
#endif
}

static void set_result(uint16_t id, uint8_t result)
{
	if (id >= FOTA_INSTANCE_COUNT) {
		LOG_WRN("Set result blocked by id,  result %d", result);
		return;
	}

#if defined(CONFIG_LWM2M_FIRMWARE_UPDATE_OBJ_SUPPORT)
        lwm2m_firmware_set_update_result_inst(id, result);
#endif
}

static int firmware_target_prepare(int dfu_image_type)
{
	int ret;

	switch (dfu_image_type) {
	case DFU_TARGET_IMAGE_TYPE_MCUBOOT:
		/* Set the required buffer for MCUboot targets */
		ret = dfu_target_mcuboot_set_buf(mcuboot_buf, sizeof(mcuboot_buf));
		break;

	case DFU_TARGET_IMAGE_TYPE_MODEM_DELTA:
		LOG_ERR("Unsupported Image type %d", dfu_image_type);
		ret = -EACCES;
		break;
	default:
		LOG_ERR("Unsupported Image type %d", dfu_image_type);
		ret = -EACCES;
		break;
	}
	return ret;
}


static int firmware_target_pre_init(int dfu_image_type)
{
	int ret;

	ret = firmware_target_prepare(dfu_image_type);
	if (ret) {
		return ret;
	}

	return dfu_target_init(dfu_image_type, 0, CONFIG_DOWNLOAD_CLIENT_BUF_SIZE, dfu_target_cb);
}

static int firmware_target_reset(uint16_t obj_inst_id)
{
	int ret;
	int dfu_image_type;

	dfu_image_type = target_image_type_get(obj_inst_id);
	ret = firmware_target_pre_init(dfu_image_type);
	if (ret) {
		return ret;
	}

	return dfu_target_reset();
}


static int set_firmware_update_type(int dfu_image_type)
{
	int ret = 0;

	switch (dfu_image_type) {
	case DFU_TARGET_IMAGE_TYPE_MCUBOOT:
		update_data.type = APP;
		break;

	case DFU_TARGET_IMAGE_TYPE_MODEM_DELTA:
		update_data.type = MODEM_DELTA;
		ret = -EACCES;
		break;
	default:
		ret = -EACCES;
		break;
	}
	return ret;
}

static int firmware_target_schedule_update(int obj_inst_id, int dfu_image_type)
{
	int ret;

	if (IS_ENABLED(CONFIG_FOTA_CLIENT_AUTOSCHEDULE_UPDATE)) {
		/* target is already scheduled */
		return 0;
	}

	ret = firmware_target_pre_init(dfu_image_type);
	if (ret) {
		return ret;
	}
	return dfu_target_schedule_update(0);
}

static void update_work_handler(struct k_work *work)
{
	uint8_t result;
	int updated_instance;

	if (update_data.type == APP) {
		updated_instance = application_obj_id;
	}

	reboot_work_handler();
}


static int firmware_instance_schedule(uint16_t obj_inst_id)
{
	int dfu_image_type;

	dfu_image_type = target_image_type_get(obj_inst_id);

	if (set_firmware_update_type(dfu_image_type)) {
		LOG_ERR("Update for image type not supported %d", dfu_image_type);
		return -EACCES;
	}

	if (firmware_target_schedule_update(obj_inst_id, dfu_image_type)) {
		LOG_ERR("DFU shedule fail");
		return -EACCES;
	}

	return 0;
}

static int firmware_update_cb(uint16_t obj_inst_id, uint8_t *args, uint16_t args_len)
{
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);
	if (firmware_instance_schedule(obj_inst_id)) {
		return -EACCES;
	}

	k_work_schedule(&update_data.work, K_SECONDS(5));
	return 0;
}

static void *firmware_get_buf(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
			      size_t *data_len)
{
	*data_len = sizeof(firmware_buf);
	return firmware_buf;
}

static int firmware_update_result(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				  uint8_t *data, uint16_t data_len, bool last_block,
				  size_t total_size)
{
	/* Store state to pernament memory */
	write_resource_to_settings(obj_inst_id, res_id, data, data_len);
	return 0;
}

static void init_firmware_variables(void)
{
	int ret;

	if (fota_host) {
		k_free(fota_host);
		fota_host = NULL;
	}
	fota_path = NULL;
	percent_downloaded = 0;
	bytes_downloaded = 0;
	ongoing_obj_id = UNUSED_OBJ_ID;
	ret = k_work_schedule(&pending_download_work, PENDING_DELAY);
	if (ret < 0) {
		for (int i = 0; i < FOTA_INSTANCE_COUNT; i++) {
			/* Find a pending instance which is which Downloading */
			if (get_state(i) != STATE_DOWNLOADING) {
				continue;
			}
			LOG_ERR("FOTA Download start fail for %d instance", i);
			set_result(i, RESULT_UPDATE_FAILED);
		}
	}
}

static int firmware_update_state(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				 uint8_t *data, uint16_t data_len, bool last_block,
				 size_t total_size)
{
	int ret;

	if (update_state_cb) {
		update_state_cb(*data);
	}

	/* Store state to pernament memory */
	write_resource_to_settings(obj_inst_id, res_id, data, data_len);

	if (*data == STATE_IDLE) {
		/* Cancel Only object is same than ongoing update */
		if (obj_inst_id == ongoing_obj_id) {
			ongoing_obj_id = UNUSED_OBJ_ID;
			fota_download_cancel();
			ret = firmware_target_reset(obj_inst_id);
			if (ret < 0) {
				LOG_ERR("Failed to reset DFU target, err: %d", ret);
			}
			init_firmware_variables();
		}
	} else if (*data == STATE_DOWNLOADED) {
		if (obj_inst_id == ongoing_obj_id) {
			init_firmware_variables();
		}
	}

	return 0;
}

static void dfu_target_cb(enum dfu_target_evt_id evt)
{
	ARG_UNUSED(evt);
}

static int firmware_block_received_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				      uint8_t *data, uint16_t data_len, bool last_block,
				      size_t total_size)
{
	uint8_t curent_percent;
	uint32_t current_bytes;
	size_t offset;
	size_t skip = 0;
	int ret = 0;
	int image_type;

	if (!data_len) {
		return -EINVAL;
	}

	if (bytes_downloaded == 0) {
		if (ongoing_obj_id != UNUSED_OBJ_ID) {
			LOG_INF("DFU is allocated already");
			return -EAGAIN;
		}

		ongoing_obj_id = obj_inst_id;
		client_acknowledge();

		image_type = dfu_target_img_type(data, data_len);
		if (image_type == DFU_TARGET_IMAGE_TYPE_NONE) {
			ret = -ENOMSG; /* Translates to unsupported image type */
			goto cleanup;
		}
		LOG_INF("Image type %d", image_type);
		ret = firmware_target_prepare(image_type);
		if (ret) {
			goto cleanup;
		}

		/* Store Started DFU type */
		target_image_type_store(obj_inst_id, image_type);
		ret = dfu_target_init(image_type, 0, total_size, dfu_target_cb);
		if (ret < 0) {
			LOG_ERR("Failed to init DFU target, err: %d", ret);
			goto cleanup;
		}

		LOG_INF("%s firmware download started.",
			image_type == DFU_TARGET_IMAGE_TYPE_MODEM_DELTA ||
					image_type == DFU_TARGET_IMAGE_TYPE_FULL_MODEM ?
				"Modem" :
				"Application");
	}

	ret = dfu_target_offset_get(&offset);
	if (ret < 0) {
		LOG_ERR("Failed to obtain current offset, err: %d", ret);
		goto cleanup;
	}

	/* Display a % downloaded or byte progress, if no total size was
	 * provided (this can happen in PULL mode FOTA)
	 */
	if (total_size > 0) {
		curent_percent = bytes_downloaded * 100 / total_size;
		if (curent_percent > percent_downloaded) {
			percent_downloaded = curent_percent;
			LOG_INF("Downloaded %d%%", percent_downloaded);
		}
	} else {
		current_bytes = bytes_downloaded + data_len;
		if (current_bytes / BYTE_PROGRESS_STEP > bytes_downloaded / BYTE_PROGRESS_STEP) {
			LOG_INF("Downloaded %d kB", current_bytes / 1024);
		}
	}

	if (bytes_downloaded < offset) {
		skip = MIN(data_len, offset - bytes_downloaded);

		LOG_INF("Skipping bytes %d-%d, already written.", bytes_downloaded,
			bytes_downloaded + skip);
	}

	bytes_downloaded += data_len;

	if (skip == data_len) {
		/* Nothing to do. */
		return 0;
	}

	ret = dfu_target_write(data + skip, data_len - skip);
	if (ret < 0) {
		LOG_ERR("dfu_target_write error, err %d", ret);
		goto cleanup;
	}

	if (!last_block) {
		/* Keep going */
		return 0;
	}
	/* Last write to flash should be flush write */
	ret = dfu_target_done(true);
	if (ret == 0 && IS_ENABLED(CONFIG_FOTA_CLIENT_AUTOSCHEDULE_UPDATE)) {
		ret = dfu_target_schedule_update(0);
	}

	if (ret < 0) {
		LOG_ERR("dfu_target_done error, err %d", ret);
		goto cleanup;
	}
	LOG_INF("Firmware downloaded, %d bytes in total", bytes_downloaded);

	if (total_size && (bytes_downloaded != total_size)) {
		LOG_ERR("Early last block, downloaded %d, expecting %d", bytes_downloaded,
			total_size);
		ret = -EIO;
	}

cleanup:
	if (ret < 0) {
		if (dfu_target_reset() < 0) {
			LOG_ERR("Failed to reset DFU target");
		}
	}

	return ret;
}

static void fota_download_callback(const struct fota_download_evt *evt)
{
	int dfu_image_type;

	if (ongoing_obj_id == UNUSED_OBJ_ID) {
		return;
	}

	switch (evt->id) {
	/* These two cases return immediately */
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		/* Fetch instance image type */
		dfu_image_type = target_image_type_get(ongoing_obj_id);
		if (dfu_image_type == DFU_TARGET_IMAGE_TYPE_NONE) {
			dfu_image_type = fota_download_target();
			LOG_INF("FOTA download started, target %d", dfu_image_type);
			target_image_type_store(ongoing_obj_id, dfu_image_type);
		}
		return;
	default:
		return;

	/* Following cases mark end of FOTA download */
	case FOTA_DOWNLOAD_EVT_CANCELLED:
		LOG_ERR("FOTA_DOWNLOAD_EVT_CANCELLED");
		set_result(ongoing_obj_id, RESULT_CONNECTION_LOST);
		break;
	case FOTA_DOWNLOAD_EVT_ERROR:
		LOG_ERR("FOTA_DOWNLOAD_EVT_ERROR");

		dfu_image_type = fota_download_target();
		LOG_INF("FOTA download failed, target %d", dfu_image_type);
		target_image_type_store(ongoing_obj_id, dfu_image_type);
		switch (evt->cause) {
		/* No error, used when event ID is not FOTA_DOWNLOAD_EVT_ERROR. */
		case FOTA_DOWNLOAD_ERROR_CAUSE_NO_ERROR:
			set_result(ongoing_obj_id, RESULT_CONNECTION_LOST);
			break;
		/* Downloading the update failed. The download may be retried. */
		case FOTA_DOWNLOAD_ERROR_CAUSE_DOWNLOAD_FAILED:
			set_result(ongoing_obj_id, RESULT_CONNECTION_LOST);
			break;
		/* The update is invalid and was rejected. Retry will not help. */
		case FOTA_DOWNLOAD_ERROR_CAUSE_INVALID_UPDATE:
			/* FALLTHROUGH */
		/* Actual firmware type does not match expected. Retry will not help. */
		case FOTA_DOWNLOAD_ERROR_CAUSE_TYPE_MISMATCH:
			set_result(ongoing_obj_id, RESULT_UNSUP_FW);
			break;
		default:
			set_result(ongoing_obj_id, RESULT_UPDATE_FAILED);
			break;
		}
		break;

	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("FOTA download finished");
		dfu_image_type = fota_download_target();
		target_image_type_store(ongoing_obj_id, dfu_image_type);
		set_state(ongoing_obj_id, STATE_DOWNLOADED);
		break;
	}
}

static void start_fota_download(struct k_work *work)
{
	int ret;
	enum dfu_target_image_type type;

	type = DFU_TARGET_IMAGE_TYPE_ANY;

	ret = fota_download_start_with_image_type(fota_host, fota_path, fota_sec_tag, 0, 0, type);
	if (ret) {
		LOG_ERR("fota_download_start() failed, return code %d", ret);
		set_result(ongoing_obj_id, RESULT_CONNECTION_LOST);
	}

	return;
}

static int init_start_download(char *uri)
{
	int ret;

	ret = fota_download_init(fota_download_callback);
	if (ret != 0) {
		LOG_ERR("fota_download_init() returned %d", ret);
		return -EBUSY;
	}

	bool is_tls = strncmp(uri, "https://", 8) == 0 || strncmp(uri, "coaps://", 8) == 0;
	if (is_tls) {
		/* FIXME */
		fota_sec_tag = 1;
	} else {
		fota_sec_tag = -1;
	}

	/* Find the end of protocol marker https:// or coap:// */
	char *s = strstr(uri, "://");

	if (!s) {
		LOG_ERR("Host not found");
		return -EINVAL;
	}
	s += strlen("://");

	/* Find the end of host name, which is start of path */
	char *e = strchr(s, '/');

	if (!e) {
		LOG_ERR("Path not found");
		return -EINVAL;
	}

	/* Path can point to a string, which is kept in LwM2M engine's memory */
	fota_path = e + 1; /* Skip the '/' from path */
	int len = e - uri;

	/* For host, I need to allocate space, as I need to copy the substring */
	fota_host = k_malloc(len + 1);
	if (!fota_host) {
		LOG_ERR("Failed to allocate memory");
		return -ENOMEM;
	}
	strncpy(fota_host, uri, len);
	fota_host[len] = 0;

	k_work_submit(&download_work);

	return 0;
}

static void lwm2m_start_download_image(uint8_t *data, uint16_t obj_instance)
{
	int ret;
	char *package_uri = (char *)data;

	ongoing_obj_id = obj_instance;
	/* Clear stored Image type */
	target_image_type_store(obj_instance, DFU_TARGET_IMAGE_TYPE_NONE);
	ret = init_start_download(package_uri);
	switch (ret) {
	case 0:
		/* OK */
		break;
	case -EINVAL:
		set_result(obj_instance, RESULT_INVALID_URI);
		break;
	case -EBUSY:
		/* Failed to init MCUBoot or download client */
		set_result(obj_instance, RESULT_NO_STORAGE);
		break;
	default: /* Remaining errors from init_start_download() are mostly
		  * reflected by OUT OF MEMORY situations
		  */
		set_result(obj_instance, RESULT_OUT_OF_MEM);
	}
}

static void start_pending_fota_download(struct k_work *work)
{
	struct lwm2m_engine_res_inst *res_inst;
	struct lwm2m_obj_path path;

	if (ongoing_obj_id != UNUSED_OBJ_ID) {
		return;
	}

	for (int i = 0; i < FOTA_INSTANCE_COUNT; i++) {
		/* Find a pending instance which is which Downloading */
		if (get_state(i) != STATE_DOWNLOADING) {
			continue;
		}

		path.level = LWM2M_PATH_LEVEL_RESOURCE_INST;
		path.obj_id = ENABLED_LWM2M_FIRMWARE_OBJECT;
		path.obj_inst_id = i;
		path.res_id = 1;
		path.res_inst_id = 0;

		res_inst = lwm2m_engine_get_res_inst(&path);

		if (!res_inst) {
			continue;
		}
		LOG_INF("Trigger Pending instance %d", i);
		ongoing_obj_id = i;
		lwm2m_start_download_image(res_inst->data_ptr, i);
		return;
	}
}

static int write_dl_uri(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id, uint8_t *data,
			uint16_t data_len, bool last_block, size_t total_size)
{
	uint8_t state;
	char *package_uri = (char *)data;


	LOG_DBG("write URI: %s", package_uri);

	state = get_state(obj_inst_id);

	if (state == STATE_IDLE && data_len > 0) {
		set_state(obj_inst_id, STATE_DOWNLOADING);

		if (ongoing_obj_id == UNUSED_OBJ_ID) {
			lwm2m_start_download_image(data, obj_inst_id);
		} else {
			set_result(obj_inst_id, RESULT_ADV_CONFLICT_STATE);
		}
	} else if (data_len == 0) {
		if (ongoing_obj_id == UNUSED_OBJ_ID || ongoing_obj_id == obj_inst_id) {
			ongoing_obj_id = obj_inst_id;
			/* reset to state idle and result default */
			/* Init DFU state */
			set_result(obj_inst_id, RESULT_DEFAULT);
			ongoing_obj_id = UNUSED_OBJ_ID;
		}
	}
	return 0;
}

void lwm2m_firmware_set_update_state_cb(lwm2m_firmware_get_update_state_cb_t cb)
{
	update_state_cb = cb;
}

static void lwm2m_firmware_load_from_settings(int instance_id)
{
	int ret;
	char path[sizeof(LWM2M_FIRM_PREFIX "65632/0/10")];

	/* Read Object Spesific data */
	snprintk(path, sizeof(path), LWM2M_FIRM_PREFIX "/%d/%d", ENABLED_LWM2M_FIRMWARE_OBJECT,
		 instance_id);
	ret = settings_load_subtree(path);
	if (ret) {
		LOG_ERR("Failed to load settings, %d", ret);
	}
}

static void lwm2m_firmware_object_pull_protocol_init(int instance_id)
{
	struct lwm2m_engine_res *res;

	res = lwm2m_engine_get_res(&LWM2M_OBJ(ENABLED_LWM2M_FIRMWARE_OBJECT, instance_id,
					      LWM2M_FOTA_UPDATE_PROTO_SUPPORT_ID));
	if (res && res->multi_res_inst && res->res_inst_count < FOTA_PULL_SUPPORTED_COUNT) {
		/* Update resource instance count and buffer's */
		res->res_instances = pull_protocol_buff;
		res->res_inst_count = FOTA_PULL_SUPPORTED_COUNT;
		for (int i = 0; i < FOTA_PULL_SUPPORTED_COUNT; i++) {
			pull_protocol_buff[i].res_inst_id = RES_INSTANCE_NOT_CREATED;
			pull_protocol_buff[i].data_len = 0;
			pull_protocol_buff[i].max_data_len = 0;
			pull_protocol_buff[i].data_ptr = NULL;
			pull_protocol_buff[i].data_flags = 0;
		}
	}
}

static void lwm2m_firware_pull_protocol_support_resource_init(int instance_id)
{
	struct lwm2m_obj_path path;
	int ret;
	int supported_protocol_count;

	lwm2m_firmware_object_pull_protocol_init(instance_id);

	/* Enable non-security &  Security protocols for download client */
	supported_protocol_count = 4;

	for (int i = 0; i < supported_protocol_count; i++) {
		path = LWM2M_OBJ(ENABLED_LWM2M_FIRMWARE_OBJECT, instance_id,
				 LWM2M_FOTA_UPDATE_PROTO_SUPPORT_ID, i);

		ret = lwm2m_create_res_inst(&path);
		if (ret) {
			return;
		}

		ret = lwm2m_set_res_buf(&path, &pull_protocol_support[i],
					sizeof(uint8_t), sizeof(uint8_t), LWM2M_RES_DATA_FLAG_RO);
		if (ret) {
			lwm2m_delete_res_inst(&path);
			return;
		}
	}
}

static void lwm2m_firmware_register_write_callbacks(int instance_id)
{
	struct lwm2m_obj_path path = LWM2M_OBJ(ENABLED_LWM2M_FIRMWARE_OBJECT, instance_id,
					       LWM2M_FOTA_PACKAGE_ID);

	lwm2m_register_pre_write_callback(&path, firmware_get_buf);

	path.res_id = LWM2M_FOTA_PACKAGE_URI_ID;
	lwm2m_register_post_write_callback(&path, write_dl_uri);
	/* State */
	path.res_id = LWM2M_FOTA_STATE_ID;
	lwm2m_register_post_write_callback(&path, firmware_update_state);
	/* Result */
	path.res_id = LWM2M_FOTA_UPDATE_RESULT_ID;
	lwm2m_register_post_write_callback(&path, firmware_update_result);
}

static void firmware_object_state_check(void)
{
	uint8_t object_state;
	int dfu_image_type;

	object_state = get_state(application_obj_id);
	dfu_image_type = target_image_type_get(application_obj_id);

	if (object_state == STATE_DOWNLOADING) {
		ongoing_obj_id = application_obj_id;
		/* reset to state idle and result default */
		/* Init DFU state */
		set_result(application_obj_id, RESULT_DEFAULT);
	} else if (object_state == STATE_UPDATING &&
		   (dfu_image_type & DFU_TARGET_IMAGE_TYPE_ANY_MODEM)) {
		ongoing_obj_id = application_obj_id;
		set_result(application_obj_id, RESULT_UPDATE_FAILED);
	}
}

int lwm2m_init_firmware(void)
{
	int ret;

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("Failed to initialize settings subsystem, %d", ret);
		return ret;
	}

	ret = settings_register(&lwm2m_firm_settings);
	if (ret) {
		LOG_ERR("Failed to register settings, %d", ret);
		return ret;
	}

	for (int i = 0; i < FOTA_INSTANCE_COUNT; i++) {
		target_image_type[i] = DFU_TARGET_IMAGE_TYPE_NONE;
	}

	k_work_init_delayable(&update_data.work, update_work_handler);
	ongoing_obj_id = UNUSED_OBJ_ID;

	/* Init stream targets */
#ifdef CONFIG_DFU_TARGET_MCUBOOT
	/* Set the required buffer for MCUboot targets */
	ret = dfu_target_mcuboot_set_buf(mcuboot_buf, sizeof(mcuboot_buf));
	if (ret) {
		LOG_ERR("Failed to set MCUboot flash buffer %d", ret);
	}
#endif

	/* setup data buffer for block-wise transfer */
	application_obj_id = modem_obj_id = 0;
	lwm2m_firmware_load_from_settings(application_obj_id);
	lwm2m_firware_pull_protocol_support_resource_init(application_obj_id);
	lwm2m_firmware_register_write_callbacks(application_obj_id);
	lwm2m_firmware_set_update_cb(firmware_update_cb);
	lwm2m_firmware_set_write_cb(firmware_block_received_cb);

	firmware_object_state_check();
	return 0;
}

int lwm2m_init_image(void)
{
	int ret = 0;
	bool image_ok;
	uint8_t state = get_state(application_obj_id);

	image_ok = boot_is_img_confirmed();
	LOG_INF("Image is%s confirmed OK", image_ok ? "" : " not");
	if (!image_ok) {
		ret = boot_write_img_confirmed();
		if (ret) {
			LOG_ERR("Couldn't confirm this image: %d", ret);
			return ret;
		}

		LOG_INF("Marked image as OK");
		if (state == STATE_UPDATING) {
			LOG_INF("Firmware updated successfully");
			set_result(application_obj_id, RESULT_SUCCESS);
		}

	} else {
		if (state == STATE_UPDATING) {
			LOG_INF("Firmware failed to be updated");
			set_result(application_obj_id, RESULT_UPDATE_FAILED);
		}
	}

	return ret;
}
