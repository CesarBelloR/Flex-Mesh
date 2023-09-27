/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#if defined(CONFIG_MEMFAULT)
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/core/trace_event.h>
#include <memfault/ports/watchdog.h>
#include <memfault/panics/coredump.h>
#include "etc_memfault.h"
#endif
#include <memfault_ncs.h>

#define MODULE debug_module

#include "modules_common.h"
#include "events/app_event.h"
#include "events/cloud_event.h"
#include "events/data_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"
#include "events/modem_event.h"
#include "events/ui_event.h"
#include "events/debug_event.h"
#include "etc_settings.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DEBUG_MODULE_LOG_LEVEL);

struct debug_msg_data {
	union {
		struct cloud_event cloud;
		struct util_event util;
		struct ui_event ui;
		struct sensor_event sensor;
		struct data_event data;
		struct app_event app;
		struct modem_event modem;
	} module;
};

/* Forward declarations. */
static void message_handler(struct debug_msg_data *msg);

#if defined(CONFIG_MEMFAULT)
/* Enumerator used to specify what type of Memfault that is sent. */
static enum memfault_data_type {
	METRICS,
	COREDUMP
} send_type;

static K_SEM_DEFINE(mflt_internal_send_sem, 0, 1);

static void memfault_internal_send(void)
{
entry:
	k_sem_take(&mflt_internal_send_sem, K_FOREVER);

	if (send_type == COREDUMP) {
		if (memfault_coredump_has_valid_coredump(NULL)) {
			LOG_DBG("Sending a coredump to Memfault!");
		} else {
			SEND_EVENT(debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE);
			LOG_DBG("No coredump available.");
			goto entry;
		}
	}

	memfault_zephyr_port_post_data();
	if (send_type == COREDUMP) {
		SEND_EVENT(debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE);
	}
	goto entry;
}

K_THREAD_DEFINE(mflt_send_thread, CONFIG_DEBUG_MODULE_MEMFAULT_THREAD_STACK_SIZE,
		memfault_internal_send, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

#endif /* if defined(CONFIG_MEMFAULT) */

static struct module_data self = {
	.name = "debug",
	.msg_q = NULL,
	.supports_shutdown = false,
	.supports_watchdog = true
};

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_modem_event(aeh)) {
		struct modem_event *event = cast_modem_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.modem = *event
		};

		message_handler(&debug_msg);
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *event = cast_cloud_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.cloud = *event
		};

		message_handler(&debug_msg);
	}

	if (is_sensor_event(aeh)) {
		struct sensor_event *event =
				cast_sensor_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.sensor = *event
		};

		message_handler(&debug_msg);
	}

	if (is_ui_event(aeh)) {
		struct ui_event *event = cast_ui_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.ui = *event
		};

		message_handler(&debug_msg);
	}

	if (is_app_event(aeh)) {
		struct app_event *event = cast_app_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.app = *event
		};

		message_handler(&debug_msg);
	}

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.data = *event
		};

		message_handler(&debug_msg);
	}

	if (is_util_event(aeh)) {
		struct util_event *event = cast_util_event(aeh);
		struct debug_msg_data debug_msg = {
			.module.util = *event
		};

		message_handler(&debug_msg);
	}

	return false;
}

#if defined(CONFIG_MEMFAULT)

/**
 * @brief Send Memfault data. To transfer Memfault data using an internal transport,
 *	  CONFIG_DEBUG_MODULE_MEMFAULT_USE_EXTERNAL_TRANSPORT must be selected.
 *	  Dispatching of Memfault data is offloaded to a dedicated thread.
 */
static void send_memfault_data(void)
{
	/* Offload sending of Memfault data to a dedicated thread. */
	if (memfault_packetizer_data_available()) {
		k_sem_give(&mflt_internal_send_sem);
	} else if (send_type == COREDUMP) {
		SEND_EVENT(debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE);
	}
}

/** Set the memfault device ID with information from etc settings. */
static void set_device_id(void)
{
	char device_id[ETC_SETTINGS_DEVICE_ID_LEN] = {0};

	etc_get_device_id(device_id, sizeof(device_id));
	memfault_etc_device_id_set(device_id, strlen(device_id));
}

static void add_modem_metrics(int64_t time_to_connect) 
{
	if (time_to_connect == -1) {
		return;
	}

	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(ModemTimeToConnect), 
				(uint32_t)time_to_connect);
}

static void memfault_handle_event(struct debug_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		set_device_id();
	}

	/* Send Memfault data at the same time application data is sent to save overhead
	 * compared to having Memfault SDK trigger regular updates independently. All data
	 * should preferably be sent within the same LTE RRC connected window.
	 */
	if (IS_EVENT(msg, data, DATA_EVT_SEND_COMPLETE)) {
		/* Limit how often non-coredump memfault data (events and metrics) are sent
		 * to memfault. Updates can never occur more often than the interval set by
		 * CONFIG_DEBUG_MODULE_MEMFAULT_UPDATES_MIN_INTERVAL_SEC and the first update is
		 * always sent.
		 */
		static int64_t last_update;

		if (((k_uptime_get() - last_update) <
		     (MSEC_PER_SEC * CONFIG_DEBUG_MODULE_MEMFAULT_UPDATES_MIN_INTERVAL_SEC)) &&
		    (last_update != 0)) {
			LOG_DBG("Not enough time has passed since the last Memfault update, abort");
			return;
		}

		last_update = k_uptime_get();
		send_type = METRICS;
		send_memfault_data();
		return;
	}

	/* If the module is configured to use Memfaults internal HTTP transport, coredumps are
	 * sent on an established connection to LTE.
	 */
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
		add_modem_metrics(msg->module.modem.data.time_to_connect);
		/* Send coredump on LTE CONNECTED. */
		send_type = COREDUMP;
		send_memfault_data();
		return;
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY)) {
		memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(BatteryMv), 
				(uint32_t)msg->module.sensor.data.sensors->battery_mV);
	}
}
#endif /* defined(CONFIG_MEMFAULT) */

static void handle_wdt_feed_evt(struct debug_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_WATCHDOG_FEED_REQUEST)) {
		SEND_WDT_ACK(debug, DEBUG_EVT_WDT_ACK, self.id);
	}
}

static void message_handler(struct debug_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		int err = module_start(&self);

		if (err) {
			LOG_ERR("Failed starting module, error: %d", err);
			SEND_ERROR(debug, DEBUG_EVT_ERROR, err);
		}

		/* Notify the rest of the application that it is connected to network
		 * when building for PC.
		 */
		if (IS_ENABLED(CONFIG_BOARD_QEMU_X86) || IS_ENABLED(CONFIG_BOARD_NATIVE_POSIX)) {
			{ SEND_EVENT(debug, DEBUG_EVT_EMULATOR_INITIALIZED); }
			SEND_EVENT(debug, DEBUG_EVT_EMULATOR_NETWORK_CONNECTED);
		}
	}

	handle_wdt_feed_evt(msg);
#if defined(CONFIG_MEMFAULT)
	memfault_handle_event(msg);
#endif
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, app_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, data_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, util_event);
