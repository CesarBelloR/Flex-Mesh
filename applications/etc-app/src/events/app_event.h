#ifndef _APP_EVENT_H_
#define _APP_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event types submitted by Application module. */
enum app_event_type {
	/** Signal that the application has done necessary setup, and
	 *  now started.
	 */
	APP_EVT_START,
	
	/** Connect to LTE network. */
	APP_EVT_LTE_CONNECT,

	/** Disconnect from LTE network. */
	APP_EVT_LTE_DISCONNECT,

	/** Signal other modules to start sampling and report the data when
	 *  it's ready.
	 *  The event must also contain a list with requested data types,
	 *  @ref app_module_data_type.
	 */
	APP_EVT_DATA_GET,
	/* Same as @ref APP_EVT_DATA_GET but signals that the request
	 * was triggered by a user.
	 */
	APP_EVT_DATA_GET_USER_TRIGGERED,

	/** Request transmit the log to lora/cloud */
	APP_EVT_DATA_TRANSMIT,

	/** Request to sync with cloud in desired event (Lora Sync, Magnet, T.B.D in future) */
	APP_EVT_DATA_SYNC_CLOUD,

	/** Request a GNSS location update based on time and configured GNSS interval */
	APP_EVT_REQUEST_LOCATION,

	/** Request receive from logger to relay */
	APP_EVT_DATA_RECEIVE,
	
	/** Create a list with all available sensor types in the system and
	 *  distribute it as an APP_EVT_DATA_GET event.
	 */
	APP_EVT_DATA_GET_ALL,

	/** Request latest configuration from the cloud. */
	APP_EVT_CONFIG_GET,

	/** The force request shutdown when LTE units miss check-in over cellular */
	APP_EVT_REQUEST_SHUTDOWN,
	
	/** The application module has performed all procedures to prepare for
	 *  a shutdown of the system.
	 */
	APP_EVT_SHUTDOWN_READY,

	/** The application module receives a request from user to start calibration
	 */
	APP_EVT_REQUEST_CALIBRATION,
	/* Calibration timeout */
	APP_EVT_TIMEOUT_CALIBRATION,
	/* Calibration failed during the "calibration check" stage */
	APP_EVT_CALIBRATION_ERROR,
	/** An irrecoverable error has occurred in the application module. Error details are
	 *  attached in the event structure.
	 */
	APP_EVT_ERROR
};

enum app_data_type {
	APP_DATA_ENVIRONMENTAL,
	APP_DATA_MODEM_STATIC,
	APP_DATA_BATTERY,
	APP_DATA_COUNT,
};

struct app_event {
	struct app_event_header header;
	enum app_event_type type;
	enum app_data_type data_list[APP_DATA_COUNT];

	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/* Calibration result */
		int calibration_result;
	} data;

	size_t count;

	/** The time each module has to fetch data before what is available
	 *  is transmitted.
	 */
	int timeout;
};

APP_EVENT_TYPE_DECLARE(app_event);

#ifdef __cplusplus
}
#endif

#endif /* _APP_EVENT_H_ */