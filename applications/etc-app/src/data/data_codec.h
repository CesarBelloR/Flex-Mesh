#ifndef DATA_CODEC_H__
#define DATA_CODEC_H__

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "events/lora_event.h"
#include "events/sensor_event.h"

/** @brief Structure containing battery data published to cloud. */
struct data_battery {
	/** Battery voltage level. */
	uint16_t bat;
	/** Battery data timestamp. UNIX milliseconds. */
	int64_t bat_ts;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_sensors {
	struct sensor_data data;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_modem_static {
	/** Static modem data timestamp. UNIX milliseconds. */
	int64_t ts;
	/** Device board version. */
	char brdv[30];
	/** Modem firmware. */
	char fw[40];
	/** Device IMEI. */
	char imei[16];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_lora_sensors {
	int64_t env_ts;
	char sensor_msg[LORA_EVENT_MSG_DATA_LEN];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

/** @brief Type of data to be handled by the respective API. Used to signify what data structure
 *         that is passed in to the function.
 */
enum json_common_buffer_type {
	JSON_COMMON_SENSOR,
	JSON_COMMON_LORA_SENSOR,
	JSON_COMMON_COUNT
};

typedef union {
	struct data_lora_sensors lora;
	struct data_battery battery;
	struct data_sensors sensor;
} data_etc_sensors;

/** @brief Operation to be carried out with the passed in data. */
enum json_common_op_code {
	JSON_COMMON_INVALID,
	/** Encode data and add it to a passed in parent array object. This option does not
	 *  label the encoded data.
	 */
	JSON_COMMON_ADD_DATA_TO_ARRAY,
	/** Encode data and add it to a passed in parent object. */
	JSON_COMMON_ADD_DATA_TO_OBJECT,
	/** Encode data and set the passed in object pointer to point to it. */
	JSON_COMMON_GET_POINTER_TO_OBJECT
};

static inline bool data_codec_compare_temperature_is_valid(float temperature) {
	if ((temperature >= SENSOR_TEMP_C_MIN) && 
	    (temperature <= SENSOR_TEMP_C_MAX)) {
		return true;
	}
	return false;
}

void data_codec_populate_lora_sensor_buffer(
				struct data_lora_sensors *sensor_buffer,
				struct data_lora_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count);

void data_codec_populate_sensor_internal_buffer(
				struct data_sensors *sensor_buffer,
				struct data_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count);

char* data_codec_prepare_cloud_packet(struct data_lora_sensors *lora_buffer, 
				size_t lora_buf_count,
				struct data_sensors *sensor_buffer,
				size_t sensor_buf_count,
				struct data_modem_static *modem_data,
				struct data_battery *batt_data);
#endif /* DATA_CODEC_H__ */