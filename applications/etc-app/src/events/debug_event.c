/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>

#include "debug_event.h"

static char *get_evt_type_str(enum debug_event_type type)
{
	switch (type) {
	case DEBUG_EVT_MEMFAULT_DATA_READY:
		return "DEBUG_EVT_MEMFAULT_DATA_READY";
	case DEBUG_EVT_EMULATOR_INITIALIZED:
		return "DEBUG_EVT_EMULATOR_INITIALIZED";
	case DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE:
		return "DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE";
	case DEBUG_EVT_EMULATOR_NETWORK_CONNECTED:
		return "DEBUG_EVT_EMULATOR_NETWORK_CONNECTED";
	case DEBUG_EVT_ERROR:
		return "DEBUG_EVT_ERROR";
	case DEBUG_EVT_WDT_ACK:
		return "DEBUG_EVT_WDT_ACK";
	default:
		return "Unknown event";
	}
}

static void log_event(const struct app_event_header *aeh)
{
	const struct debug_event *event = cast_debug_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "%s", get_evt_type_str(event->type));
}

static void profile_event(struct log_event_buf *buf,
			 const struct app_event_header *aeh)
{
}

APP_EVENT_INFO_DEFINE(debug_event,
		ENCODE(),
		ENCODE(),
		profile_event);

APP_EVENT_TYPE_DEFINE(debug_event,
		log_event,
		&debug_event_info,
		APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
