#include <zephyr/kernel.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include <math.h>
#include "data_codec.h"
#include <zephyr/logging/log.h>
#include <string.h>
LOG_MODULE_REGISTER(data_codec, CONFIG_ETC_APP_LOG_LEVEL);

#define DATA_CODEC_BUFFER_MAX_SIZE 512

static char data_codec_buffer_size[DATA_CODEC_BUFFER_MAX_SIZE];

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

static cJSON *create_data_arr_logger(struct data_sensors *sens_data[],
				uint8_t sens_data_len,
				struct data_battery *batt_data) 
{
	cJSON *data_arr;
	cJSON *item;

	data_arr = cJSON_CreateArray();
	/* sensor_id */
	item = cJSON_CreateNumber(123456);
	cJSON_AddItemToArray(data_arr, item);
	/* time */
	item = cJSON_CreateNumber(0);
	cJSON_AddItemToArray(data_arr, item);
	/* batt */
	item = cJSON_CreateNumber(batt_data->bat);
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
	
	for (int i = 0; i < sens_data_len; i++) {
		item = cJSON_CreateNumber(sens_data[i]->temperature);
		cJSON_AddItemToArray(data_arr, item);
	}

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
				struct data_modem_static *modem_data,
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

	if (modem_data != NULL) {
		cJSON_AddStringToObject(root_obj, "modem_id", modem_data->imei);
	}
	cJSON_AddNumberToObject(root_obj, "time", 0);
	if (batt_data != NULL) {
		cJSON_AddNumberToObject(root_obj, "batt", batt_data->bat);
	}
	cJSON_AddNumberToObject(root_obj, "sig", -50);
	cJSON_AddStringToObject(root_obj, "fw", "1.0");
	/* Packet counter that increments with every packet. */
	cJSON_AddNumberToObject(root_obj, "packet", pckt_cnt);

	struct_arr = cJSON_CreateStringArray(struct_strings, ARRAY_SIZE(struct_strings));
	cJSON_AddItemToObject(root_obj, "structure", struct_arr);

	pckt_cnt++;

	return 0;
} 

char *data_codec_prepare_logger_packet(struct data_sensors *sens_data[],
				uint8_t sens_data_len,
				struct data_modem_static *modem_data,
				struct data_battery *batt_data) 
{
	int err;
	char *buffer;
	bool object_added = false;
	cJSON *data_arr;
	cJSON *arr;

	cJSON *root_obj = cJSON_CreateObject();
	if (root_obj == NULL) {
		goto exit;
	}

	create_packet_header(root_obj, modem_data, batt_data);

	arr = cJSON_CreateArray();
	cJSON_AddItemToObject(root_obj, "data", arr);

	data_arr = create_data_arr_logger(sens_data, sens_data_len, batt_data);
	if (data_arr == NULL) {
		goto exit;
	}
	cJSON_AddItemToArray(arr, data_arr);

	bool ret = cJSON_PrintPreallocated(root_obj, data_codec_buffer_size, sizeof(data_codec_buffer_size), false);
	if (ret == false) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto exit;
	}

	return data_codec_buffer_size;

exit:
	cJSON_Delete(root_obj);
	return NULL;
}

char* data_codec_prepare_cloud_packet(struct data_lora_sensors *sensor_buf, 
				size_t sensor_buf_count,
				struct data_modem_static *modem_data,
				struct data_battery *batt_data)
{
	int err;
	bool object_added = false;
	cJSON *data_arr;
	bool ret = false;

	cJSON *root_obj = cJSON_CreateObject();

	if (root_obj == NULL) {
		goto exit;
	}

	create_packet_header(root_obj, modem_data, batt_data);

	cJSON* data_obj = cJSON_CreateArray();
	if (data_obj == NULL) {
		LOG_ERR("Can't create data obj for snapshot payload");
		goto exit;
	}

	cJSON_AddItemToObject(root_obj, "data", data_obj);
	for (int i = 0; i < sensor_buf_count; i++) {
        	struct data_lora_sensors *sensor = &sensor_buf[i];
		if (!sensor->queued) {
			continue;
		}
		data_arr = create_data_arr_lora(sensor);
		if (data_arr != NULL) {
			cJSON_AddItemToArray(data_obj, data_arr);
		}
	}

	ret = cJSON_PrintPreallocated(root_obj, data_codec_buffer_size, 
				sizeof(data_codec_buffer_size), false);
	if (ret == false) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto exit;
	}

	return data_codec_buffer_size;
exit:
	cJSON_Delete(root_obj);
	return NULL;
}
