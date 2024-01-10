#include <stdio.h>
#include "ble_event.h"

static char *get_evt_type_str(enum ble_event_type type)
{
	switch (type) {
	case BLE_EVT_DISCONNECTED:
		return "BLE_EVT_DISCONNECTED";
	case BLE_EVT_CONNECTING:
		return "BLE_EVT_CONNECTING";
	case BLE_EVT_CONNECTED:
		return "BLE_EVT_CONNECTED";
	case BLE_EVT_SECURED:
		return "BLE_EVT_SECURED";
	case BLE_EVT_CONN_FAILED:
		return "BLE_EVT_CONN_FAILED";
	case BLE_EVT_CONN_READY:
		return "BLE_EVT_CONN_READY";
	case BLE_EVT_DATA_SEND_ACK:
		return "BLE_EVT_DATA_SEND_ACK";
	case BLE_EVT_DATA_SEND_FAIL:
		return "BLE_EVT_DATA_SEND_FAIL";
	case BLE_EVT_DATA_EXPORT_FAIL:
		return "BLE_EVT_DATA_EXPORT_FAIL";
	case BLE_EVT_RECLAIM_REQUEST:
		return "BLE_EVT_RECLAIM_REQUEST";
	case BLE_EVT_SHUTDOWN_READY:
		return "BLE_EVT_SHUTDOWN_READY";
	case BLE_EVT_ERROR:
		return "BLE_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_ble_event(const struct app_event_header *aeh)
{
	const struct ble_event *event = cast_ble_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
}

static void profile_ble_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}

APP_EVENT_INFO_DEFINE(ble_event,
		  ENCODE(),
		  ENCODE(),
		  profile_ble_event);

APP_EVENT_TYPE_DEFINE(ble_event, log_ble_event, &ble_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
