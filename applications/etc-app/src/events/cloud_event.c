#include <stdio.h>
#include "cloud_event.h"

static char *get_evt_type_str(enum cloud_event_type type)
{
	switch (type) {
	case CLOUD_EVT_CONNECTED:
		return "CLOUD_EVT_CONNECTED";
	case CLOUD_EVT_DISCONNECTED:
		return "CLOUD_EVT_DISCONNECTED";
	case CLOUD_EVT_CONNECTING:
		return "CLOUD_EVT_CONNECTING";
	case CLOUD_EVT_PAUSED:
		return "CLOUD_EVT_PAUSED";
	case CLOUD_EVT_RX_OFF:
		return "CLOUD_EVT_RX_OFF";
	case CLOUD_EVT_RECONNECT_REQUEST:
		return "CLOUD_EVT_RECONNECT_REQUEST";
	case CLOUD_EVT_CONNECTION_TIMEOUT:
		return "CLOUD_EVT_CONNECTION_TIMEOUT";
	case CLOUD_EVT_DATA_SEND_ACK:
		return "CLOUD_EVT_DATA_SEND_ACK";
	case CLOUD_EVT_DATA_SEND_FAIL:
		return "CLOUD_EVT_DATA_SEND_FAIL";
	case CLOUD_EVT_REBOOT_REQUEST:
		return "CLOUD_EVT_REBOOT_REQUEST";
	case CLOUD_EVT_RECLAIM_REQUEST:
		return "CLOUD_EVT_RECLAIM_REQUEST";
	case CLOUD_EVT_LOCATION_REQUEST:
		return "CLOUD_EVT_LOCATION_REQUEST";
	case CLOUD_EVT_CONFIG_RECEIVED:
		return "CLOUD_EVT_CONFIG_RECEIVED";
	case CLOUD_EVT_CONFIG_EMPTY:
		return "CLOUD_EVT_CONFIG_EMPTY";
	case CLOUD_EVT_FOTA_START:
		return "CLOUD_EVT_FOTA_START";
	case CLOUD_EVT_FOTA_DONE:
		return "CLOUD_EVT_FOTA_DONE";
	case CLOUD_EVT_FOTA_DOWNLOADED:
		return "CLOUD_EVT_FOTA_DOWNLOADED";
	case CLOUD_EVT_FOTA_ERROR:
		return "CLOUD_EVT_FOTA_ERROR";
	case CLOUD_EVT_SHUTDOWN_READY:
		return "CLOUD_EVT_SHUTDOWN_READY";
	case CLOUD_EVT_ERROR:
		return "CLOUD_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_cloud_event(const struct app_event_header *aeh)
{
	const struct cloud_event *event = cast_cloud_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
}

static void profile_cloud_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}

APP_EVENT_INFO_DEFINE(cloud_event,
		  ENCODE(),
		  ENCODE(),
		  profile_cloud_event);

APP_EVENT_TYPE_DEFINE(cloud_event, log_cloud_event, &cloud_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
