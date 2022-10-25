#include <stdio.h>

#include "util_event.h"

static char *get_evt_type_str(enum util_module_event_type type)
{
	switch (type) {
	case UTIL_EVT_SHUTDOWN_REQUEST:
		return "UTIL_EVT_SHUTDOWN_REQUEST";
	default:
		return "Unknown event";
	}
}

static void log_util_event(const struct app_event_header *aeh)
{
	const struct util_event *event = cast_util_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
}

static void profile_util_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}


APP_EVENT_INFO_DEFINE(util_event,
		  ENCODE(),
		  ENCODE(),
		  profile_util_event);

APP_EVENT_TYPE_DEFINE(util_event, log_util_event, &util_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
