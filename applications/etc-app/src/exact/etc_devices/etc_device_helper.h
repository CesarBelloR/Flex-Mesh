#ifndef ETC_DEVICE_HELPER_H
#define ETC_DEVICE_HELPER_H

#include <stdint.h>
#include "events/sensor_event.h"

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
 *         - -ENOSPC if the buffer is too small to hold the encoded data.
 */
int etc_common_encode_sensor_data(struct sensor_data *sensor, uint8_t *buf, size_t *buf_len);

/**
 * @brief Decodes a CBOR buffer into sensor data.
 *
 * This function decodes a CBOR buffer into a `struct sensor_data` using the zcbor library.
 * The CBOR structure must include a timestamp, an array of sensor samples, and a battery level,
 * matching the format produced by `etc_common_encode_sensor_data`.
 *
 * @param sensor Pointer to the sensor data structure where the decoded data will be stored.
 * @param buf Pointer to the input buffer containing the CBOR data.
 * @param buf_len Size of the input buffer in bytes.
 *
 * @return 0 on success, or a negative error code on failure:
 *         - -EINVAL if decoding fails due to invalid CBOR structure, type mismatches, or missing
 * keys.
 */
int etc_common_decode_sensor_data(struct sensor_data *sensor, uint8_t *buf, size_t buf_len);

#endif /* ETC_DEVICE_HELPER_H */