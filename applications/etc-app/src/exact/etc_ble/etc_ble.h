#ifndef ETC_BLE_H_
#define ETC_BLE_H_

#include <stdint.h>
#include "events/sensor_event.h"

/* MTU is 256 bytes */
#define ETC_BLE_FRAME_PAYLOAD_MAX_LEN (240) 

#pragma pack(push, 1)

struct flex_ble_frame {
	uint8_t msg_id;
	uint8_t frame_id;
	uint16_t frame_len;
	uint8_t frame_payload[ETC_BLE_FRAME_PAYLOAD_MAX_LEN];
};

#pragma pack(pop)

/**
 * @brief Initialize the BLE peripheral for ETC
 * 
 * @return	0 on success, an error code otherwise. 
 */
int etc_ble_init(void);

/**
 * @brief Sets the current sensor data for the ETC BLE module.
 *
 * This function allows you to set the current sensor data.
 *
 * @param data Pointer to the structure containing sensor data.
 */
void etc_ble_set_current_sensor(struct sensor_data* data);

/**
 * @brief Notifies the app with sensor data.
 *
 * This function is used to notify the app with sensor data.
 *
 * @param data Pointer to the buffer containing sensor data.
 * @param len  Length of the sensor data buffer.
 * @return     Returns 0 on success, non-zero on failure.
 */
int etc_ble_sensor_notify(const uint8_t *data, uint16_t len);
#endif /* ETC_BLE_H_ */