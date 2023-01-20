#include <zephyr/kernel.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include <math.h>
#include <string.h>
#include "etc_date_time.h"
#include "data_codec.h"
#include "app_version.h"
#include "etc_settings.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(data_codec, CONFIG_ETC_APP_LOG_LEVEL);

#define DATA_CODEC_BUFFER_MAX_SIZE 512
#define DATA_CODEC_TEMP_BUFFER_MAX_SIZE 32

static char data_codec_buffer[DATA_CODEC_BUFFER_MAX_SIZE];
static char data_codec_temp_buffer[DATA_CODEC_TEMP_BUFFER_MAX_SIZE];

/* External declarations */
extern int quectel_bg95_get_rssi(void);

static inline bool is_digit(char in) {
	if (in >= '0' && in <= '9') {
		return true;
	}
	return false;
}

void data_codec_populate_lora_sensor_buffer(
				struct data_lora_sensors *sensor_buffer,
				struct data_lora_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count)
{
	if (!new_sensor_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	if (*head_sensor_buf == buffer_count) {
		*head_sensor_buf = 0;
	}

	sensor_buffer[*head_sensor_buf] = *new_sensor_data;
	*head_sensor_buf += 1;
	LOG_DBG("Entry: %d of %d in sensor buffer filled", *head_sensor_buf,
		buffer_count - 1);
}

void data_codec_populate_sensor_internal_buffer(
				struct data_sensors *sensor_buffer,
				struct data_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count)
{
	if (!new_sensor_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	if (*head_sensor_buf == buffer_count) {
		*head_sensor_buf = 0;
	}

	sensor_buffer[*head_sensor_buf] = *new_sensor_data;
	*head_sensor_buf += 1;
	LOG_DBG("Entry: %d of %d in sensor buffer filled", *head_sensor_buf,
		buffer_count - 1);
}

static cJSON *create_sensor_value_item(float temperature)
{
	cJSON *item;

	if (data_codec_compare_temperature_is_valid(temperature)) {
		snprintf(data_codec_temp_buffer, sizeof(data_codec_temp_buffer),
			"%2.2f", temperature);
		item = cJSON_CreateRaw(data_codec_temp_buffer);
	} else {
		item = cJSON_CreateString("*");
	}

	return item;
}

static cJSON *create_data_arr_logger(struct data_sensors *sens_data,
				     struct data_modem_static *modem_data,
				     char *device_id) 
{
	cJSON *data_arr;
	cJSON *item;
	char *id = "";
	uint16_t bat = 0;

	if (device_id != NULL) {
		id = device_id;
	}

	data_arr = cJSON_CreateArray();
	/* sensor_id */
	item = cJSON_CreateString(id);
	cJSON_AddItemToArray(data_arr, item);
	/* time */
	item = cJSON_CreateNumber(sens_data->data.timestamp);
	cJSON_AddItemToArray(data_arr, item);
	/* batt */
	float battery_V = (float)sens_data->data.battery_mV / 1000.0;
	snprintf(data_codec_temp_buffer, sizeof(data_codec_temp_buffer), "%1.2f", battery_V);
	item = cJSON_CreateRaw(data_codec_temp_buffer);
	cJSON_AddItemToArray(data_arr, item);
	/* sig */
	item = cJSON_CreateNumber(quectel_bg95_get_rssi());
	cJSON_AddItemToArray(data_arr, item);
	/* fw */
	item = cJSON_CreateString(APP_VERSION_STR);
	cJSON_AddItemToArray(data_arr, item);
	/* packet */
	item = cJSON_CreateNumber(0);
	cJSON_AddItemToArray(data_arr, item);
	
	for (int i = SENSOR_INPUT_IN1; i < SENSOR_INPUT_MAX; i++) {
		item = create_sensor_value_item(sens_data->data.temperature[i]);
		cJSON_AddItemToArray(data_arr, item);
	}
	item = create_sensor_value_item(sens_data->data.temperature[SENSOR_INPUT_AMBIENT]);
	cJSON_AddItemToArray(data_arr, item);


	return data_arr;
}

static cJSON *create_data_arr_lora(struct data_lora_sensors *sensor) 
{
	const char *sep = ",";
	cJSON *data_arr;
	cJSON *item;
	char *state;
	char *ret;

	if (sensor == NULL) {
		return NULL;
	}

	data_arr = cJSON_CreateArray();
	if (data_arr == NULL) {
		return NULL;
	}
	/* sensor_id */
	item = cJSON_CreateNumber(sensor->env_ts);
	cJSON_AddItemToArray(data_arr, item);
	/* time */
	item = cJSON_CreateNumber(0);
	cJSON_AddItemToArray(data_arr, item);
	/* batt */
	item = cJSON_CreateNumber(4.2f);
	cJSON_AddItemToArray(data_arr, item);
	/* sig */
	item = cJSON_CreateNumber(-50);
	cJSON_AddItemToArray(data_arr, item);
	/* fw */
	item = cJSON_CreateString("1.0");
	cJSON_AddItemToArray(data_arr, item);
	/* packet */
	item = cJSON_CreateNumber(0);
	cJSON_AddItemToArray(data_arr, item);
	
	/* Parse LoRa string and add data to array. */
	ret = strtok(sensor->sensor_msg, sep);
	item = NULL;
	while (ret != NULL) {
		if (*ret == '*') {
			item = cJSON_CreateString("*");
		} else if (is_digit(*ret)) { 
			item = cJSON_CreateRaw(ret);
		}
		if (item != NULL) {
			cJSON_AddItemToArray(data_arr, item);
		}
		item = NULL;
		ret = strtok(NULL, sep);
	}

	return data_arr;
}

static int create_packet_header(cJSON *root_obj,
				char *device_id,
				struct data_battery *batt_data) 
{	
	const char* struct_strings[] = {
		"sensor_id",
		"time",
		"batt",
		"sig",
		"fw",
		"packet",
		"v1",
		"v2",
		"v3",
		"v4",
		"v5",
		"v6"
	};
	static uint8_t pckt_cnt = 0;
	cJSON *struct_arr;

	if (root_obj == NULL) {
		return -EINVAL;
	}

	if (device_id != NULL) {
		cJSON_AddStringToObject(root_obj, "modem_id", device_id);
	}
	cJSON_AddNumberToObject(root_obj, "time", date_time_now_second());
	if (batt_data != NULL) {
		cJSON_AddNumberToObject(root_obj, "batt", batt_data->data.battery_mV);
	}
	cJSON_AddNumberToObject(root_obj, "sig", quectel_bg95_get_rssi());
	cJSON_AddStringToObject(root_obj, "fw", APP_VERSION_STR);
	/* Packet counter that increments with every packet. */
	cJSON_AddNumberToObject(root_obj, "packet", pckt_cnt);

	struct_arr = cJSON_CreateStringArray(struct_strings, ARRAY_SIZE(struct_strings));
	cJSON_AddItemToObject(root_obj, "structure", struct_arr);

	pckt_cnt++;

	return 0;
} 

char* data_codec_prepare_cloud_packet(struct data_lora_sensors *lora_buffer, 
				size_t lora_buf_count,
				struct data_sensors *sensor_buffer,
				size_t sensor_buf_count,
				struct data_modem_static *modem_data,
				struct data_battery *batt_data)
{
	int err;
	char *buffer;
	bool object_added = false;
	cJSON *data_arr;
	cJSON *arr;
	char *retval = NULL;
	char device_id[ETC_SETTINGS_DEVICE_ID_LEN];

	cJSON *root_obj = cJSON_CreateObject();
	if (root_obj == NULL) {
		goto exit;
	}

	/* Retrieve device ID from settings */
	etc_get_device_id(device_id, sizeof(device_id));

	create_packet_header(root_obj, device_id, batt_data);

	arr = cJSON_CreateArray();
	cJSON_AddItemToObject(root_obj, "data", arr);
	
	for (int i = 0; i < sensor_buf_count; i++) {
		data_arr = create_data_arr_logger(&sensor_buffer[i],  modem_data,
						  device_id);
		if (data_arr == NULL) {
			goto exit;
		}
		cJSON_AddItemToArray(arr, data_arr);
	}

	bool ret = cJSON_PrintPreallocated(root_obj, data_codec_buffer, sizeof(data_codec_buffer), false);
	if (ret == false) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto exit;
	}

	retval = data_codec_buffer;

exit:
	cJSON_Delete(root_obj);
	return retval;
}
