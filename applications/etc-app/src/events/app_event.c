#include <stdio.h>
#include "app_event.h"
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(APP_EVENT, CONFIG_ETC_APP_LOG_LEVEL);

static char *type2str(enum app_data_type type)
{
	switch (type) {
	case APP_DATA_ENVIRONMENTAL:
		return "ENV";
	case APP_DATA_MODEM_STATIC:
		return "MOD_STAT";
	case APP_DATA_BATTERY:
		return "BAT";
	default:
		return "Unknown type";
	}
}

static char *get_evt_type_str(enum app_event_type type)
{
	switch (type) {
	case APP_EVT_DATA_GET:
		return "APP_EVT_DATA_GET";
	case APP_EVT_CONFIG_GET:
		return "APP_EVT_CONFIG_GET";
	case APP_EVT_DATA_GET_ALL:
		return "APP_EVT_DATA_GET_ALL";
	case APP_EVT_START:
		return "APP_EVT_START";
	case APP_EVT_LTE_CONNECT:
		return "APP_EVT_LTE_CONNECT";
	case APP_EVT_LTE_DISCONNECT:
		return "APP_EVT_LTE_DISCONNECT";
	case APP_EVT_SHUTDOWN_READY:
		return "APP_EVT_SHUTDOWN_READY";
	case APP_EVT_ERROR:
		return "APP_EVT_ERROR";
	default: {
		LOG_WRN("Unknown event type %d", (int)type);
		return "Unknown event";
	}
	}
}

static void log_app_event(const struct app_event_header *aeh)
{
	const struct app_event *event = cast_app_event(aeh);
	char data_types[50] = "\0";

	if (event->type == APP_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else if (event->type == APP_EVT_DATA_GET) {
		for (int i = 0; i < event->count; i++) {
			strcat(data_types, type2str(event->data_list[i]));

			if (i == event->count - 1) {
				break;
			}

			strcat(data_types, ", ");
		}

		APP_EVENT_MANAGER_LOG(aeh, "%s - Requested data types (%s)",
				get_evt_type_str(event->type), data_types);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_app_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}

APP_EVENT_INFO_DEFINE(app_event,
		  ENCODE(),
		  ENCODE(),
		  profile_app_event);

APP_EVENT_TYPE_DEFINE(app_event, log_app_event, &app_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
