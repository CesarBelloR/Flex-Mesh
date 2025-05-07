#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <math.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "etc_device_helper.h"

LOG_MODULE_REGISTER(etc_device_helper, CONFIG_ETC_APP_LOG_LEVEL);

#define CBOR_KEY_TIMESTAMP	1 // 32-bit integer (Timestamp)
#define CBOR_KEY_SENSOR_SAMPLES 2 // Map (Array of sensor samples)
#define CBOR_KEY_SENSOR_PORT	3 // String/integer (Sensor port)
#define CBOR_KEY_SENSOR_TYPE	4 // String/integer (Sensor type)
#define CBOR_KEY_SENSOR_VALUE	5 // Half-precision float (Sensor value)
#define CBOR_KEY_BATTERY_MV	6 // 16-bit integer (Battery in mV)

#define CBOR_MAX_BACKUPS      5 // Number of backup slots to keep in the state
#define CBOR_DECODE_INNER_MAP 6 // Inner map length for decode

int etc_common_encode_sensor_data(struct sensor_data *sensor, uint8_t *buf, size_t *buf_len)
{
	ZCBOR_STATE_E(encoding_state, CBOR_MAX_BACKUPS, buf, *buf_len, 0);
	bool success = true;

	// Start CBOR map with 3 key-value pairs (timestamp, sensor array, battery)
	success &= zcbor_map_start_encode(encoding_state, 3);

	// Timestamp
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_TIMESTAMP);
	success &= zcbor_uint32_put(encoding_state, sensor->timestamp);

	// Array of sensor samples
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_SENSOR_SAMPLES);
	success &= zcbor_list_start_encode(encoding_state, SENSOR_EVENT_NUM_DEV_MAX);
	for (int i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		success &= zcbor_map_start_encode(encoding_state, 3);
		// Sensor index (0-SENSOR_EVENT_NUM_DEV_MAX)
		success &= zcbor_uint32_put(encoding_state, CBOR_KEY_SENSOR_PORT);
		success &= zcbor_uint32_put(encoding_state, i);
		// Sensor type/status (hardcoded as 1)
		success &= zcbor_uint32_put(encoding_state, CBOR_KEY_SENSOR_TYPE);
		success &= zcbor_uint32_put(encoding_state, 1);
		// Sensor value
		success &= zcbor_uint32_put(encoding_state, CBOR_KEY_SENSOR_VALUE);
		success &= zcbor_float16_put(encoding_state, sensor->sensor[i]);
		success &= zcbor_map_end_encode(encoding_state, 0);
	}
	success &= zcbor_list_end_encode(encoding_state, 0);

	// Battery level
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_BATTERY_MV);
	success &= zcbor_uint32_put(encoding_state, sensor->battery_mV);

	// End CBOR map
	success &= zcbor_map_end_encode(encoding_state, 0);

	if (!success) {
		return -EINVAL;
	}

	size_t encoded_len = encoding_state->payload - buf;
	if (encoded_len > *buf_len) {
		return -ENOSPC;
	}

	*buf_len = encoded_len;
	return 0;
}

static inline bool zcbor_list_or_map_end(zcbor_state_t *state)
{
	if (state->decode_state.indefinite_length_array) {
		return *state->payload == 0xff;
	}
	return state->elem_count == 0;
}

int etc_common_decode_sensor_data(struct sensor_data *sensor, uint8_t *buf, size_t buf_len)
{
	ZCBOR_STATE_D(decoding_state, CBOR_MAX_BACKUPS, buf, buf_len, 1, 0);
	bool success = true;

	// Initialize the decoder
	success &= zcbor_map_start_decode(decoding_state);
	if (!success) {
		LOG_DBG("Failed to start map decode: %d", zcbor_peek_error(decoding_state));
		return -EINVAL;
	}

	bool timestamp_found = false;
	bool sensors_found = false;
	bool battery_found = false;

	// Decode each key-value pair in the map
	while (success && !zcbor_list_or_map_end(decoding_state)) {
		// Get the key
		uint32_t key;
		success &= zcbor_uint32_decode(decoding_state, &key);
		if (!success) {
			LOG_DBG("Failed to decode key: %d", zcbor_peek_error(decoding_state));
			return -EINVAL;
		}

		switch (key) {
		case CBOR_KEY_TIMESTAMP:
			success &=
				zcbor_uint32_decode(decoding_state, (uint32_t *)&sensor->timestamp);
			if (!success) {
				LOG_DBG("Failed to decode timestamp: %d",
					zcbor_peek_error(decoding_state));
				return -EINVAL;
			}
			timestamp_found = true;
			break;

		case CBOR_KEY_SENSOR_SAMPLES: {
			size_t array_len;
			success &= zcbor_list_start_decode(decoding_state);
			array_len = decoding_state->elem_count;
			if (!success) {
				LOG_DBG("Failed to start sensor samples list: %d",
					zcbor_peek_error(decoding_state));
				return -EINVAL;
			}

			if (array_len != SENSOR_EVENT_NUM_DEV_MAX) {
				LOG_DBG("Sensor array length mismatch: got %zu, expected %d",
					array_len, SENSOR_EVENT_NUM_DEV_MAX);
				success = false;
				break;
			}

			for (size_t j = 0; success && j < array_len; j++) {
				size_t inner_map_len;
				success &= zcbor_map_start_decode(decoding_state);
				inner_map_len = decoding_state->elem_count;
				if (!success) {
					LOG_DBG("Failed to start sensor map %zu: %d", j,
						zcbor_peek_error(decoding_state));
					return -EINVAL;
				}

				if (inner_map_len != CBOR_DECODE_INNER_MAP) {
					LOG_DBG("Inner map length mismatch: got %zu",
						inner_map_len);
					success = false;
					break;
				}

				uint32_t port = UINT32_MAX;
				uint32_t type = UINT32_MAX;
				float value = NAN;

				while (success && !zcbor_list_or_map_end(decoding_state)) {
					uint32_t inner_key;
					success &= zcbor_uint32_decode(decoding_state, &inner_key);
					if (!success) {
						LOG_DBG("Failed to decode inner key: %d",
							zcbor_peek_error(decoding_state));
						return -EINVAL;
					}

					switch (inner_key) {
					case CBOR_KEY_SENSOR_PORT:
						success &=
							zcbor_uint32_decode(decoding_state, &port);
						if (!success) {
							LOG_DBG("Failed to decode port: %d",
								zcbor_peek_error(decoding_state));
							return -EINVAL;
						}
						break;
					case CBOR_KEY_SENSOR_TYPE:
						success &=
							zcbor_uint32_decode(decoding_state, &type);
						if (!success) {
							LOG_DBG("Failed to decode type: %d",
								zcbor_peek_error(decoding_state));
							return -EINVAL;
						}
						break;
					case CBOR_KEY_SENSOR_VALUE:
						success &= zcbor_float16_decode(decoding_state,
										&value);
						if (!success) {
							LOG_DBG("Failed to decode value: %d",
								zcbor_peek_error(decoding_state));
							return -EINVAL;
						}
						break;
					default:
						LOG_DBG("Invalid inner key: %u", inner_key);
						success = false;
						break;
					}
				}

				// Close the inner map
				success &= zcbor_map_end_decode(decoding_state);
				if (!success) {
					LOG_DBG("Failed to end sensor map %zu: %d", j,
						zcbor_peek_error(decoding_state));
					return -EINVAL;
				}

				if (!success || port != j || type != 1 || isnan(value)) {
					LOG_DBG("Invalid sensor data: port=%u, type=%u, value=%f",
						port, type, (double)value);
					success = false;
					break;
				}
				sensor->sensor[j] = value;
			}

			// Close the sensor samples list
			success &= zcbor_list_end_decode(decoding_state);
			if (!success) {
				LOG_DBG("Failed to end sensor samples list: %d",
					zcbor_peek_error(decoding_state));
				return -EINVAL;
			}
			sensors_found = success;
			break;
		}

		case CBOR_KEY_BATTERY_MV:
			success &= zcbor_uint32_decode(decoding_state,
						       (uint32_t *)&sensor->battery_mV);
			if (!success) {
				LOG_DBG("Failed to decode battery: %d",
					zcbor_peek_error(decoding_state));
				return -EINVAL;
			}
			sensor->battery_status = 0;
			battery_found = true;
			break;

		default:
			LOG_DBG("Invalid key: %u", key);
			success = false;
			break;
		}
	}

	// Final checks
	if (!success) {
		return -EINVAL;
	}
	if (!timestamp_found || !sensors_found || !battery_found) {
		LOG_DBG("Missing required keys: timestamp=%d, sensors=%d, battery=%d",
			timestamp_found, sensors_found, battery_found);
		return -EINVAL;
	}
	return 0;
}