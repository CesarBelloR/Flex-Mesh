#include <zephyr/kernel.h>
#include <string.h>

#include "data_codec.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(data_codec, CONFIG_ETC_APP_LOG_LEVEL);


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