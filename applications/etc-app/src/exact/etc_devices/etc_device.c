#include "etc_device.h"
#include "etc_sensor.h"
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/random/random.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include "etc_settings.h"
#include "etc_device_record.h"
#include "etc_img.h"
#include "etc_memfault_metrics.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_device, CONFIG_ETC_APP_LOG_LEVEL);

#define ETC_SETTINGS_NODE_LABEL etc_settings_storage

#define ETC_RECORD_DEFAULT_RX_DURATION_SECONDS (5)
#define ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS (60)
#define ETC_RECORD_DEFAULT_TX_INTERVAL_SECONDS (300)
#define ETC_RECORD_DEFAULT_TX_PROBE_SECONDS (21600)
#define ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE (15)

static enum etc_device_mode etc_device_current_mode;
static union etc_device_record current_relay_data_sensor;
extern struct etc_device_record_table *p_etc_device_record_table;
static int etc_nvs_write(uint16_t element_id, const void *data, size_t len);
static int etc_nvs_read(uint16_t element_id, void *data, size_t len);
static int etc_nvs_read_with_len(uint16_t element_id, void *data, size_t len);
static int etc_nvs_reset_relay_stat(void);
static struct nvs_fs etc_fs;

static uint16_t ram_nack_record_id;
static enum etc_device_job logger_job = ETC_DEVICE_JOB_LOG;
static enum etc_transmit_sub_job transmit_sub_job = ETC_TRANSMIT_NORMAL;
static uint16_t tx_logger_lora_offset_mins = 0;
static time_t device_next_transmit_s = 0;

static struct etc_device_relay_record_stat relay_record_stat;
static struct etc_device_relay_record_stat *p_relay_stat = &relay_record_stat;
static uint8_t etc_relay_record_buf[ETC_DEVICE_RELAY_BUF_SIZE];
static struct etc_device_relay_record relay_record_list[ETC_RELAY_RECORD_MAX_ELEMENT];
K_MUTEX_DEFINE(etc_relay_record_mutex);

static enum gnss_location_request_status gnss_request;
K_MUTEX_DEFINE(gnss_request_mutex);
static time_t time_last_gnss_request;

K_MUTEX_DEFINE(relay_data_sensor_mtx);

static struct etc_device_record_backup_data* p_record_backup = NULL;

/* The public key ID of the current (signed) image. The public key ID 
 * is the first 4 bytes of the public key hash. 
 */
static uint8_t img_pubkey_id[IMG_PUBKEY_ID_LEN];

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
		LOG_ERR("Failed to mount the etc storage");
		return;
	} else {
			LOG_DBG("Mounted the etc storage successfully");
	}

	rc = etc_nvs_read(ETC_SETTING_DEVICE_MODE_ID, &etc_device_current_mode, 
		sizeof(etc_device_current_mode));
	if (rc) {
		/* If failed in reading device mode ID */
		etc_device_current_mode = ETC_SETTING_DEVICE_MODE_DEFAULT;
	}

	if (etc_device_current_mode == ETC_DEVICE_MODE_RELAY) {
		etc_nvs_reset_relay_stat();
		memset(&current_relay_data_sensor, 0, sizeof(current_relay_data_sensor));
	}
	LOG_DBG("Offset %d - Size %d - Sector Size %d - Sector Cnt %d", (int)etc_fs.offset,
		FLASH_AREA_SIZE(ETC_SETTINGS_NODE_LABEL), info.size, etc_fs.sector_count);
	LOG_DBG("Initialised etc setting successfully");
	etc_device_record_init();
	/* Load the record backup */
	p_record_backup = etc_device_record_backup_get_object();
	/* Sync last record backup */
	etc_device_sync_record_on_ram();
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

static int etc_nvs_read_with_len(uint16_t element_id, void *data, size_t len)
{
	ssize_t read_len = 0;
	read_len = nvs_read(&etc_fs, element_id, data, len);
	if (read_len < 0) {
		LOG_ERR("Failed in reading NVS %d", read_len);
	}

	return read_len;
}

static int etc_nvs_reset_relay_stat(void) {
	relay_record_stat.flag_error = false;
	relay_record_stat.flag_over_flow = false;
	relay_record_stat.number_record = 0;
	relay_record_stat.read_index = 0;
	relay_record_stat.write_index = 0;
	return 0;
}

static void etc_device_init_gnss(void)
{
	int ret;

	ret = etc_device_read_setting(ETC_GNSS_LOCATION_REQUEST_STATUS, &gnss_request,
				      sizeof(gnss_request));
	if (ret) {
		etc_device_set_location_request(ETC_GNSS_LOCATION_NO_REQUEST);
	}

	ret = etc_device_read_setting(ETC_GNSS_TIME_LAST_REQUEST, &time_last_gnss_request,
				      sizeof(time_last_gnss_request));
	if (ret) {
		etc_device_set_last_time_gnss_request(0);
	}
}

void etc_device_init(void)
{
	char *dev_str = "Unknown";
	enum etc_device_mode dev_mode = etc_get_device_mode();
	uint8_t hash[IMAGE_HASH_LEN];
	int rc;

	logger_job = ETC_DEVICE_JOB_TX_RX;
	/* Logger Lora mode will sync with interval quarter hour */
	tx_logger_lora_offset_mins = (uint16_t)((sys_rand32_get() % 4) *
		ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE);
	if (dev_mode == ETC_DEVICE_MODE_RELAY) {
		dev_str = "Relay";
	} else if (dev_mode == ETC_DEVICE_MODE_LORA_LOGGER) {
		dev_str = "LoRa Logger";
	} else if (dev_mode == ETC_DEVICE_MODE_LTE_LOGGER) {
		dev_str = "LTE Logger";
	}
	LOG_INF("Device is %s", dev_str);

	rc = etc_img_get_pubkey_hash(hash);
	if (rc != 0) {
		LOG_WRN("retrieving img pubkey hash: %d", rc);
	} else {
		LOG_HEXDUMP_DBG(hash, IMAGE_HASH_LEN, "pubkey hash");
		memcpy(img_pubkey_id, hash, sizeof(img_pubkey_id));
	}

	etc_device_init_gnss();
}

int etc_device_write_setting(uint16_t setting_id, const void *setting, int setting_size)
{
	return etc_nvs_write(setting_id, setting, setting_size);
}

int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size)
{
	// LOG_DBG("Read setting ID %d", setting_id);
	return etc_nvs_read(setting_id, setting, setting_size);
}

int etc_device_delete_setting(uint16_t setting_id) 
{
	return nvs_delete(&etc_fs, setting_id);
}

int etc_device_read_setting_with_len(uint16_t setting_id, void *setting, int setting_size)
{
	// LOG_DBG("Read setting ID %d", setting_id);
	return etc_nvs_read_with_len(setting_id, setting, setting_size);
}

int etc_device_write_record_sensor(struct sensor_data *sensor, bool ota_running)
{
	int ret;
	int64_t time_start = k_uptime_get();
	union etc_device_record record;
	record.battery = (float)sensor->battery_mV / 1000.0;
	record.flag = (uint32_t)(sensor->battery_status);
	record.timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		record.sensor[i] = sensor->sensor[i];
	}
	LOG_HEXDUMP_DBG((uint8_t *)&record, sizeof(record), "SAVE");
	if (ota_running) {
		if (p_record_backup->num_records < ETC_DEVICE_BACKUP_RECORD_MAX_ELEMENT) {
			memcpy(&p_record_backup->records[p_record_backup->num_records], 
			       &record, sizeof(record));
			p_record_backup->num_records += 1;
		}
		LOG_DBG("Saved to backup RAM %d", p_record_backup->num_records);
		etc_device_record_backup_sync();
		return 0;
	} else {
		ret = etc_device_write_record(&record);
	}
	
	LOG_DBG("NVS time record: %lld", k_uptime_get() - time_start);
	return ret;
}

int etc_device_write_record(union etc_device_record *record)
{
	struct etc_device_record_index record_index = etc_device_record_get_next_index();
	off_t record_addr = etc_device_record_get_addr_offset_by_index(record_index);
	uint16_t record_id = etc_device_record_get_id_by_index(record_index);
	LOG_DBG("Record to write data %d (0x%08x) (%d,%d)", ETC_RECORD_ID_HEADER(record_id), (uint32_t)record_addr,
		record_index.sector_idx, record_index.element_idx);
	int rc = etc_device_record_write_data(record_addr, record->data, ETC_DEVICE_RECORD_SIZE);
	if (rc) {
		LOG_ERR("Can't write data to record err %d", rc);
		return rc;
	}
	etc_device_record_set_nack(record_id);
	etc_device_record_save_stat();
	return 0;
}

int etc_device_read_record(union etc_device_record *record, bool *active_reclaim)
{
	int rc = etc_device_record_find_nack(etc_device_record_reading,
		record, active_reclaim);
	if (rc < 0) {
		LOG_WRN("Don't have NACK record");
		return 0;
	}

	LOG_DBG("Record ID %d", rc);
	return rc;
}

int etc_device_write_relay_data(struct etc_device_relay_record *record)
{
	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	memcpy(&relay_record_list[p_relay_stat->write_index], record, sizeof(*record));
	if (p_relay_stat->number_record < ETC_RELAY_RECORD_MAX_ELEMENT) {
		p_relay_stat->number_record += 1;
		etc_mflt_metrics_relay_buffer_entries(p_relay_stat->number_record);
	} else {
		p_relay_stat->flag_over_flow = true;
		k_mutex_unlock(&etc_relay_record_mutex);
		LOG_WRN("Relay buffer overflow %d %d %d", p_relay_stat->number_record, 
			p_relay_stat->write_index, p_relay_stat->read_index);
		return -ENOMEM;
	}
	if (++p_relay_stat->write_index == ETC_RELAY_RECORD_MAX_ELEMENT) {
		p_relay_stat->write_index = 0;
	}
	LOG_DBG("Write record okay: %d %d", p_relay_stat->number_record, p_relay_stat->write_index);
	k_mutex_unlock(&etc_relay_record_mutex);
	return 0;
}

int etc_device_read_relay_data(struct etc_device_relay_record *record)
{
	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	if (p_relay_stat->number_record == 0) {
		k_mutex_unlock(&etc_relay_record_mutex);
		return -ENODATA;
	}
	memcpy(record, &relay_record_list[p_relay_stat->read_index], 
		sizeof(*record));
	k_mutex_unlock(&etc_relay_record_mutex);
	return 0;
}

void etc_device_sync_relay_data(void) {
	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	if (++p_relay_stat->read_index == ETC_RELAY_RECORD_MAX_ELEMENT) {
		p_relay_stat->read_index = 0;
	}
	p_relay_stat->number_record -= 1;
	k_mutex_unlock(&etc_relay_record_mutex);
}

int etc_device_set_ack_record(int record_id)
{
	int64_t time_start = k_uptime_get();
	etc_device_record_set_ack(ETC_RECORD_ID(record_id));
	LOG_DBG("NVS time ack: %lld", k_uptime_get() - time_start);
	return 0;
}

uint16_t etc_device_nack_count(void)
{
	return (etc_device_record_get_total_record() - 
		etc_device_record_get_num_ack());
}

int etc_device_erase_setting(uint16_t setting_id) 
{
	return nvs_delete(&etc_fs, setting_id);
}

enum etc_device_mode etc_device_get_mode(void)
{
	return etc_get_device_mode();
}

bool etc_device_is_logger_lora(void)
{
	return (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER);
}

bool etc_device_is_relay(void) 
{
	return (etc_get_device_mode() == ETC_DEVICE_MODE_RELAY);
}

int etc_device_get_rx_timeout(void)
{
	int rx_duration = etc_get_rx_duration_secs();
	return rx_duration == 0 ? ETC_RECORD_DEFAULT_RX_DURATION_SECONDS
				: rx_duration;
}

int etc_device_get_log_interval_second(void) 
{
	int second = etc_get_log_interval_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS
				: second;
}

int etc_device_get_tx_interval_second(void) 
{
	int second = etc_get_tx_interval_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_TX_INTERVAL_SECONDS : second;
}

int etc_device_get_tx_probe_second(void) 
{
	int second = etc_get_tx_probe_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_TX_PROBE_SECONDS : second;
}

void etc_device_set_job(enum etc_device_job job) 
{
	logger_job = job;
}

enum etc_device_job etc_device_get_job(void) 
{
	return logger_job;
}

void etc_device_set_transmit_sub_job(enum etc_transmit_sub_job job) {
	transmit_sub_job = job;
}

enum etc_transmit_sub_job etc_device_get_transmit_sub_job(void) {
	return transmit_sub_job;
}

int etc_device_erase_cfg(void) 
{
	int rc = 0;
	/* Erase config, record and reclaim */
	for (int id = ETC_CONFIG_ID; id <= ETC_RECORD_RECLAIM; id ++ ) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase setting */
	for (int id = ETC_SETTING_TIME_MEASURE_INTERVAL_ID; id <= ETC_SETTING_TX_PROBE_SEC_ID; id ++ ) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase record RAM and sub-sys settings */
	etc_device_record_clean_up();
	return 0;
}

uint16_t etc_device_get_tx_logger_lora_offset_mins(void) 
{
	return tx_logger_lora_offset_mins;
}

uint16_t etc_device_get_tx_no_probe_offset_mins(void) 
{
	uint16_t probe_offset = 0;
	if (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) {
		probe_offset = etc_get_lora_probe_offset_secs();
	} else if (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) {
		probe_offset = etc_get_lte_probe_offset_secs();
	}
	return (probe_offset / 60);
}

int etc_device_get_img_pubkey_id(uint8_t *pubkey_id, uint8_t pubkey_id_len)
{
	__ASSERT_NO_MSG(pubkey_id_len >= sizeof(img_pubkey_id));

	memcpy(pubkey_id, img_pubkey_id, sizeof(img_pubkey_id));
	
	return sizeof(img_pubkey_id);
}

void etc_device_set_next_transmit(time_t next_transmit_s)
{
	device_next_transmit_s = next_transmit_s;
}

time_t etc_device_get_next_transmit(void)
{
	return device_next_transmit_s;
}

int etc_device_set_location_request(enum gnss_location_request_status status)
{
	int rc = 0;

	k_mutex_lock(&gnss_request_mutex, K_FOREVER);
	if (gnss_request == status) {
		goto exit;
	}
	gnss_request = status;
	rc = etc_device_write_setting(ETC_GNSS_LOCATION_REQUEST_STATUS, &gnss_request,
				      sizeof(gnss_request));
exit:
	k_mutex_unlock(&gnss_request_mutex);

	if (status == ETC_GNSS_LOCATION_REQUESTED) {
		LOG_INF("Location request set");
	}

	return rc;
}

bool etc_device_is_location_requested(void)
{
	bool rc = false;

	k_mutex_lock(&gnss_request_mutex, K_FOREVER);
	if (gnss_request == ETC_GNSS_LOCATION_REQUESTED) {
		rc = true;
	}
	k_mutex_unlock(&gnss_request_mutex);

	return rc;
}

int etc_device_set_last_time_gnss_request(time_t time_requested)
{
	int rc;

	if (time_last_gnss_request == time_requested) {
		return 0;
	}

	time_last_gnss_request = time_requested;
	rc = etc_device_write_setting(ETC_GNSS_TIME_LAST_REQUEST, &time_last_gnss_request,
				      sizeof(time_last_gnss_request));
	return rc;
}

time_t etc_device_get_last_time_gnss_request(void)
{
	return time_last_gnss_request;
}

int etc_device_set_location(struct etc_gnss_data *data)
{
	int rc;

	rc = etc_device_write_setting(ETC_GNSS_LAST_LOCATION, data, sizeof(*data));
	return rc;
}

int etc_device_retrieve_location(struct etc_gnss_data *data)
{
	int rc;

	rc = etc_device_read_setting(ETC_GNSS_LAST_LOCATION, data, sizeof(*data));
	return rc;
}

void etc_device_relay_write_record_sensor(struct sensor_data *sensor) 
{
	LOG_DBG("Write sensor data for relay");
	k_mutex_lock(&relay_data_sensor_mtx, K_FOREVER);
	current_relay_data_sensor.battery = (float)sensor->battery_mV / 1000.0;
	current_relay_data_sensor.flag = (uint32_t)(sensor->battery_status);
	current_relay_data_sensor.flag |= (uint32_t)(ETC_DEVICE_RELAY_DATA_READY_MASK);
	current_relay_data_sensor.timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		current_relay_data_sensor.sensor[i] = sensor->sensor[i];
	}
	k_mutex_unlock(&relay_data_sensor_mtx);
}

int etc_device_relay_read_record_sensor(union etc_device_record* record) {
	int rc = -EIO;
	k_mutex_lock(&relay_data_sensor_mtx, K_FOREVER);
	if (current_relay_data_sensor.flag & ETC_DEVICE_RELAY_DATA_READY_MASK) {
		current_relay_data_sensor.flag &= ~ETC_DEVICE_RELAY_DATA_READY_MASK;
		memcpy(record, &current_relay_data_sensor, sizeof(*record));
		rc = 0;
	}
	k_mutex_unlock(&relay_data_sensor_mtx);
	return rc;
}

void etc_device_sync_record_on_ram(void) {
	union etc_device_record record;
	for (int i = 0; i < p_record_backup->num_records; i++) {
		memcpy(&record, &p_record_backup->records[i], sizeof(record));
		etc_device_write_record(&record);
	}
	p_record_backup->num_records = 0;
	etc_device_record_backup_sync();
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_erase_configuration(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_erase_cfg();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_config,
	SHELL_CMD(erase, NULL, "Erase all configuration - development only", cmd_erase_configuration),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(config, &sub_config, "ETC Configuration Management", NULL);

#endif
