#include <math.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "etc_device_helper.h"
#include "etc_sensor.h"

LOG_MODULE_REGISTER(etc_device_helper, CONFIG_ETC_APP_LOG_LEVEL);

#if defined(CONFIG_ETC_RECORD_CBOR)
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#define CBOR_KEY_TIMESTAMP	1 // 32-bit integer (Timestamp)
#define CBOR_KEY_SENSOR_SAMPLES 2 // Map (Array of sensor samples)
#define CBOR_KEY_SENSOR_PORT	3 // String/integer (Sensor port)
#define CBOR_KEY_SENSOR_TYPE	4 // String/integer (Sensor type)
#define CBOR_KEY_SENSOR_VALUE	5 // Half-precision float (Sensor value)
#define CBOR_KEY_BATTERY_MV	6 // 16-bit integer (Battery in mV)

#define CBOR_VALUE_SENSOR_TYPE_TEMPERATURE 1 // Sensor type (Temperature)
#define CBOR_VALUE_SENSOR_TYPE_HUMIDITY	   2 // Sensor type (Humidity)

#define CBOR_MAX_BACKUPS      5 // Number of backup slots to keep in the state
#define CBOR_DECODE_INNER_MAP 6 // Inner map length for decode

// Sensor metadata structure
struct sensor_metadata {
	uint32_t port;
	uint32_t type;
	bool (*is_valid)(float value);
	float default_value;
};

// sensor metadata lookup table
static const struct sensor_metadata sensor_meta[] = {
	{SENSOR_INPUT_IN1, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN2, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN3, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN4, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN5, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN6, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN7, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_IN8, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_AMBIENT, CBOR_VALUE_SENSOR_TYPE_TEMPERATURE, sensor_temperature_is_valid,
	 SENSOR_TEMP_NO_CONNECTED},
	{SENSOR_INPUT_HUMID, CBOR_VALUE_SENSOR_TYPE_HUMIDITY, sensor_humidity_is_valid,
	 SENSOR_HUMID_NO_CONNECTED}};
#define SENSOR_META_COUNT (sizeof(sensor_meta) / sizeof(sensor_meta[0]))

// Encode a single sensor sample (unchanged from previous)
static int encode_sensor_sample(zcbor_state_t *state, const struct sensor_metadata *meta,
				float value)
{
	bool success = true;
	success &= zcbor_map_start_encode(state, CBOR_DECODE_INNER_MAP);
	success &= zcbor_uint32_put(state, CBOR_KEY_SENSOR_PORT);
	success &= zcbor_uint32_put(state, meta->port);
	success &= zcbor_uint32_put(state, CBOR_KEY_SENSOR_TYPE);
	success &= zcbor_uint32_put(state, meta->type);
	success &= zcbor_uint32_put(state, CBOR_KEY_SENSOR_VALUE);
	success &= zcbor_float16_put(state, value);
	success &= zcbor_map_end_encode(state, 0);
	return success ? 0 : -EINVAL;
}

// CBOR key handler function type
typedef int (*cbor_key_handler_t)(zcbor_state_t *state, struct sensor_data *sensor);

// CBOR key metadata structure
struct cbor_key_metadata {
	uint32_t key;
	cbor_key_handler_t handler;
};

// Forward declarations of handler functions
static int decode_timestamp(zcbor_state_t *state, struct sensor_data *sensor);
static int decode_sensor_samples(zcbor_state_t *state, struct sensor_data *sensor);
static int decode_battery(zcbor_state_t *state, struct sensor_data *sensor);

// Static CBOR key metadata table
static const struct cbor_key_metadata cbor_keys[] = {
	{CBOR_KEY_TIMESTAMP, decode_timestamp},
	{CBOR_KEY_SENSOR_SAMPLES, decode_sensor_samples},
	{CBOR_KEY_BATTERY_MV, decode_battery}};
#define CBOR_KEYS_COUNT (sizeof(cbor_keys) / sizeof(cbor_keys[0]))

// Helper to check if a list or map has ended
static inline bool zcbor_list_or_map_end(zcbor_state_t *state)
{
	if (state->decode_state.indefinite_length_array) {
		return *state->payload == 0xff;
	}
	return state->elem_count == 0;
}

// Decode timestamp
static int decode_timestamp(zcbor_state_t *state, struct sensor_data *sensor)
{
	if (!zcbor_uint32_decode(state, (uint32_t *)&sensor->timestamp)) {
		LOG_ERR("Failed to decode timestamp: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	return 0;
}

// Decode battery level
static int decode_battery(zcbor_state_t *state, struct sensor_data *sensor)
{
	if (!zcbor_uint32_decode(state, (uint32_t *)&sensor->battery_mV)) {
		LOG_ERR("Failed to decode battery: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	return 0;
}

// Decode a single sensor sample
static int decode_sensor_sample(zcbor_state_t *state, struct sensor_data *sensor)
{
	if (!zcbor_map_start_decode(state)) {
		LOG_ERR("Failed to start sensor map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	uint32_t port = UINT32_MAX;
	uint32_t type = UINT32_MAX;
	float value = NAN;

	while (!zcbor_list_or_map_end(state)) {
		uint32_t inner_key;
		if (!zcbor_uint32_decode(state, &inner_key)) {
			LOG_ERR("Failed to decode inner key: %d", zcbor_peek_error(state));
			return -EINVAL;
		}

		switch (inner_key) {
		case CBOR_KEY_SENSOR_PORT:
			if (!zcbor_uint32_decode(state, &port)) {
				LOG_ERR("Failed to decode port: %d", zcbor_peek_error(state));
				return -EINVAL;
			}
			break;
		case CBOR_KEY_SENSOR_TYPE:
			if (!zcbor_uint32_decode(state, &type)) {
				LOG_ERR("Failed to decode type: %d", zcbor_peek_error(state));
				return -EINVAL;
			}
			break;
		case CBOR_KEY_SENSOR_VALUE:
			if (!zcbor_float16_decode(state, &value)) {
				LOG_ERR("Failed to decode value: %d", zcbor_peek_error(state));
				return -EINVAL;
			}
			break;
		default:
			LOG_ERR("Invalid inner key: %u", inner_key);
			return -EINVAL;
		}
	}

	if (!zcbor_map_end_decode(state)) {
		LOG_ERR("Failed to end sensor map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	if (isnan(value) || port >= SENSOR_EVENT_NUM_DEV_MAX) {
		LOG_ERR("Invalid sensor data: port=%u, type=%u, value=%f", port, type,
			(double)value);
		return -EINVAL;
	}

	bool valid_type = false;
	for (size_t i = 0; i < SENSOR_META_COUNT; i++) {
		if (sensor_meta[i].port == port && sensor_meta[i].type == type) {
			valid_type = true;
			break;
		}
	}
	if (!valid_type) {
		LOG_ERR("Invalid sensor type for port %u: %u", port, type);
		return -EINVAL;
	}

	sensor->sensor[port] = value;
	return 0;
}

// Decode sensor samples array
static int decode_sensor_samples(zcbor_state_t *state, struct sensor_data *sensor)
{
	size_t array_len;
	if (!zcbor_list_start_decode(state)) {
		LOG_ERR("Failed to start sensor samples list: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	array_len = state->elem_count;

	if (array_len > SENSOR_EVENT_NUM_DEV_MAX) {
		LOG_ERR("Sensor array too large: %zu > %d", array_len, SENSOR_EVENT_NUM_DEV_MAX);
		return -EINVAL;
	}

	for (size_t i = 0; i < array_len; i++) {
		int ret = decode_sensor_sample(state, sensor);
		if (ret != 0) {
			LOG_ERR("Failed to decode sensor sample %zu: %d", i, ret);
			return ret;
		}
	}

	if (!zcbor_list_end_decode(state)) {
		LOG_ERR("Failed to end sensor samples list: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	return 0;
}

int etc_device_encode_cbor_data(struct sensor_data *sensor, uint8_t *buf, size_t *buf_len)
{
	ZCBOR_STATE_E(encoding_state, CBOR_MAX_BACKUPS, buf, *buf_len, 0);
	bool success = true;

	// Start CBOR map with 3 key-value pairs
	success &= zcbor_map_start_encode(encoding_state, 3);

	// Encode timestamp
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_TIMESTAMP);
	success &= zcbor_uint32_put(encoding_state, sensor->timestamp);

	// Count valid sensors to set array length
	uint8_t num_sensors_valid = 0;
	for (size_t i = 0; i < SENSOR_META_COUNT; i++) {
		if (sensor_meta[i].is_valid(sensor->sensor[sensor_meta[i].port])) {
			num_sensors_valid++;
		}
	}

	// Encode sensor samples array
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_SENSOR_SAMPLES);
	success &= zcbor_list_start_encode(encoding_state, num_sensors_valid);

	// Encode valid sensor samples
	for (size_t i = 0; i < SENSOR_META_COUNT; i++) {
		if (sensor_meta[i].is_valid(sensor->sensor[sensor_meta[i].port])) {
			int ret = encode_sensor_sample(encoding_state, &sensor_meta[i],
						       sensor->sensor[sensor_meta[i].port]);
			if (ret != 0) {
				LOG_ERR("Failed to encode sensor %zu: %d", i, ret);
				return ret;
			}
		}
	}
	success &= zcbor_list_end_encode(encoding_state, 0);

	// Encode battery level
	success &= zcbor_uint32_put(encoding_state, CBOR_KEY_BATTERY_MV);
	success &= zcbor_uint32_put(encoding_state, sensor->battery_mV);

	// End CBOR map
	success &= zcbor_map_end_encode(encoding_state, 0);

	if (!success) {
		LOG_ERR("Encoding failed: %d", zcbor_peek_error(encoding_state));
		return -EINVAL;
	}

	size_t encoded_len = encoding_state->payload - buf;
	if (encoded_len > *buf_len) {
		LOG_ERR("Encoded data exceeds buffer: %zu > %zu", encoded_len, *buf_len);
		return -ENOMEM;
	}
	*buf_len = encoded_len;
	return 0;
}

// Helper function to calculate CBOR data length
// Extra zero padding is added when decoding the read data from memory flash!
// CBOR doens't support zero padding, so we need to calculate the data length!.
static bool get_cbor_data_length(const uint8_t *buf, size_t buf_len, size_t *data_len)
{
	ZCBOR_STATE_D(state, CBOR_MAX_BACKUPS, buf, buf_len, 1, 0);

	// Start decoding the main map
	if (!zcbor_map_start_decode(state)) {
		LOG_ERR("Failed to start map decode for length: %d", zcbor_peek_error(state));
		return false;
	}
	// Process key-value pairs
	while (!zcbor_list_or_map_end(state)) {
		uint32_t key;
		if (!zcbor_uint32_decode(state, &key)) {
			LOG_ERR("Failed to decode key for length: %d", zcbor_peek_error(state));
			return false;
		}

		switch (key) {
		case CBOR_KEY_TIMESTAMP:
			uint32_t timestamp;
			if (!zcbor_uint32_decode(state, &timestamp)) {
				LOG_ERR("Failed to decode timestamp for length: %d",
					zcbor_peek_error(state));
				return false;
			}
			break;
		case CBOR_KEY_BATTERY_MV:
			uint32_t battery;
			if (!zcbor_uint32_decode(state, &battery)) {
				LOG_ERR("Failed to decode battery for length: %d",
					zcbor_peek_error(state));
				return false;
			}
			break;
		case CBOR_KEY_SENSOR_SAMPLES:
			if (!zcbor_list_start_decode(state)) {
				LOG_ERR("Failed to start sensor samples list for length: %d",
					zcbor_peek_error(state));
				return false;
			}
			size_t array_len = state->elem_count;
			for (size_t i = 0; i < array_len; i++) {
				if (!zcbor_map_start_decode(state)) {
					LOG_ERR("Failed to start sensor map for length: %d",
						zcbor_peek_error(state));
					return false;
				}
				while (!zcbor_list_or_map_end(state)) {
					uint32_t inner_key;
					if (!zcbor_uint32_decode(state, &inner_key)) {
						LOG_ERR("Failed to decode inner key for length: %d",
							zcbor_peek_error(state));
						return false;
					}
					switch (inner_key) {
					case CBOR_KEY_SENSOR_PORT:
					case CBOR_KEY_SENSOR_TYPE:
						uint32_t val;
						if (!zcbor_uint32_decode(state, &val)) {
							LOG_ERR("Failed to decode port/type for "
								"length: %d",
								zcbor_peek_error(state));
							return false;
						}
						break;
					case CBOR_KEY_SENSOR_VALUE:
						float val_float;
						if (!zcbor_float16_decode(state, &val_float)) {
							LOG_ERR("Failed to decode value for "
								"length: %d",
								zcbor_peek_error(state));
							return false;
						}
						break;
					default:
						LOG_ERR("Invalid inner key for length: %u",
							inner_key);
						return false;
					}
				}
				if (!zcbor_map_end_decode(state)) {
					LOG_ERR("Failed to end sensor map for length: %d",
						zcbor_peek_error(state));
					return false;
				}
			}
			if (!zcbor_list_end_decode(state)) {
				LOG_ERR("Failed to end sensor samples list for length: %d",
					zcbor_peek_error(state));
				return false;
			}
			break;
		default:
			LOG_ERR("Invalid key for length: %u", key);
			return false;
		}
	}

	if (!zcbor_map_end_decode(state)) {
		LOG_ERR("Failed to end map decode for length: %d", zcbor_peek_error(state));
		return false;
	}

	*data_len = state->payload - buf;
	return true;
}

int etc_device_decode_cbor_data(struct sensor_data *sensor, uint8_t *buf, size_t buf_len)
{
	// Calculate actual CBOR data length
	size_t data_len;
	if (!get_cbor_data_length(buf, buf_len, &data_len)) {
		LOG_ERR("Failed to determine CBOR data length");
		return -EINVAL;
	}

	if (data_len > buf_len) {
		LOG_ERR("Calculated data length %zu exceeds buffer %zu", data_len, buf_len);
		return -EINVAL;
	}

	ZCBOR_STATE_D(decoding_state, CBOR_MAX_BACKUPS, buf, data_len, 1, 0);
	LOG_HEXDUMP_DBG(buf, data_len, "DECODE");
	// Start decoding the main map
	if (!zcbor_map_start_decode(decoding_state)) {
		LOG_ERR("Failed tostart map decode: %d", zcbor_peek_error(decoding_state));
		return -EINVAL;
	}

	bool timestamp_found = false;
	bool sensors_found = false;
	bool battery_found = false;

	/* Set all sensors as default value (no connected) */
	for (size_t i = 0; i < SENSOR_META_COUNT; i++) {
		sensor->sensor[i] = sensor_meta[i].default_value;
	}

	// Handle each key-value pair
	while (!zcbor_list_or_map_end(decoding_state)) {
		uint32_t key;
		if (!zcbor_uint32_decode(decoding_state, &key)) {
			LOG_ERR("Failed to decode key: %d", zcbor_peek_error(decoding_state));
			return -EINVAL;
		}

		// Find and invoke the key handler
		bool key_handled = false;
		for (size_t i = 0; i < CBOR_KEYS_COUNT; i++) {
			if (cbor_keys[i].key == key) {
				int ret = cbor_keys[i].handler(decoding_state, sensor);
				if (ret != 0) {
					return ret;
				}
				key_handled = true;
				if (key == CBOR_KEY_TIMESTAMP)
					timestamp_found = true;
				else if (key == CBOR_KEY_SENSOR_SAMPLES)
					sensors_found = true;
				else if (key == CBOR_KEY_BATTERY_MV)
					battery_found = true;
				break;
			}
		}

		if (!key_handled) {
			LOG_ERR("Invalid key: %u", key);
			return -EINVAL;
		}
	}

	if (!zcbor_map_end_decode(decoding_state)) {
		LOG_ERR("Failed to end map decode: %d", zcbor_peek_error(decoding_state));
		return -EINVAL;
	}

	if (!timestamp_found || !sensors_found || !battery_found) {
		LOG_ERR("Missing required keys: timestamp=%d, sensors=%d, battery=%d",
			timestamp_found, sensors_found, battery_found);
		return -EINVAL;
	}

	return 0;
}
#else
int etc_device_encoder_legacy_data(struct sensor_data *sensor, union etc_device_record *record)
{
	record->battery = (float)sensor->battery_mV / 1000.0;
	record->flag = (uint32_t)(sensor->battery_status);
	record->timestamp = (uint32_t)sensor->timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		record->sensor[i] = sensor->sensor[i];
	}
	return 0;
}
#endif

int etc_device_pack_sensor_data(struct sensor_data *sensor, union etc_device_record *record)
{
#if defined(CONFIG_ETC_RECORD_CBOR)
	size_t buf_len = sizeof(record->data);
	return etc_device_encode_cbor_data(sensor, record->data, &buf_len);
#else
	return etc_device_encoder_legacy_data(sensor, record);
#endif
	return -ENOTSUP;
}

int etc_device_unpack_sensor_data(union etc_device_record *record)
{
	int rc = -ENOTSUP;
#if defined(CONFIG_ETC_RECORD_CBOR)
	struct sensor_data sensor = {0};
	rc = etc_device_decode_cbor_data(&sensor, record->data, sizeof(record->data));
	if (rc) {
		LOG_ERR("Failed to decode sensor data %d", rc);
		return rc;
	}
	record->battery = (double)sensor.battery_mV / 1000.0;
	record->timestamp = (uint32_t)sensor.timestamp;
	for (uint8_t i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		record->sensor[i] = sensor.sensor[i];
	}
#else
	rc = 0;
#endif
	return rc;
}
