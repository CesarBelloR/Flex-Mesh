#include <stdio.h>
#include "data_event.h"

static char *get_evt_type_str(enum data_event_type type)
{
	switch (type) {
	case DATA_EVT_DATA_SEND:
		return "DATA_EVT_DATA_SEND";
	case DATA_EVT_TEST_DATA_READY:
		return "DATA_EVT_TEST_DATA_READY";
	case DATA_EVT_RELAY_DATA_READY:
		return "DATA_EVT_RELAY_DATA_READY";
	case DATA_EVT_DATA_READY:
		return "DATA_EVT_DATA_READY";
	case DATA_EVT_SEND_COMPLETE:
		return "DATA_EVT_SEND_DONE";
	case DATA_EVT_UI_DATA_READY:
		return "DATA_EVT_UI_DATA_READY";
	case DATA_EVT_UI_DATA_SEND:
		return "DATA_EVT_UI_DATA_SEND";
	case DATA_EVT_IMPACT_DATA_READY:
		return "DATA_EVT_IMPACT_DATA_READY";
	case DATA_EVT_IMPACT_DATA_SEND:
		return "DATA_EVT_IMPACT_DATA_SEND";
	case DATA_EVT_CONFIG_INIT:
		return "DATA_EVT_CONFIG_INIT";
	case DATA_EVT_CONFIG_READY:
		return "DATA_EVT_CONFIG_READY";
	case DATA_EVT_CONFIG_GET:
		return "DATA_EVT_CONFIG_GET";
	case DATA_EVT_CONFIG_SEND:
		return "DATA_EVT_CONFIG_SEND";
	case DATA_EVT_SHUTDOWN_READY:
		return "DATA_EVT_SHUTDOWN_READY";
	case DATA_EVT_DATE_TIME_OBTAINED:
		return "DATA_EVT_DATE_TIME_OBTAINED";
	case DATA_EVT_ERROR:
		return "DATA_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_data_event(const struct app_event_header *aeh)
{
	const struct data_event *event = cast_data_event(aeh);

	if (event->type == DATA_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_data_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}

APP_EVENT_INFO_DEFINE(data_event,
		  ENCODE(),
		  ENCODE(),
		  profile_data_event);

APP_EVENT_TYPE_DEFINE(data_event, log_data_event, &data_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
