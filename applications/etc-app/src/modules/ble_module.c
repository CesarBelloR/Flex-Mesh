#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include "data/etc_json.h"
#include "cloud/cloud_wrapper.h"

#define MODULE ble

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#include "events/app_event.h"
#include "events/ble_event.h"
#include "events/cloud_event.h"
#include "events/data_event.h"
#include "events/modem_event.h"
#include "events/util_event.h"
#include "events/modem_event.h"
#include "events/debug_event.h"
#include "modules_common.h"
#include "app_version.h"
#include "etc_settings.h"
#include "etc_device.h"
#include "etc_ble.h"

struct ble_msg_data
{
	union
	{
		struct app_event app;
		struct data_event data;
		struct cloud_event cloud;
		struct modem_event modem;
		struct util_event util;
		struct debug_event debug;
	} module;
};

/* BLE module super states. */
static enum state_type {
	STATE_BLE_INIT,
	STATE_BLE_DISCONNECTED,
	STATE_BLE_CONNECTED,
	STATE_BLE_SHUTDOWN
} state;

/* Variables to save BLE buffer */
static uint8_t ble_buf[CONFIG_LWM2M_COAP_MAX_MSG_SIZE];
static uint8_t ble_channel_out = ETC_BLE_SENSOR_CHAR;
/* BLE module message queue. */
#define BLE_QUEUE_ENTRY_COUNT 10
#define BLE_QUEUE_BYTE_ALIGNMENT 4

K_MSGQ_DEFINE(msgq_ble, sizeof(struct ble_msg_data),
	      BLE_QUEUE_ENTRY_COUNT, BLE_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
    .name = "ble",
    .msg_q = &msgq_ble,
    .supports_shutdown = true};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state)
	{
	case STATE_BLE_INIT:
		return "STATE_BLE_INIT";
	case STATE_BLE_DISCONNECTED:
		return "STATE_BLE_DISCONNECTED";
	case STATE_BLE_CONNECTED:
		return "STATE_BLE_CONNECTED";
	case STATE_BLE_SHUTDOWN:
		return "STATE_BLE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state)
	{
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct ble_msg_data msg = {0};
	bool enqueue_msg = false, consume = false;

	if (is_app_event(aeh))
	{
		struct app_event *evt = cast_app_event(aeh);

		msg.module.app = *evt;
		enqueue_msg = true;
	}

	if (is_modem_event(aeh))
	{
		struct modem_event *evt = cast_modem_event(aeh);

		msg.module.modem = *evt;
		enqueue_msg = true;
	}

	if (is_data_event(aeh))
	{
		struct data_event *evt = cast_data_event(aeh);

		msg.module.data = *evt;
		enqueue_msg = true;
	}

	if (is_util_event(aeh))
	{
		struct util_event *evt = cast_util_event(aeh);

		msg.module.util = *evt;
		enqueue_msg = true;
	}

	if (is_cloud_event(aeh))
	{
		struct cloud_event *evt = cast_cloud_event(aeh);

		msg.module.cloud = *evt;
		enqueue_msg = true;
	}

	if (is_debug_event(aeh))
	{
		struct debug_event *evt = cast_debug_event(aeh);

		msg.module.debug = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg)
	{
		int err = module_enqueue_msg(&self, &msg);

		__ASSERT_NO_MSG(err == 0);
		if (err)
		{
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(ble, BLE_EVT_ERROR, err);
		}
	}

	return consume;
}

static void ble_module_evt_handler(const struct etc_ble_evt *evt)
{
	switch (evt->type) {
		case ETC_BLE_EVT_DISCONNECTED: {
			SEND_EVENT(ble, BLE_EVT_DISCONNECTED);
			state_set(STATE_BLE_DISCONNECTED);
			break;
		}
		case ETC_BLE_EVT_CONNECTING: {
			SEND_EVENT(ble, BLE_EVT_CONNECTING);
			break;
		}
		case ETC_BLE_EVT_CONNECTED: {
			SEND_EVENT(ble, BLE_EVT_CONNECTED);
			state_set(STATE_BLE_CONNECTED);
			break;
		}
		case ETC_BLE_EVT_CCC_MEASURE_READY: {
			ble_channel_out = ETC_BLE_SENSOR_CHAR;
			SEND_EVENT(ble, BLE_EVT_CONN_READY);
			break;
		}
		case ETC_BLE_EVT_CCC_RECLAIM_READY: {
			ble_channel_out = ETC_BLE_RECLAIM_CHAR;
			struct ble_event *ble_evt = new_ble_event();
			ble_evt->type = BLE_EVT_RECLAIM_REQUEST;
			ble_evt->data.reclaim.start_time_s = evt->reclaim.start_time_s;
			ble_evt->data.reclaim.end_time_s = evt->reclaim.end_time_s;
			APP_EVENT_SUBMIT(ble_evt);
			break;
		};
		case ETC_BLE_EVT_ERR: {
			SEND_EVENT(ble, BLE_EVT_ERROR);
			break;
		}
		default:
			LOG_WRN("Unknown type %d", evt->type);
			break;
	}
}

static int setup(void)
{
#if defined(CONFIG_BT)
	if (etc_device_get_mode() == ETC_DEVICE_MODE_BLE) {
		return etc_ble_init(ble_module_evt_handler);
	}
#endif
	return 0;
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct ble_msg_data *msg)
{
	
}

static void on_state_shutdown(struct ble_msg_data *msg)
{
	if ((IS_EVENT(msg, util, UTIL_EVT_WAKEUP_REQUEST)))
	{
		LOG_INF("Wakeup");
	}
}

/* Message handler for connected states. */
static void on_connected_states(struct ble_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_DATA_SEND_BLE)) {
#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT
		{
			int err;
			err = etc_ble_notify(ble_channel_out, msg->module.data.data.buffer.buf, 
				msg->module.data.data.buffer.buf_len, true);
			if (err) {
				if (err != -ENOTCONN) {
					LOG_ERR("Failed to send data to BLE");
				}
				SEND_ERROR(ble, BLE_EVT_DATA_SEND_FAIL, err);
			} else {
				SEND_EVENT(ble, BLE_EVT_DATA_SEND_ACK);
			}
			return;
		}
#else
		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
			int err;

			struct lwm2m_obj_path paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];

			struct data_module_data_buffers *buffer = (struct data_module_data_buffers *)
				msg->module.data.data.buffer.buf;

			__ASSERT(ARRAY_SIZE(paths) ==
				 ARRAY_SIZE(buffer->paths),
				 "Path object list not the same size");

			for (int i = 0; i < ARRAY_SIZE(paths); i++) {
				paths[i] = buffer->paths[i];
			}

			int ble_len = sizeof(ble_buf);
			err = cloud_wrap_data_export(paths, 
				buffer->valid_object_paths, ble_buf, &ble_len);
			if (err) {
				LOG_ERR("cloud_wrap_data_export, err: %d", err);
				SEND_ERROR(ble, BLE_EVT_DATA_EXPORT_FAIL, err);
			} else {
				err = etc_ble_notify(ble_channel_out, ble_buf, ble_len, true);
				if (err) {
					if (err != -ENOTCONN) {
						LOG_ERR("Failed to send data to BLE");
					}
					SEND_ERROR(ble, BLE_EVT_DATA_SEND_FAIL, err);
				} else {
					SEND_EVENT(ble, BLE_EVT_DATA_SEND_ACK);
				}
			}
			return;
		}
#endif
	}

	if (IS_EVENT(msg, data, DATA_EVT_SEND_COMPLETE)) {
		/* By default, channel out is sensor characteristic */
		ble_channel_out = ETC_BLE_SENSOR_CHAR;
	}
}

/* Message handler for all states. */
static void on_all_states(struct ble_msg_data *msg)
{

}

void ble_module_thread_fn(void)
{
	int err;
	struct ble_msg_data msg = {0};

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err)
	{
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(ble, BLE_EVT_ERROR, err);
	}
	
	err = setup();
	if (err) {
		LOG_ERR("Failed to setup BLE module, error: %d", err);
		SEND_ERROR(ble, BLE_EVT_ERROR, err);
	}

	state_set(STATE_BLE_DISCONNECTED);
	while (true)
	{
		module_get_next_msg(&self, &msg);

		switch (state)
		{
		case STATE_BLE_INIT:
			on_state_init(&msg);
			break;
		case STATE_BLE_CONNECTED:
			on_connected_states(&msg);
			break;
		case STATE_BLE_DISCONNECTED:
			break;
		case STATE_BLE_SHUTDOWN:
			on_state_shutdown(&msg);
			break;
		default:
			LOG_ERR("Unknown Cloud module state.");
			break;
		}

		on_all_states(&msg);
	}
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);
#if IS_ENABLED(CONFIG_DEBUG_MODULE)
APP_EVENT_SUBSCRIBE(MODULE, debug_event);
#endif

