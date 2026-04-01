#include "etc_device.h"
#include "etc_sensor.h"
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/random/random.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include "etc_settings.h"
#include "etc_device_record.h"
#include "etc_img.h"
#include "etc_memfault.h"
#include "etc_memfault_metrics.h"
#include "etc_device_helper.h"
#include "etc_date_time.h"
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_device, CONFIG_ETC_APP_LOG_LEVEL);

#define ETC_SETTINGS_NODE_LABEL etc_settings_storage

#define ETC_RECORD_DEFAULT_RX_DURATION_SECONDS	(120)
#define ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS (60)
#define ETC_RECORD_DEFAULT_TX_INTERVAL_SECONDS	(300)
#define ETC_RECORD_DEFAULT_TX_PROBE_SECONDS	(21600)
#define ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE	(15)

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
static uint16_t tx_lora_cloud_sync_hour;
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

static struct etc_device_record_backup_data *p_record_backup = NULL;

/* The public key ID of the current (signed) image. The public key ID
 * is the first 4 bytes of the public key hash.
 */
static uint8_t img_pubkey_id[IMG_PUBKEY_ID_LEN];

static struct etc_device_reclaim_request list_reclaim_request[ETC_RECLAIM_RELAY_MAX_ELEMENT];
K_MUTEX_DEFINE(reclaim_request_mtx);

static const struct device *reclaim_ram_dev = DEVICE_DT_GET(DT_ALIAS(reclaim_request_ram));

#define ETC_RECLAIM_RAM_MAGIC	      0xDEADC0DEU
#define ETC_RECLAIM_STALE_TIMEOUT_SEC (5 * 24 * 3600)
#define RECLAIM_RAM_MAGIC_OFFSET      0
#define RECLAIM_RAM_DATA_OFFSET	      sizeof(uint32_t)
#define RECLAIM_RAM_CRC_OFFSET                                                                     \
	(RECLAIM_RAM_DATA_OFFSET +                                                                 \
	 sizeof(struct etc_reclaim_request_ram) * ETC_RECLAIM_RELAY_MAX_ELEMENT)

#pragma pack(push, 1)
struct etc_reclaim_request_ram {
	char logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE]; /* 17 */
	int32_t start_time;				/*  4 */
	int32_t stop_time;				/*  4 */
	int32_t created_at;				/*  4 */
	uint8_t active;					/*  1 */
	uint8_t _pad[2];				/*  2 */
}; /* = 32 bytes */
#pragma pack(pop)

static void etc_reclaim_list_zero(void)
{
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		atomic_set(&list_reclaim_request[i].flag_set, false);
		list_reclaim_request[i].start_time = 0;
		list_reclaim_request[i].stop_time = 0;
		list_reclaim_request[i].created_at = 0;
		list_reclaim_request[i].logger_id[0] = '\0';
	}
}

static void etc_persist_reclaim_request_list(void)
{
	struct etc_reclaim_request_ram store[ETC_RECLAIM_RELAY_MAX_ELEMENT];
	uint32_t magic = ETC_RECLAIM_RAM_MAGIC;

	memset(store, 0, sizeof(store));
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		store[i].active = (uint8_t)atomic_get(&req->flag_set);
		store[i].start_time = req->start_time;
		store[i].stop_time = req->stop_time;
		store[i].created_at = req->created_at;
		memcpy(store[i].logger_id, req->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE);
	}

	uint32_t crc = crc32_ieee((uint8_t *)store, sizeof(store));

	/* Write data first, then CRC, then magic — magic written last so a
	 * reboot mid-write leaves magic absent and the load path discards. */
	int rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_DATA_OFFSET, (uint8_t *)store,
				    sizeof(store));
	if (rc) {
		LOG_ERR("Failed to write reclaim data to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_CRC_OFFSET, (uint8_t *)&crc,
				sizeof(crc));
	if (rc) {
		LOG_ERR("Failed to write reclaim CRC to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				sizeof(magic));
	if (rc) {
		LOG_ERR("Failed to write reclaim magic to retained RAM: %d", rc);
	}
}

static void etc_load_reclaim_request_list(void)
{
	struct etc_reclaim_request_ram store[ETC_RECLAIM_RELAY_MAX_ELEMENT];
	uint32_t magic = 0;
	uint32_t stored_crc = 0;

	if (!device_is_ready(reclaim_ram_dev)) {
		LOG_ERR("Reclaim retained-mem device not ready");
		etc_reclaim_list_zero();
		return;
	}

	int rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				   sizeof(magic));
	if (rc || magic != ETC_RECLAIM_RAM_MAGIC) {
		LOG_INF("Reclaim retained RAM: no valid data (magic=0x%08x), starting empty",
			magic);
		etc_reclaim_list_zero();
		etc_persist_reclaim_request_list();
		return;
	}

	rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_DATA_OFFSET, (uint8_t *)store,
			       sizeof(store));
	if (rc) {
		LOG_ERR("Failed to read reclaim data from retained RAM: %d", rc);
		etc_reclaim_list_zero();
		return;
	}

	rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_CRC_OFFSET, (uint8_t *)&stored_crc,
			       sizeof(stored_crc));
	if (rc || stored_crc != crc32_ieee((uint8_t *)store, sizeof(store))) {
		LOG_WRN("Reclaim retained RAM: CRC mismatch, discarding");
		etc_reclaim_list_zero();
		etc_persist_reclaim_request_list();
		return;
	}

	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		atomic_set(&req->flag_set, store[i].active);
		req->start_time = store[i].start_time;
		req->stop_time = store[i].stop_time;
		req->created_at = store[i].created_at;
		memcpy(req->logger_id, store[i].logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE);
	}
	LOG_DBG("Reclaim request list restored from retained RAM");
}

void etc_device_nvs_init(void)
{
	int rc = 0;
	struct flash_pages_info info;
	etc_fs.flash_device = FLASH_AREA_DEVICE(ETC_SETTINGS_NODE_LABEL);
	if (!device_is_ready(etc_fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", etc_fs.flash_device->name);
		return;
	}

	etc_fs.offset = FIXED_PARTITION_OFFSET(ETC_SETTINGS_NODE_LABEL);
	rc = flash_get_page_info_by_offs(etc_fs.flash_device, etc_fs.offset, &info);
	if (rc) {
		LOG_DBG("Unable to get page info");
	}

	etc_fs.sector_size = info.size;
	etc_fs.sector_count = (FIXED_PARTITION_SIZE(ETC_SETTINGS_NODE_LABEL) / info.size);
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
		FIXED_PARTITION_SIZE(ETC_SETTINGS_NODE_LABEL), info.size, etc_fs.sector_count);
	LOG_DBG("Initialised etc setting successfully");
	etc_device_record_init();
	/* Load the record backup */
	p_record_backup = etc_device_record_backup_get_object();
	/* Sync last record backup */
	etc_device_sync_record_on_ram();
	/* Restore reclaim request list from retained RAM */
	etc_load_reclaim_request_list();
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
		LOG_ERR("Failed in reading NVS %d %d", element_id, read_len);
		return read_len;
	}

	if (read_len > len) {
		LOG_WRN("Read length is higher than request read %d %d %d", element_id, len,
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
		LOG_ERR("Failed in reading NVS %d %d", element_id, read_len);
	}

	return read_len;
}

static int etc_nvs_reset_relay_stat(void)
{
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
	tx_logger_lora_offset_mins =
		(uint16_t)((sys_rand32_get() % 4) * ETC_DEVICE_TX_NO_PROBE_OFFSET_MINUTE);
	tx_lora_cloud_sync_hour =
		(uint16_t)((sys_rand32_get() % ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_HOUR_OFFSET_MAX) +
			   ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_HOUR);
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

int etc_device_write_record_sensor(struct sensor_data *sensor)
{
	int ret;
	int64_t time_start = k_uptime_get();
	union etc_device_record record;
	memset(record.data, 0, sizeof(record.data));
	ret = etc_device_pack_sensor_data(sensor, &record);
	if (ret) {
		LOG_ERR("Failed to prepare sensor data %d", ret);
		return ret;
	}
	ret = etc_device_write_record(&record);
	LOG_DBG("NVS time record %d: %lld", ret, k_uptime_get() - time_start);
	return ret;
}

int etc_device_write_record(union etc_device_record *record)
{
	struct etc_device_record_index record_index = etc_device_record_get_next_index();
	off_t record_addr = etc_device_record_get_addr_offset_by_index(&record_index);
	int rc = etc_device_record_write_data(record_addr, record->data, ETC_DEVICE_RECORD_SIZE);
	if (rc) {
		LOG_ERR("Can't write data to record err %d", rc);
		return rc;
	}
	/* Sync data */
	uint16_t record_id = etc_device_record_get_latest_id();
	struct etc_device_record_index wrote_record_index = etc_device_get_index_by_id(record_id);
	off_t recorded_addr = etc_device_record_get_addr_offset_by_id(record_id);
	LOG_DBG("Record to write data %d (0x%08x / 0x%08x) (%d,%d) -> (%d,%d)",
		ETC_RECORD_ID_HEADER(record_id), (uint32_t)record_addr, (uint32_t)recorded_addr,
		record_index.sector_idx, record_index.element_idx, wrote_record_index.sector_idx,
		wrote_record_index.element_idx);

	etc_device_record_set_nack(record_id);
	etc_device_record_save_stat();
	return 0;
}

int etc_device_read_record(union etc_device_record *record, bool *active_reclaim)
{
	int record_id =
		etc_device_record_find_nack(etc_device_record_reading, record, active_reclaim);
	if (record_id < 0) {
		LOG_WRN("Don't have NACK record");
		return 0;
	}
	int ret = etc_device_unpack_sensor_data(record);
	if (ret < 0) {
		LOG_ERR("Failed to unpack sensor data %d", ret);
		/* Skip record if parsing sensor data failed. */
		etc_device_set_ack_record(record_id);
		return ret;
	}
	etc_device_map_embeddable_sensor_data(record);
	LOG_DBG("Record ID %d", record_id);
	return record_id;
}

int etc_device_write_relay_data(struct etc_device_relay_record *record)
{
	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	if (p_relay_stat->number_record < ETC_RELAY_RECORD_MAX_ELEMENT) {
		memcpy(&relay_record_list[p_relay_stat->write_index], record, sizeof(*record));
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
	memcpy(record, &relay_record_list[p_relay_stat->read_index], sizeof(*record));
	p_relay_stat->last_read_count = 1;
	k_mutex_unlock(&etc_relay_record_mutex);
	return 0;
}

static struct etc_device_relay_record *get_relay_data_at_index(uint8_t index)
{
	uint8_t relay_record_index;
	if ((index + 1) > p_relay_stat->number_record) {
		return NULL;
	}

	relay_record_index = (p_relay_stat->read_index + index) % ETC_RELAY_RECORD_MAX_ELEMENT;
	return &relay_record_list[relay_record_index];
}

int etc_device_read_relay_data_packet(struct etc_device_relay_packet *packet)
{
	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	if (p_relay_stat->number_record == 0) {
		k_mutex_unlock(&etc_relay_record_mutex);
		return -ENODATA;
	}
	if (p_relay_stat->number_record > ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS) {
		packet->num_records = ETC_DEVICE_RELAY_PACKAGE_MAX_RECORDS;
	} else {
		packet->num_records = p_relay_stat->number_record;
	}

	for (int i = 0; i < packet->num_records; i++) {
		memcpy(&packet->records[i], get_relay_data_at_index(i), sizeof(packet->records[i]));
	}
	/* Save number of records read */
	p_relay_stat->last_read_count = packet->num_records;
	k_mutex_unlock(&etc_relay_record_mutex);
	return 0;
}

int etc_device_sync_relay_data(void)
{
	int retval = 0;

	k_mutex_lock(&etc_relay_record_mutex, K_FOREVER);
	if (p_relay_stat->last_read_count == 0) {
		retval = -ENODATA;
		goto exit;
	}
	if (p_relay_stat->number_record < p_relay_stat->last_read_count) {
		LOG_WRN("Sync record invalid %d %d", p_relay_stat->number_record,
			p_relay_stat->last_read_count);
		ETC_MEMFAULT_TRACE_EVENT_WITH_LOG(
			relay_sync_record_invalid, "Sync record invalid %d %d",
			p_relay_stat->number_record, p_relay_stat->last_read_count);
		retval = -ENODATA;
		goto exit;
	}
	p_relay_stat->read_index = (p_relay_stat->read_index + p_relay_stat->last_read_count) %
				   ETC_RELAY_RECORD_MAX_ELEMENT;
	p_relay_stat->number_record -= p_relay_stat->last_read_count;
	p_relay_stat->last_read_count = 0;
exit:
	k_mutex_unlock(&etc_relay_record_mutex);
	return retval;
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
	return (etc_device_record_get_total_record() - etc_device_record_get_num_ack());
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

bool etc_device_is_always_on(void)
{
	return (etc_get_power_mode() == ETC_POWER_MODE_ALWAYS_ON);
}

int etc_device_get_rx_duration(void)
{
	int rx_duration = etc_get_rx_duration_secs();
	return rx_duration == 0 ? ETC_RECORD_DEFAULT_RX_DURATION_SECONDS : rx_duration;
}

int etc_device_get_log_interval_second(void)
{
	int second = etc_get_log_interval_secs();
	return second == 0 ? ETC_RECORD_DEFAULT_LOG_INTERVAL_SECONDS : second;
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

void etc_device_set_transmit_sub_job(enum etc_transmit_sub_job job)
{
	transmit_sub_job = job;
}

enum etc_transmit_sub_job etc_device_get_transmit_sub_job(void)
{
	return transmit_sub_job;
}

int etc_device_erase_cfg(void)
{
	int rc = 0;
	/* Erase config, record and reclaim */
	for (int id = ETC_CONFIG_ID; id <= ETC_RECORD_RECLAIM; id++) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase setting */
	for (int id = ETC_SETTING_TIME_MEASURE_INTERVAL_ID; id <= ETC_SETTING_TX_PROBE_SEC_ID;
	     id++) {
		rc = nvs_delete(&etc_fs, id);
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Erase record RAM and sub-sys settings */
	etc_device_record_clean_up();
	/* Clear reclaim request retained RAM */
	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	etc_reclaim_list_zero();
	etc_persist_reclaim_request_list();
	k_mutex_unlock(&reclaim_request_mtx);
	return 0;
}

uint16_t etc_device_get_tx_logger_lora_offset_mins(void)
{
	return tx_logger_lora_offset_mins;
}

uint16_t etc_device_get_tx_lora_cloud_sync_hour(void)
{
	return tx_lora_cloud_sync_hour;
}

uint16_t etc_device_get_tx_no_probe_offset_mins(void)
{
	uint16_t probe_offset = 0;
	if (etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) {
		probe_offset = etc_get_lora_probe_offset_secs();
	} else if (etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) {
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
	current_relay_data_sensor.battery = (double)sensor->battery_mV / 1000.0;
	current_relay_data_sensor.flag = (uint32_t)(sensor->battery_status);
	current_relay_data_sensor.flag |= (uint32_t)(ETC_DEVICE_RELAY_DATA_READY_MASK);
	current_relay_data_sensor.timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		current_relay_data_sensor.sensor[i] = sensor->sensor[i];
	}
	k_mutex_unlock(&relay_data_sensor_mtx);
}

int etc_device_relay_read_record_sensor(union etc_device_record *record)
{
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

void etc_device_sync_record_on_ram(void)
{
	union etc_device_record record;
	for (int i = 0; i < p_record_backup->num_records; i++) {
		memcpy(&record, &p_record_backup->records[i], sizeof(record));
		etc_device_write_record(&record);
	}
	p_record_backup->num_records = 0;
	etc_device_record_backup_sync();
}

static void etc_reclaim_slot_assign(struct etc_device_reclaim_request *slot, const char *logger_id,
				    int start_time, int stop_time, int32_t created_at)
{
	strncpy(slot->logger_id, logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE - 1);
	slot->logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE - 1] = '\0';
	slot->start_time = start_time;
	slot->stop_time = stop_time;
	slot->created_at = created_at;
	atomic_set(&slot->flag_set, true);
}

int etc_set_reclaim_request_for_relay(char *logger_id, int start_time, int stop_time)
{
	if (!etc_device_is_relay()) {
		return -EINVAL;
	}

	if (start_time > stop_time) {
		return -EINVAL;
	}

	int now = date_time_now_second();
	int32_t created = (now >= 0) ? (int32_t)now : 0;

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);

	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *request = &list_reclaim_request[i];
		if (atomic_get(&request->flag_set)) {
			continue;
		}

		etc_reclaim_slot_assign(request, logger_id, start_time, stop_time, created);
		etc_persist_reclaim_request_list();
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_DBG("Added reclaim request for %s [%d - %d]", logger_id, start_time, stop_time);
		return 0;
	}

	/* No free slot — evict the oldest stale entry if it is old enough */
	if (now < 0) {
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_WRN("RTC not synced, cannot evict stale reclaim entry");
		return -ENOMEM;
	}

	int evict_idx = -1;
	int32_t oldest_created = INT32_MAX;
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (req->created_at > 0 &&
		    (now - req->created_at) >= ETC_RECLAIM_STALE_TIMEOUT_SEC) {
			if (req->created_at < oldest_created) {
				oldest_created = req->created_at;
				evict_idx = i;
			}
		}
	}

	if (evict_idx < 0) {
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_WRN("Reclaim buffer full, no stale entry to evict");
		return -ENOMEM;
	}

	struct etc_device_reclaim_request *slot = &list_reclaim_request[evict_idx];
	LOG_INF("Evicting stale reclaim entry for %s (created=%d)", slot->logger_id,
		slot->created_at);
	etc_reclaim_slot_assign(slot, logger_id, start_time, stop_time, created);
	etc_persist_reclaim_request_list();
	k_mutex_unlock(&reclaim_request_mtx);
	LOG_DBG("Added reclaim request for %s [%d - %d] (evicted stale)", logger_id, start_time,
		stop_time);
	return 0;
}

int etc_get_reclaim_request_for_relay_with_logger_id(
	const char *logger_id, struct etc_device_reclaim_request *reclaim_request)
{
	if (logger_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *request = &list_reclaim_request[i];
		if (!atomic_get(&request->flag_set)) {
			continue;
		}
		if (strncmp(logger_id, request->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) == 0) {
			memcpy(reclaim_request, request, sizeof(*reclaim_request));
			k_mutex_unlock(&reclaim_request_mtx);
			return 0;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	return -ENOENT;
}

int etc_clear_reclaim_request_if_satisfied(const char *logger_id, int timestamp)
{
	if (logger_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (!atomic_get(&req->flag_set)) {
			continue;
		}
		if (strncmp(logger_id, req->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) != 0) {
			continue;
		}
		int32_t start = req->start_time;
		int32_t stop = req->stop_time;
		if (timestamp >= start && timestamp <= stop) {
			atomic_set(&req->flag_set, false);
			req->start_time = 0;
			req->stop_time = 0;
			req->created_at = 0;
			req->logger_id[0] = '\0';
			etc_persist_reclaim_request_list();
			k_mutex_unlock(&reclaim_request_mtx);
			LOG_INF("Reclaim request for %s satisfied by ts=%d [%d-%d]", logger_id,
				timestamp, start, stop);
			return 0;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	return -ENOENT;
}

int etc_device_verify_to_set_psm(const struct etc_config *new_config)
{
	if (etc_device_is_relay()) {
		if ((new_config->power_mode != ETC_POWER_MODE_ALWAYS_ON) &&
		    (etc_device_is_always_on())) {
			return 1;
		}

		if ((new_config->device_mode != ETC_DEVICE_MODE_RELAY) &&
		    (etc_device_is_always_on())) {
			return 1;
		}

		if ((new_config->power_mode == ETC_POWER_MODE_ALWAYS_ON) &&
		    (!etc_device_is_always_on())) {
			return 2;
		}
	}
	return 0;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_erase_configuration(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_erase_cfg();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config,
			       SHELL_CMD(erase, NULL, "Erase all configuration - development only",
					 cmd_erase_configuration),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(config, &sub_config, "ETC Configuration Management", NULL);

static int cmd_relay_reclaim_set(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 4 || argc > 5) {
		shell_error(shell,
			    "Usage: relay_reclaim set <logger_id> <start> <stop> [created_at]");
		return -EINVAL;
	}
	char *logger_id = argv[1];
	int start = (int)strtol(argv[2], NULL, 10);
	int stop = (int)strtol(argv[3], NULL, 10);
	int rc = etc_set_reclaim_request_for_relay(logger_id, start, stop);
	if (rc) {
		shell_error(shell, "Failed to set reclaim request: %d", rc);
		return rc;
	}
	/* Optional: override created_at for testing stale eviction */
	if (argc == 5) {
		int32_t created = (int32_t)strtol(argv[4], NULL, 10);
		k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
		for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
			struct etc_device_reclaim_request *req = &list_reclaim_request[i];
			if (atomic_get(&req->flag_set) &&
			    strncmp(req->logger_id, logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) ==
				    0 &&
			    req->start_time == start && req->stop_time == stop) {
				req->created_at = created;
				break;
			}
		}
		etc_persist_reclaim_request_list();
		k_mutex_unlock(&reclaim_request_mtx);
	}
	shell_print(shell, "Reclaim request set for %s [%d - %d]", logger_id, start, stop);
	return 0;
}

static int cmd_relay_reclaim_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int count = 0;

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (atomic_get(&req->flag_set)) {
			shell_print(shell, "[%d] %s start=%d stop=%d", i, req->logger_id,
				    req->start_time, req->stop_time);
			count++;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	if (count == 0) {
		shell_print(shell, "No active reclaim requests");
	}
	return 0;
}

static int cmd_relay_reclaim_clear_all(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	etc_reclaim_list_zero();
	etc_persist_reclaim_request_list();
	k_mutex_unlock(&reclaim_request_mtx);
	shell_print(shell, "All reclaim requests cleared");
	return 0;
}

static int cmd_relay_reclaim_satisfy(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(shell, "Usage: relay_reclaim satisfy <logger_id> <timestamp>");
		return -EINVAL;
	}
	char *logger_id = argv[1];
	int timestamp = (int)strtol(argv[2], NULL, 10);
	int rc = etc_clear_reclaim_request_if_satisfied(logger_id, timestamp);
	if (rc == 0) {
		shell_print(shell, "Reclaim satisfied for %s at ts=%d", logger_id, timestamp);
	} else {
		shell_print(shell, "No matching reclaim request for %s at ts=%d", logger_id,
			    timestamp);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_relay_reclaim,
	SHELL_CMD_ARG(set, NULL, "Set reclaim request: <logger_id> <start> <stop> [created_at]",
		      cmd_relay_reclaim_set, 4, 1),
	SHELL_CMD(list, NULL, "List active reclaim requests", cmd_relay_reclaim_list),
	SHELL_CMD(clear_all, NULL, "Clear all reclaim requests", cmd_relay_reclaim_clear_all),
	SHELL_CMD_ARG(satisfy, NULL, "Satisfy reclaim request: <logger_id> <timestamp>",
		      cmd_relay_reclaim_satisfy, 3, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(relay_reclaim, &sub_relay_reclaim, "Relay reclaim request management", NULL);

#endif
