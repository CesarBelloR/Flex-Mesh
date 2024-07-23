#ifndef _BLE_EVENT_H_
#define _BLE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"
#include "cloud_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event types submitted by the ble module. */
enum ble_event_type {
	/** Bluetooth stack disconnected from the remote peer. */
	BLE_EVT_DISCONNECTED,

	/** Bluetooth stack is connecting to the remote peer. */
	BLE_EVT_CONNECTING,

	/** Bluetooth stack successfully connected to the remote peer. */
	BLE_EVT_CONNECTED,

	/** Bluetooth stack set the connection security to at least level 2 (that is, encryption and
	 *  no authentication).
	 */
	BLE_EVT_SECURED,

	/** Bluetooth stack failed to connect the remote peer. */
	BLE_EVT_CONN_FAILED,

	/** Connection is ready to exchange data */
	BLE_EVT_CONN_READY,

	/** Acknowledgement for last send was received. */
	BLE_EVT_DATA_SEND_ACK,

	/** Last data send failed. */
	BLE_EVT_DATA_SEND_FAIL,

	/** Last data can't export */
	BLE_EVT_DATA_EXPORT_FAIL, 

	/** Reclaim request received from BLE */
	BLE_EVT_RECLAIM_REQUEST,

	/** Query record for reclaiming data */
	BLE_EVT_QUERY_RECLAIM,
	
	/** A FOTA update has started. */
	BLE_EVT_FOTA_START,

	/** FOTA has been performed, a reboot of the application is needed. */
	BLE_EVT_FOTA_DOWNLOADED,

	/** FOTA has been performed, a reboot of the application is needed. */
	BLE_EVT_FOTA_DONE,

	/** An error occurred during a FOTA update. */
	BLE_EVT_FOTA_ERROR,
	/** The cloud module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	BLE_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the ble module. Error details are
	 *  attached in the event structure.
	 */
	BLE_EVT_ERROR
};

struct ble_event {
	struct app_event_header header;
	enum ble_event_type type;
	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		struct reclaim_data reclaim;
	} data;
};

APP_EVENT_TYPE_DECLARE(ble_event);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_EVENT_H_ */