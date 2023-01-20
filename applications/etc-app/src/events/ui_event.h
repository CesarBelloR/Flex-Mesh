#ifndef _UI_EVENT_H_
#define _UI_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UI event types submitted by the UI module. */
enum ui_event_type {
	/** Button/Hall Sensor has been pressed.
	 *  Payload is of type @ref ui_module_data (ui).
	 */
	UI_EVT_INPUT_DATA_READY,

	/** The UI module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	UI_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the cloud module. Error details are
	 *  attached in the event structure.
	 */
	UI_EVT_ERROR
};

struct ui_event {
	struct app_event_header header;

	/** UI module event type. */
	enum ui_event_type type;

	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
	} data;
};

APP_EVENT_TYPE_DECLARE(ui_event);

#ifdef __cplusplus
}

#endif 

#endif /* _UI_EVENT_H_ */