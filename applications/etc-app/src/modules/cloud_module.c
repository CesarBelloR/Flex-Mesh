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
#include "common.h"
#define MODULE cloud
#define MODULE_CLOUD_CONNECT_RETRIES 3

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#include "events/app_event.h"
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
#include "etc_memfault.h"

#define CLOUD_RESUME_CONNECTION_TIMEOUT_S 60
#define DISCONNECT_RECONNECTION_TIMEOUT_S CLOUD_RESUME_CONNECTION_TIMEOUT_S

struct cloud_msg_data
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

/* Cloud module super states. */
static enum state_type {
	STATE_LTE_INIT,
	STATE_LTE_DISCONNECTED,
	STATE_LTE_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Cloud module sub states. */
static enum sub_state_cloud_running {
	SUB_STATE_CLOUD_DISCONNECTED,
	SUB_STATE_CLOUD_CONNECTED,
	SUB_STATE_CLOUD_CONNECTING
} sub_state_cloud_running = SUB_STATE_CLOUD_DISCONNECTED;

/* Cloud module sub states. */
static enum sub_state_lte_connected {
	SUB_STATE_CLOUD_PAUSED,
	SUB_STATE_CLOUD_RUNNING
} sub_state_lte_connected = SUB_STATE_CLOUD_RUNNING;

/* Cloud module sub states. */
static enum sub_state_lte_disconnected {
	SUB_STATE_LTE_OFF,
	SUB_STATE_LTE_PSM
} sub_state_lte_disconnected;

const k_tid_t cloud_module_thread;

/* Last publish message id */
static uint16_t last_message_id = 0;

/* Cloud module message queue. */
#define CLOUD_QUEUE_ENTRY_COUNT 20
#define CLOUD_QUEUE_BYTE_ALIGNMENT 4

K_MSGQ_DEFINE(msgq_cloud, sizeof(struct cloud_msg_data),
	      CLOUD_QUEUE_ENTRY_COUNT, CLOUD_QUEUE_BYTE_ALIGNMENT);

static void connection_timeout_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(connection_timeout_work, connection_timeout_work_handler);

static struct module_data self = {
    .name = "cloud",
    .msg_q = &msgq_cloud,
    .supports_shutdown = true};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state)
	{
	case STATE_LTE_INIT:
		return "STATE_LTE_INIT";
	case STATE_LTE_DISCONNECTED:
		return "STATE_LTE_DISCONNECTED";
	case STATE_LTE_CONNECTED:
		return "STATE_LTE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static char *sub_state_lte_connected2str(enum sub_state_lte_connected new_state)
{
	switch (new_state) {
	case SUB_STATE_CLOUD_RUNNING:
		return "SUB_STATE_CLOUD_RUNNING";
	case SUB_STATE_CLOUD_PAUSED:
		return "SUB_STATE_CLOUD_PAUSED";
	default:
		return "Unknown";
	}
}

static char *sub_state_cloud_running2str(enum sub_state_cloud_running new_state)
{
	switch (new_state)
	{
	case SUB_STATE_CLOUD_DISCONNECTED:
		return "SUB_STATE_CLOUD_DISCONNECTED";
	case SUB_STATE_CLOUD_CONNECTED:
		return "SUB_STATE_CLOUD_CONNECTED";
	case SUB_STATE_CLOUD_CONNECTING:
		return "SUB_STATE_CLOUD_CONNECTING";
	default:
		return "Unknown";
	}
}

static char *sub_state_lte_disconnected2str(enum sub_state_lte_disconnected new_state)
{
	switch (new_state)
	{
	case SUB_STATE_LTE_OFF:
		return "SUB_STATE_LTE_OFF";
	case SUB_STATE_LTE_PSM:
		return "SUB_STATE_LTE_PSM";
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

static void sub_state_cloud_running_set(enum sub_state_cloud_running new_state)
{
	if (new_state == sub_state_cloud_running) {
		LOG_DBG("Sub state: %s", sub_state_cloud_running2str(sub_state_cloud_running));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state_cloud_running2str(sub_state_cloud_running),
		sub_state_cloud_running2str(new_state));

	sub_state_cloud_running = new_state;
}

static void sub_state_lte_connected_set(enum sub_state_lte_connected new_state)
{
	if (new_state == sub_state_lte_connected)
	{
		LOG_DBG("Sub state: %s", 
			sub_state_lte_connected2str(sub_state_lte_connected));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state_lte_connected2str(sub_state_lte_connected),
		sub_state_lte_connected2str(new_state));

	sub_state_lte_connected = new_state;
}

static void sub_state_lte_disconnected_set(enum sub_state_lte_disconnected new_state)
{
	if (new_state == sub_state_lte_disconnected)
	{
		LOG_DBG("Sub state: %s", 
			sub_state_lte_disconnected2str(sub_state_lte_disconnected));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state_lte_disconnected2str(sub_state_lte_disconnected),
		sub_state_lte_disconnected2str(new_state));

	sub_state_lte_disconnected = new_state;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct cloud_msg_data msg = {0};
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
			SEND_ERROR(cloud, CLOUD_EVT_ERROR, err);
		}
	}

	return consume;
}

void cloud_wrap_event_handler(const struct cloud_wrap_event *evt)
{
	switch (evt->type)
	{
	case CLOUD_WRAP_EVT_CONNECTING:
	{
		LOG_DBG("CLOUD_WRAP_EVT_CONNECTING");
		SEND_EVENT(cloud, CLOUD_EVT_CONNECTING);
		break;
	}
	case CLOUD_WRAP_EVT_CONNECTED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_CONNECTED");
		SEND_EVENT(cloud, CLOUD_EVT_CONNECTED);
		break;
	}
	case CLOUD_WRAP_EVT_READY:
	{
		LOG_DBG("CLOUD_WRAP_EVT_READY");
		break;
	}

	case CLOUD_WRAP_EVT_DISCONNECTED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DISCONNECTED");
		SEND_EVENT(cloud, CLOUD_EVT_DISCONNECTED);
		break;
	}
	case CLOUD_WRAP_EVT_PAUSED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_PAUSED");
		SEND_EVENT(cloud, CLOUD_EVT_PAUSED);
		break;
	}

	case CLOUD_WRAP_EVT_RX_OFF:
	{
		LOG_DBG("CLOUD_WRAP_EVT_RX_OFF");
		SEND_EVENT(cloud, CLOUD_EVT_RX_OFF);
		break;
	}

	case CLOUD_WRAP_EVT_DATA_RECEIVED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DATA_RECEIVED");
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_START:
	{
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_START");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_START);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_DOWNLOADED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_DOWNLOADED");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_DOWNLOADED);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_DONE:
	{
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_DONE");
		k_sleep(K_SECONDS(10));
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_DONE);
		break;
	}
	case CLOUD_WRAP_EVT_ERROR:
	{
		LOG_DBG("CLOUD_WRAP_EVT_ERROR, %d", evt->err);
		SEND_EVENT(cloud, CLOUD_EVT_CONNECTION_TIMEOUT);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_ERROR:
	{
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_ERROR");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_ERROR);
		break;
	}
	case CLOUD_WRAP_EVT_DATA_SEND_ACK:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DATA_SEND_ACK %d", evt->message_id);
		if ((evt->message_id == last_message_id) ||
		    (evt->message_id == 0)) {
			/* Cloud received data */
			SEND_EVENT(cloud, CLOUD_EVT_DATA_SEND_ACK);
		}
		break;
	}
	case CLOUD_WRAP_EVT_DATA_SEND_FAIL:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DATA_SEND_FAIL %d", evt->message_id);
		/* Cloud did not receive data */
		struct cloud_event *evt = new_cloud_event();
		evt->type = CLOUD_EVT_DATA_SEND_FAIL;
		evt->data.err = -ECONNREFUSED;
		APP_EVENT_SUBMIT(evt);
		break;
	}
	case CLOUD_WRAP_EVT_DATA_SEND_TIMEOUT:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DATA_SEND_TIMEOUT %d", evt->message_id);
		struct cloud_event *evt = new_cloud_event();
		evt->type = CLOUD_EVT_DATA_SEND_FAIL;
		evt->data.err = -ETIMEDOUT;
		APP_EVENT_SUBMIT(evt);
		break;
	}
	case CLOUD_WRAP_EVT_REBOOT_REQUEST:
	{
		LOG_DBG("CLOUD_WRAP_EVT_REBOOT_REQUEST");
		SEND_EVENT(cloud, CLOUD_EVT_REBOOT_REQUEST);
		break;
	}
	case CLOUD_WRAP_EVT_RECLAIM_REQUEST:
	{
		LOG_DBG("CLOUD_WRAP_EVT_RECLAIM_REQUEST %d %d", 
			evt->reclaim.start_time_s, evt->reclaim.end_time_s);
		struct cloud_event *cloud_evt = new_cloud_event();
		cloud_evt->type = CLOUD_EVT_RECLAIM_REQUEST;
		cloud_evt->data.reclaim.start_time_s = evt->reclaim.start_time_s;
		cloud_evt->data.reclaim.end_time_s = evt->reclaim.end_time_s;
		APP_EVENT_SUBMIT(cloud_evt);
		break;
	}
	case CLOUD_WRAP_EVT_LOCATION_REQUEST:
	{
		LOG_DBG("CLOUD_WRAP_EVT_LOCATION_REQUEST");
		SEND_EVENT(cloud, CLOUD_EVT_LOCATION_REQUEST);
		break;
	}
	case CLOUD_WRAP_EVT_COMMAND_RELAY_REQUEST:
	{
		LOG_DBG("CLOUD_WRAP_EVT_COMMAND_RELAY_REQUEST");
		etc_common_export_relay_command(evt->data.buf, evt->data.len);
		break;
	}
	default:
		LOG_DBG("Unknown Cloud Wrap event type: %d", evt->type);
		break;
	}
}


static int setup(void)
{
	cloud_wrap_init(cloud_wrap_event_handler);
	return 0;
}

static int connect_cloud(void)
{
	int ret;
	LOG_DBG("Connecting to cloud");

	/* If starting the cloud connect fails, there is a logic error in firmware. Trigger assert.
	 */
	ret = cloud_wrap_connect();
	__ASSERT_NO_MSG((ret == 0) || (ret == -EINPROGRESS));
	if (ret == -EINPROGRESS) {
		ETC_MEMFAULT_TRACE_EVENT(cloud_connection_in_progress);
	}
	
	return ret;
}

static void connection_timeout_work_handler(struct k_work *work)
{
	ETC_MEMFAULT_TRACE_EVENT(cloud_connection_timeout);
	SEND_EVENT(cloud, CLOUD_EVT_RECONNECT_REQUEST);
}

static void disconnect_cloud(void)
{
	cloud_wrap_disconnect();
}

static void pause_cloud(void)
{
	cloud_wrap_pause();
	k_work_cancel_delayable(&connection_timeout_work);
}

static void resume_cloud(void)
{
	__ASSERT_NO_MSG(cloud_wrap_resume() == 0);
	k_work_reschedule(&connection_timeout_work, K_SECONDS(CLOUD_RESUME_CONNECTION_TIMEOUT_S));
}

static void set_cloud_connected(void)
{
	sub_state_cloud_running_set(SUB_STATE_CLOUD_CONNECTED);
	k_work_cancel_delayable(&connection_timeout_work);
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_INITIALIZED))) {
		int err;

		state_set(STATE_LTE_DISCONNECTED);
		err = setup();
		__ASSERT(err == 0, "setup() failed");
	}
}

/* Message handler for STATE_LTE_CONNECTED. */
static void on_state_lte_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED)) {
		state_set(STATE_LTE_DISCONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		state_set(STATE_LTE_DISCONNECTED);
		sub_state_lte_disconnected_set(SUB_STATE_LTE_PSM);
	}
}

/* Message handler for STATE_LTE_DISCONNECTED. */
static void on_state_lte_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED_READY)) {
		state_set(STATE_LTE_CONNECTED);
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(cloud, CLOUD_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}
}

/* Message handler for SUB_STATE_CLOUD_CONNECTED. */
static void on_sub_state_cloud_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED) ||
	    IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTING)) {
		/* Reset the cloud running state to connecting
		 * (LwM2M will attempt connection after resume).
		 * Cloud will be paused in on_sub_state_cloud_running() on events
		 * MODEM_EVT_PSM_ENTERED and MODEM_EVT_LTE_DISCONNECTED */
		sub_state_cloud_running_set(SUB_STATE_CLOUD_CONNECTING);
	}

	if (IS_EVENT(msg, data, DATA_EVT_DATA_SEND)) {
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

			err = cloud_wrap_data_send(NULL,
						   buffer->valid_object_paths,
						   true,
						   0,
						   paths);
			if (err) {
				LOG_ERR("cloud_wrap_data_send, err: %d", err);
				struct cloud_event *evt = new_cloud_event();
				evt->type = CLOUD_EVT_DATA_SEND_FAIL;
				evt->data.err = err;
				APP_EVENT_SUBMIT(evt);
			}

			return;
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DOWNLOADED)) {
		lwm2m_rd_client_update();
	}
}

/* Message handler for SUB_STATE_CLOUD_DISCONNECTED. */
static void on_sub_state_cloud_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		set_cloud_connected();
	}

	if (IS_EVENT(msg, debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE) ||
	    IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_RECONNECT_REQUEST)) {
		/* Start cloud connection process */
		if (connect_cloud() == 0) {
			sub_state_cloud_running_set(SUB_STATE_CLOUD_CONNECTING);
		}
	}
}

static void on_sub_state_cloud_connecting(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		set_cloud_connected();
	}

	/* Schedule timeout work if cloud is trying to connect when a data transmit
	 * request is received. This is a safety measure to ensure the LwM2M client
	 * is restarted if it is not yet running.
	 * Use _schedule instead of _reschedule here, as we could otherwise repeatedly
	 * reschedule the work without the work ever being executed. */
	if (IS_EVENT(msg, debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE) ||
	    IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT)) {
		k_work_schedule(&connection_timeout_work,
				K_SECONDS(DISCONNECT_RECONNECTION_TIMEOUT_S));
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RECONNECT_REQUEST)) {
		connect_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_PAUSED. */
static void on_sub_state_cloud_paused(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, debug, DEBUG_EVT_MEMFAULT_COREDUMP_COMPLETE) ||
	    IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_RECONNECT_REQUEST)) {
		resume_cloud();
		sub_state_lte_connected_set(SUB_STATE_CLOUD_RUNNING);
		if (sub_state_cloud_running == SUB_STATE_CLOUD_DISCONNECTED) {
			/* Attempt cloud connection if cloud is not actively trying to connect. */
			if (connect_cloud() == 0) {
				sub_state_cloud_running_set(SUB_STATE_CLOUD_CONNECTING);
			}
		}
	}
}

static void on_sub_state_cloud_running(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED) ||
	    IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED)) {
		pause_cloud();
		sub_state_lte_connected_set(SUB_STATE_CLOUD_PAUSED);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT)) {
		disconnect_cloud();
		pause_cloud();
		sub_state_lte_connected_set(SUB_STATE_CLOUD_PAUSED);
	}
}

static void on_state_shutdown(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, util, UTIL_EVT_WAKEUP_REQUEST)))
	{
		LOG_INF("Wakeup");
	}
}

/* Message handler for all states. */
static void on_all_states(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_DATA_SEND))
	{
		last_message_id = msg->module.data.data.message_id;
		LOG_INF("Last data send message id %d", last_message_id);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_ERROR)) {
		SEND_EVENT(cloud, CLOUD_EVT_REBOOT_REQUEST);
	}

	/* Set cloud to disconnected on all states if disconnected or timeout
	 * event is emitted.
	 */
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT)) {
		sub_state_cloud_running_set(SUB_STATE_CLOUD_DISCONNECTED);
		k_work_reschedule(&connection_timeout_work,
				  K_SECONDS(DISCONNECT_RECONNECTION_TIMEOUT_S));
	}
}

void cloud_module_thread_fn(void)
{
	int err;
	struct cloud_msg_data msg = {0};

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err)
	{
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, err);
	}

	state_set(STATE_LTE_INIT);
	sub_state_lte_connected_set(SUB_STATE_CLOUD_RUNNING);
	sub_state_cloud_running_set(SUB_STATE_CLOUD_DISCONNECTED);

	while (true)
	{
		module_get_next_msg(&self, &msg);

		switch (state)
		{
		case STATE_LTE_INIT:
			on_state_init(&msg);
			break;
		case STATE_LTE_CONNECTED:
			switch (sub_state_lte_connected) {
			case SUB_STATE_CLOUD_RUNNING:
				switch (sub_state_cloud_running) {
				case SUB_STATE_CLOUD_CONNECTED:
					on_sub_state_cloud_connected(&msg);
					break;
				case SUB_STATE_CLOUD_DISCONNECTED:
					on_sub_state_cloud_disconnected(&msg);
					break;
				case SUB_STATE_CLOUD_CONNECTING:
					on_sub_state_cloud_connecting(&msg);
					break;
				default:
					LOG_ERR("Unknown Cloud module sub state");
					break;
				}
				on_sub_state_cloud_running(&msg);
				break;
			case SUB_STATE_CLOUD_PAUSED:
				on_sub_state_cloud_paused(&msg);
				break;
			default:
				LOG_ERR("Unknown LTE connected sub state");
			}

			on_state_lte_connected(&msg);
			break;
		case STATE_LTE_DISCONNECTED:
			on_state_lte_disconnected(&msg);
			switch (sub_state_lte_disconnected) {
			case SUB_STATE_LTE_OFF:
				break;
			case SUB_STATE_LTE_PSM:
				break;
			default:
				LOG_ERR("Unknown Cloud module sub state");
				break;
			}
			break;
		case STATE_SHUTDOWN:
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
#if IS_ENABLED(CONFIG_DEBUG_MODULE)
APP_EVENT_SUBSCRIBE(MODULE, debug_event);
#endif
APP_EVENT_SUBSCRIBE_FIRST(MODULE, cloud_event);
