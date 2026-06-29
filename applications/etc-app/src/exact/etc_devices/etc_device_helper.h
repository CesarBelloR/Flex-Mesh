#ifndef ETC_DEVICE_HELPER_H
#define ETC_DEVICE_HELPER_H

#include <stdint.h>
#include "events/sensor_event.h"

#if defined(CONFIG_ETC_RECORD_CBOR)
#define ETC_DEVICE_RECORD_SIZE (120)
#else
#define ETC_DEVICE_RECORD_SIZE (12 + SENSOR_EVENT_NUM_DEV_MAX * sizeof(float))
#endif

/* Number of sensor */
#define ETC_DEVICE_NUM_SENSOR  (SENSOR_EVENT_NUM_DEV_MAX)

union etc_device_record {
	uint8_t data[ETC_DEVICE_RECORD_SIZE];
	struct {
		float battery;
		float sensor[ETC_DEVICE_NUM_SENSOR];
		uint32_t timestamp;
		uint32_t flag; /* Use 8 bytes to save battery status */
	};
};

/* Assert to verify the record size must fit the macro ETC_DEVICE_RECORD_SIZE */
BUILD_ASSERT(ETC_DEVICE_RECORD_SIZE >= sizeof(union etc_device_record));
#if defined(CONFIG_ETC_RECORD_CBOR)
/**
 * @brief Encodes sensor data into a CBOR buffer.
 *
 * This function encodes a `struct sensor_data` into a CBOR buffer using the zcbor library.
 * The CBOR structure includes a timestamp, an array of sensor samples, and a battery level.
 * The encoded buffer length is returned via the `buf_len` pointer.
 *
 * @param sensor Pointer to the sensor data structure to encode.
 * @param buf Pointer to the output buffer where the CBOR data will be written.
 * @param buf_len Pointer to the size of the buffer. On success, this is updated to the actual
 * encoded length.
 *
 * @return 0 on success, or a negative error code on failure:
 *         - -EINVAL if encoding fails due to invalid data or CBOR structure.
 *         - -ENOMEM if the buffer is too small to hold the encoded data.
 */
int etc_device_encode_cbor_data(struct sensor_data *sensor, uint8_t *buf, size_t *buf_len);

/**
 * @brief Decodes a CBOR buffer into sensor data.
 *
 * This function decodes a CBOR buffer into a `struct sensor_data` using the zcbor library.
 * The CBOR structure must include a timestamp, an array of sensor samples, and a battery level,
 * matching the format produced by `etc_device_encode_cbor_data`.
 *
 * @param sensor Pointer to the sensor data structure where the decoded data will be stored.
 * @param buf Pointer to the input buffer containing the CBOR data.
 * @param buf_len Size of the input buffer in bytes.
 *
 * @return 0 on success, or a negative error code on failure:
 *         - -EINVAL if decoding fails due to invalid CBOR structure, type mismatches, or missing
 * keys.
 */
int etc_device_decode_cbor_data(struct sensor_data *sensor, uint8_t *buf, size_t buf_len);
#endif
/**
 * @file sensor_data.h
 * @brief Functions for packing and unpacking sensor data into/from device records.
 */

/**
 * @brief Packs sensor data into a device record.
 * 
 * This function takes sensor data and packs it into the provided device record structure.
 * 
 * @param sensor Pointer to the sensor data structure containing the data to pack.
 * @param record Pointer to the device record union where the data will be packed.
 * @return int Returns 0 on success, or a negative error code on failure.
 */
int etc_device_pack_sensor_data(struct sensor_data *sensor, union etc_device_record* record);

/**
 * @brief Unpacks sensor data from a device record.
 * 
 * This function extracts sensor data from the provided device record union.
 * 
 * @param record Pointer to the device record union containing the packed data.
 * @return int Returns 0 on success, or a negative error code on failure.
 */
int etc_device_unpack_sensor_data(union etc_device_record* record);

/**
 * For Lite and Embeddable devices, remap the 2-way splitter sub-ports into the
 * unused primary ports so every channel emits the same layout
 * (port1, port2, port1.B, port2.B, ...) without a dedicated splitter block.
 * Swap is performed as follows:
 *   Port 1.B (IN5) -> Port 3 (IN3)
 *   Port 2.B (IN6) -> Port 4 (IN4)
 * IN5-IN8 are then cleared: a 2-port device has no 3.B/4.B sub-ports and must
 * never emit a splitter block. No-op for all other device types.
 * @param sensor The sensor value array to modify (indexed by SENSOR_INPUT_*).
 */
void etc_device_map_two_port_sensor_data(float *sensor);

#endif /* ETC_DEVICE_HELPER_H */