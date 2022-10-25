#include <stdio.h>
#include "ui_event.h"

static char *get_evt_type_str(enum ui_event_type type)
{
	switch (type) {
	case UI_EVT_BUTTON_DATA_READY:
		return "UI_EVT_BUTTON_DATA_READY";
	case UI_EVT_SHUTDOWN_READY:
		return "UI_EVT_SHUTDOWN_READY";
	case UI_EVT_ERROR:
		return "UI_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_ui_event(const struct app_event_header *aeh)
{
	const struct ui_event *event = cast_ui_event(aeh);

	if (event->type == UI_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_ui_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}


APP_EVENT_INFO_DEFINE(ui_event,
		  ENCODE(),
		  ENCODE(),
		  profile_ui_event);

APP_EVENT_TYPE_DEFINE(ui_event, log_ui_event, &ui_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
