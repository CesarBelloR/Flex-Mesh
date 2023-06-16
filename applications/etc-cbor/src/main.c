#include <stdio.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(cbor);

uint8_t zcbor_buffer[1024];
zcbor_state_t state[5];
zcbor_state_t *p_state_encode = &state[0];
zcbor_state_t *p_state_decode = &state[0];

#define SENSOR_NO_CONNECTED -273.150
/* Minimum sensor temperature that is a valid reading. */
#define SENSOR_TEMP_C_MIN	-40.0f
/* Maximum sensor temperature that is a valid reading. */
#define SENSOR_TEMP_C_MAX	120.0f

static inline bool data_codec_compare_temperature_is_valid(float temperature) {
	if ((temperature >= SENSOR_TEMP_C_MIN) && 
	    (temperature <= SENSOR_TEMP_C_MAX)) {
		return true;
	}
	return false;
}

#define SENSOR_RECORD_FW_VERSION_MAX_LEN (16)
#define SENSOR_RECORD_SENSOR_ELEMENT_LEN (6)
struct sensor_record {
	uint32_t sensor_id;
	uint32_t time;
	float batt;
	int8_t sig;
	char fw_version[SENSOR_RECORD_FW_VERSION_MAX_LEN];
	uint16_t packet;
	float value[SENSOR_RECORD_FW_VERSION_MAX_LEN];
};

struct sensor_record example_data[] = {
	{14399, -1, 4.10, -75, "0.1.0", -4, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.8},
	{11341, -1, 3.74, -83, "0.1.0", 44, SENSOR_NO_CONNECTED, 39.5, 39.2, 39.0, 39.1, 39.2},
	{13659, -1, 1.00, -68, "0.1.0", 90, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.5},
	{15178, -1, 4.09, -66, "0.1.1", 34, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.7},
	{11243, -1, 3.96, -55, "0.1.0", 69, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 21.5},
	{11243, 1636383609, 3.96, -55, "0.1.0", 70, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 21.5},
	{10619, -1, 4.09, -63, "0.1.0", 94, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.9},
	{11852, -1, 4.06, -60, "0.1.0", 15, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.9},
	{11243, 1636383609, 3.96, -58, "0.1.0", 71, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 21.4},
	{11152, 1636381002, 4.08, -74, "0.1.0", -4, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, SENSOR_NO_CONNECTED, 20.7},
};

void main(void)
{
	zcbor_new_encode_state(p_state_encode, 5, zcbor_buffer, sizeof(zcbor_buffer), 0);
	zcbor_map_start_encode(p_state_encode, 0);

		zcbor_tstr_put_lit(p_state_encode, "modem_id");
		zcbor_uint64_put(p_state_encode, 22222);
		
		zcbor_tstr_put_lit(p_state_encode, "time");
		zcbor_uint64_put(p_state_encode, 1636384512);

		zcbor_tstr_put_lit(p_state_encode, "batt");
		zcbor_float32_put(p_state_encode, 4.26);

		zcbor_tstr_put_lit(p_state_encode, "sig");
		zcbor_int32_put(p_state_encode, -51);

		zcbor_tstr_put_lit(p_state_encode, "fw");
		zcbor_tstr_put_term(p_state_encode, "0.1.0");

		zcbor_tstr_put_lit(p_state_encode, "packet");
		zcbor_uint32_put(p_state_encode, 2);

		zcbor_tstr_put_lit(p_state_encode, "structure");
		zcbor_list_start_encode(p_state_encode, 0);
			zcbor_tstr_put_term(p_state_encode, "sensor_id");
			zcbor_tstr_put_term(p_state_encode, "time");
			zcbor_tstr_put_term(p_state_encode, "batt");
			zcbor_tstr_put_term(p_state_encode, "sig");
			zcbor_tstr_put_term(p_state_encode, "fw");
			zcbor_tstr_put_term(p_state_encode, "packet");
			zcbor_tstr_put_term(p_state_encode, "v1");
			zcbor_tstr_put_term(p_state_encode, "v2");
			zcbor_tstr_put_term(p_state_encode, "v3");
			zcbor_tstr_put_term(p_state_encode, "v4");
			zcbor_tstr_put_term(p_state_encode, "v5");
			zcbor_tstr_put_term(p_state_encode, "v6");
		zcbor_list_end_encode(p_state_encode, 0);

		zcbor_tstr_put_lit(p_state_encode, "data");
		zcbor_list_start_encode(p_state_encode, 0);

		int num_element = sizeof(example_data) / sizeof(struct sensor_record);
		for (int i = 0; i < num_element; i++) {
			struct sensor_record record = example_data[i];
			zcbor_list_start_encode(p_state_encode, 1);
				zcbor_uint32_put(p_state_encode, record.sensor_id);
				if (record.time == -1) {
					zcbor_tstr_put_term(p_state_encode, "*");
				} else {
					zcbor_uint32_put(p_state_encode, record.time);
				}
				
				zcbor_float32_put(p_state_encode, record.batt);
				zcbor_int32_put(p_state_encode, record.sig);
				zcbor_tstr_put_term(p_state_encode, record.fw_version);
				zcbor_int32_put(p_state_encode, record.packet);

				for (int sensor_id = 0; sensor_id < SENSOR_RECORD_SENSOR_ELEMENT_LEN; sensor_id++) {
					if (data_codec_compare_temperature_is_valid(record.value[sensor_id])) {
						zcbor_float32_put(p_state_encode, record.value[sensor_id]);
					} else {
						zcbor_tstr_put_term(p_state_encode, "*");
					}
				}
			zcbor_list_end_encode(p_state_encode, 1);
		}
		zcbor_list_end_encode(p_state_encode, 0);

	zcbor_map_end_encode(p_state_encode, 0);
	LOG_HEXDUMP_INF(zcbor_buffer, p_state_encode->payload_mut - zcbor_buffer, "Payload");
}