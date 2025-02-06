#include <stdio.h>
#include "lora_event.h"

static char *get_evt_type_str(enum lora_event_type type)
{
	switch (type) {
	case LORA_EVT_TX_READY:
		return "LORA_EVT_TX_READY";
	case LORA_EVT_RX_READY:
		return "LORA_EVT_RX_READY";
	case LORA_EVT_RX_DATA_READY:
		return "LORA_EVT_RX_DATA_READY";
	case LORA_EVT_SEND:
		return "LORA_EVT_SEND";
	case LORA_EVT_ACK:
		return "LORA_EVT_ACK";
	case LORA_EVT_NACK:
		return "LORA_EVT_NACK";
	case LORA_EVT_RELAY_START_RX:
		return "LORA_EVT_RELAY_START_RX";
	case LORA_EVT_RELAY_RX_COMPLETE:
		return "LORA_EVT_RELAY_RX_COMPLETE";
	case LORA_EVT_SHUTDOWN_READY:
		return "LORA_EVT_SHUTDOWN_READY";
	case LORA_EVT_ERROR:
		return "LORA_EVT_ERROR";
	default:
		return "Unknown event";
	}
}

static void log_lora_event(const struct app_event_header *aeh)
{
	const struct lora_event *event = cast_lora_event(aeh);

	if (event->type == LORA_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_lora_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}


APP_EVENT_INFO_DEFINE(lora_event,
		  ENCODE(),
		  ENCODE(),
		  profile_lora_event);

APP_EVENT_TYPE_DEFINE(lora_event, log_lora_event, &lora_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
