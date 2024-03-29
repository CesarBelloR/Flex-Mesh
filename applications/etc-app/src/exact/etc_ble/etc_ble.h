#ifndef ETC_BLE_H_
#define ETC_BLE_H_

#include <stdint.h>
#include "events/sensor_event.h"
#include "cloud/cloud_wrapper.h"

/* MTU is 256 bytes */
#define ETC_BLE_FRAME_PAYLOAD_MAX_LEN (256) 

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
	ETC_BLE_EVT_CCC_QUERY_RECLAIM,
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
	ETC_BLE_RECLAIM_CHAR,
	ETC_BLE_CONFIG_CHAR
};

/** @brief The error type for BLE */
enum {
	ETC_BLE_ERR_RECLAIM_TYPE = 0x01,
	ETC_BLE_ERR_QUERY_TYPE,
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
 * @brief Start the adv for BLE
 */
void etc_ble_start_adv(void);

/**
 * @brief Stop the adv for BLE
 */
void etc_ble_stop_adv(void);

/**
 * @brief Start the adv for BLE with timeout for magnet event
 */
void etc_ble_start_adv_with_timeout(void);

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
 * @param need_encrypt Need to apply the encrypt or not (Only use encrypt for reclaim or measure data)
 * @return     Returns 0 on success, non-zero on failure.
 */
int etc_ble_notify(int channel, const uint8_t *data, uint16_t len, bool need_encrypt);

/**
 * @brief Check if the BLE is connected to central phone app
 * 
 * @return Return true if connection is ready.
 */
bool etc_ble_get_is_connected(void);

/**
 * @brief Notify the reclaim status to central app
 * 
 * @param reclaim_status Current reclaim status (number of records)
 * @return Return 0 on success 
 */
int etc_ble_notify_reclaim_status(int reclaim_status);

/**
 * @brief Notify the query reclaim to central app
 * 
 * @param reclaim_status Current number of record based on query reclaim
 * @return Return 0 on success 
 */
int etc_ble_notify_query_reclaim(int reclaim_status);

/** @brief Update battery level value.
 *
 * Update the characteristic value of the battery level
 *
 *  @param level The battery level in percent.
 *
 *  @return Zero in case of success and error code in case of error.
 */
int etc_ble_notify_battery(uint8_t level);

/**
 * @brief Notify the error response to central app
 * 
 * @param type Type of current error (reclaim, query etc...)
 * @param error The error code based on current status
 * @return Return 0 on success 
 */
int etc_ble_notify_error(int type, int error);
#endif /* ETC_BLE_H_ */