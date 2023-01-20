#ifndef _UTIL_MODULE_EVENT_H_
#define _UTIL_MODULE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Event types submitted by the utility module. */
enum util_module_event_type {
	UTIL_EVT_SHUTDOWN_REQUEST,
	UTIL_EVT_WAKEUP_REQUEST,
};

/** @brief Shutdown reason included in shutdown requests from the utility module. */
enum shutdown_reason {
	/** Generic reason, typically an irrecoverable error. */
	REASON_GENERIC,
	/** The application shuts down because a FOTA update finished. */
	REASON_FOTA_UPDATE,
	/** Sleep mode */
	REASON_SLEEP,
};

/** @brief Utility module event. */
struct util_event {
	/** Utility module application event header. */
	struct app_event_header header;
	/** Utility module event type. */
	enum util_module_event_type type;
	/** Variable that contains the reason for the shutdown request. */
	enum shutdown_reason reason;
};

APP_EVENT_TYPE_DECLARE(util_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _UTIL_MODULE_EVENT_H_ */
