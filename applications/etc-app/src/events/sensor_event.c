#include <stdio.h>
#include "sensor_event.h"

static char *get_evt_type_str(enum sensor_event_type type)
{
	switch (type) {
	case SENSOR_EVT_ENVIRONMENTAL_DATA_READY:
		return "SENSOR_EVT_ENVIRONMENTAL_DATA_READY";
	case SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY:
		return "SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY";
	case SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT:
		return "SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT";
	case SENSOR_EVT_ENVIRONMENTAL_CONNECTED:
		return "SENSOR_EVT_ENVIRONMENTAL_CONNECTED";
	case SENSOR_EVT_BATTERY_DATA_READY:
		return "SENSOR_EVT_BATTERY_DATA_READY";
	case SENSOR_EVT_SHUTDOWN_READY:
		return "SENSOR_EVT_SHUTDOWN_READY";
	case SENSOR_EVT_ERROR:
		return "SENSOR_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_sensor_event(const struct app_event_header *aeh)
{
	const struct sensor_event *event = cast_sensor_event(aeh);

	if (event->type == SENSOR_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_sensor_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}


APP_EVENT_INFO_DEFINE(sensor_event,
		  ENCODE(),
		  ENCODE(),
		  profile_sensor_event);

APP_EVENT_TYPE_DEFINE(sensor_event, log_sensor_event, &sensor_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
