#ifndef ETC_BLE_H_
#define ETC_BLE_H_

#include <stdint.h>
#include "events/sensor_event.h"
#include "cloud/cloud_wrapper.h"

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

/** @brief Bluetooth notification event types used to signal the application. */
enum etc_ble_evt_type {
	ETC_BLE_EVT_DISCONNECTED,
	ETC_BLE_EVT_CONNECTING,
	ETC_BLE_EVT_CONNECTED,
	ETC_BLE_EVT_CCC_MEASURE_READY,
	ETC_BLE_EVT_CCC_RECLAIM_READY,
	ETC_BLE_EVT_ERR
};

/** @brief Struct with data received from the bluetooth library. */
struct etc_ble_evt {
	/** Type of event. */
	enum etc_ble_evt_type type;
	/* Reclaim data if CCC_RECLAIM request */
	struct reclaim_data reclaim;
};

/** @brief The channel charactersitic */
enum {
	ETC_BLE_SENSOR_CHAR,
	ETC_BLE_RECLAIM_CHAR
};

/** @brief Bluetooth library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*etc_ble_evt_handler_t)(const struct etc_ble_evt *evt);


/**
 * @brief Initialize the BLE peripheral for ETC
 * 
 * @return	0 on success, an error code otherwise. 
 */
int etc_ble_init(etc_ble_evt_handler_t evt_handler);

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
 * This function is used to notify the app with data.
 * 
 * @param channel The characteristic channel will send data out
 * @param data Pointer to the buffer containing data.
 * @param len  Length of the data buffer.
 * @return     Returns 0 on success, non-zero on failure.
 */
int etc_ble_notify(int channel, const uint8_t *data, uint16_t len);
#endif /* ETC_BLE_H_ */