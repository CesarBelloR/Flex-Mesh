/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>

#include "modem_event.h"

static char *get_evt_type_str(enum modem_event_type type)
{
	switch (type) {
	case MODEM_EVT_INITIALIZED:
		return "MODEM_EVT_INITIALIZED";
	case MODEM_EVT_LTE_CONNECTED:
		return "MODEM_EVT_LTE_CONNECTED";
	case MODEM_EVT_LTE_DISCONNECTED:
		return "MODEM_EVT_LTE_DISCONNECTED";
	case MODEM_EVT_LTE_CONNECTING:
		return "MODEM_EVT_LTE_CONNECTING";
	case MODEM_EVT_LTE_CELL_UPDATE:
		return "MODEM_EVT_LTE_CELL_UPDATE";
	case MODEM_EVT_LTE_PSM_UPDATE:
		return "MODEM_EVT_LTE_PSM_UPDATE";
	case MODEM_EVT_LTE_EDRX_UPDATE:
		return "MODEM_EVT_LTE_EDRX_UPDATE";
	case MODEM_EVT_MODEM_STATIC_DATA_READY:
		return "MODEM_EVT_MODEM_STATIC_DATA_READY";
	case MODEM_EVT_MODEM_DYNAMIC_DATA_READY:
		return "MODEM_EVT_MODEM_DYNAMIC_DATA_READY";
	case MODEM_EVT_MODEM_STATIC_DATA_NOT_READY:
		return "MODEM_EVT_MODEM_STATIC_DATA_NOT_READY";
	case MODEM_EVT_MODEM_DYNAMIC_DATA_NOT_READY:
		return "MODEM_EVT_MODEM_DYNAMIC_DATA_NOT_READY";
	case MODEM_EVT_BATTERY_DATA_NOT_READY:
		return "MODEM_EVT_BATTERY_DATA_NOT_READY";
	case MODEM_EVT_BATTERY_DATA_READY:
		return "MODEM_EVT_BATTERY_DATA_READY";
	case MODEM_EVT_NEIGHBOR_CELLS_DATA_NOT_READY:
		return "MODEM_EVT_NEIGHBOR_CELLS_DATA_NOT_READY";
	case MODEM_EVT_NEIGHBOR_CELLS_DATA_READY:
		return "MODEM_EVT_NEIGHBOR_CELLS_DATA_READY";
	case MODEM_EVT_SHUTDOWN_READY:
		return "MODEM_EVT_SHUTDOWN_READY";
	case MODEM_EVT_ERROR:
		return "MODEM_EVT_ERROR";
	case MODEM_EVT_CARRIER_INITIALIZED:
		return "MODEM_EVT_CARRIER_INITIALIZED";
	case MODEM_EVT_CARRIER_EVENT_LTE_LINK_UP_REQUEST:
		return "MODEM_EVT_CARRIER_EVENT_LTE_LINK_UP_REQUEST";
	case MODEM_EVT_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST:
		return "MODEM_EVT_CARRIER_EVENT_LTE_LINK_DOWN_REQUEST";
	case MODEM_EVT_CARRIER_FOTA_PENDING:
		return "MODEM_EVT_CARRIER_FOTA_PENDING";
	case MODEM_EVT_CARRIER_FOTA_STOPPED:
		return "MODEM_EVT_CARRIER_FOTA_STOPPED";
	case MODEM_EVT_CARRIER_REBOOT_REQUEST:
		return "MODEM_EVT_CARRIER_REBOOT_REQUEST";
	default:
		return "Unknown event";
	}
}

static void log_modem_event(const struct app_event_header *aeh)
{
	const struct modem_event *event = cast_modem_event(aeh);

	if (event->type == MODEM_EVT_ERROR) {
		APP_EVENT_MANAGER_LOG(aeh, "%s - Error code %d",
				get_evt_type_str(event->type), event->data.err);
	} else {
		APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
	}
}

static void profile_modem_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
}


APP_EVENT_INFO_DEFINE(modem_event,
		  ENCODE(),
		  ENCODE(),
		  profile_modem_event);

APP_EVENT_TYPE_DEFINE(modem_event, log_modem_event, &modem_event_info,
		      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
