#include <zephyr/kernel.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include <math.h>
#include "data_codec.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(data_codec, CONFIG_ETC_APP_LOG_LEVEL);

#define DATA_CODEC_BUFFER_MAX_SIZE 512

static char data_codec_buffer_size[DATA_CODEC_BUFFER_MAX_SIZE];

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

char* data_codec_prepare_cloud_packet(struct data_lora_sensors *sensor_buf, size_t sensor_buf_count)
{
	int err;
	char *buffer;
	bool object_added = false;

	cJSON *root_obj = cJSON_CreateObject();

	if (root_obj == NULL) {
		cJSON_Delete(root_obj);
		return NULL;
	}

	cJSON_AddStringToObject(root_obj, "header", "[t0, t1, t2, t3, t4]");
	cJSON* data_obj = cJSON_CreateArray();
	if (data_obj == NULL) {
		LOG_ERR("Can't create data obj for snapshot payload");
		cJSON_Delete(root_obj);
		return NULL;
	}

	cJSON_AddItemToObject(root_obj, "data", data_obj);
	for (int i = 0; i < sensor_buf_count; i++) {
                struct data_lora_sensors *sensor = &sensor_buf[i];
		if (!sensor->queued) {
			continue;
		}
		cJSON* item = cJSON_CreateObject();
		char key[16] = {0x00};
		sprintf(key, "%d", (uint32_t)sensor->env_ts);
		cJSON_AddStringToObject(item, key, sensor->sensor_msg);
		cJSON_AddItemToArray(data_obj, item);
	}

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
