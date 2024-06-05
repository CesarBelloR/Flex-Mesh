#ifndef _CLOUD_EVENT_H_
#define _CLOUD_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"
#include "cloud_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event types submitted by the cloud module. */
enum cloud_event_type {
	/** Cloud service is connected. */
	CLOUD_EVT_CONNECTED,

	/** Cloud service is disconnected. */
	CLOUD_EVT_DISCONNECTED,

	/** Connecting to a cloud service. */
	CLOUD_EVT_CONNECTING,

	/** Cloud is paused (a form of disconnected) */
	CLOUD_EVT_PAUSED,

	CLOUD_EVT_RX_OFF,

	/** Connection has timed out. */
	CLOUD_EVT_CONNECTION_TIMEOUT,

	/** Connect to LTE.
	 *  This event is sent out when the modem should connect to LTE (put into normal mode) post
	 *  provisioning of server credentials.
	 *
	 *  Only used when building for LwM2M.
	 */
	CLOUD_EVT_LTE_CONNECT,

	/** Disconnect from LTE.
	 *  This event is sent out when the modem should be put into offline mode prior to
	 *  provisioning of server credentials.
	 *
	 *  Only used when building for LwM2M.
	 */
	CLOUD_EVT_LTE_DISCONNECT,

	/** User association request received from cloud. */
	CLOUD_EVT_USER_ASSOCIATION_REQUEST,

	/** Acknowledgement for last send was received. */
	CLOUD_EVT_DATA_SEND_ACK,

	/** Last data send failed. */
	CLOUD_EVT_DATA_SEND_FAIL,

	/** Reboot requested from cloud. */
	CLOUD_EVT_REBOOT_REQUEST,

	/** Reclaim request received from cloud */
	CLOUD_EVT_RECLAIM_REQUEST,

	/** Location request received from cloud */
	CLOUD_EVT_LOCATION_REQUEST,

	/** A new device configuration has been received from cloud.
	 *  The payload associated with this event is of type @ref cloud_data_cfg (config).
	 */
	CLOUD_EVT_CONFIG_RECEIVED,

	/** An empty device configuration has been received from cloud. */
	CLOUD_EVT_CONFIG_EMPTY,

	/** A FOTA update has started. */
	CLOUD_EVT_FOTA_START,

	/** FOTA has been performed, a reboot of the application is needed. */
	CLOUD_EVT_FOTA_DOWNLOADED,

	/** FOTA has been performed, a reboot of the application is needed. */
	CLOUD_EVT_FOTA_DONE,

	/** An error occurred during a FOTA update. */
	CLOUD_EVT_FOTA_ERROR,

	/** The cloud module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	CLOUD_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the cloud module. Error details are
	 *  attached in the event structure.
	 */
	CLOUD_EVT_ERROR
};

struct cloud_event {
	struct app_event_header header;
	enum cloud_event_type type;
	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		struct reclaim_data reclaim;
	} data;
};

APP_EVENT_TYPE_DECLARE(cloud_event);

#ifdef __cplusplus
}
#endif

#endif /* _CLOUD_EVENT_H_ */