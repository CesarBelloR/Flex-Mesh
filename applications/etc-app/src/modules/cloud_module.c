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

#define MODULE cloud
#define MODULE_CLOUD_CONNECT_RETRIES 5

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#include "events/app_event.h"
#include "events/cloud_event.h"
#include "events/data_event.h"
#include "events/modem_event.h"
#include "events/util_event.h"
#include "events/modem_event.h"
#include "modules_common.h"
#include "app_version.h"
#include "etc_settings.h"
#include "etc_device.h"
struct cloud_msg_data
{
	union
	{
		struct app_event app;
		struct data_event data;
		struct cloud_event cloud;
		struct modem_event modem;
		struct util_event util;
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
static enum sub_state_lte_connected {
	SUB_STATE_CLOUD_DISCONNECTED,
	SUB_STATE_CLOUD_CONNECTED,
	SUB_STATE_CLOUD_PAUSED
} sub_state_lte_connected;

/* Cloud module sub states. */
static enum sub_state_lte_disconnected {
	SUB_STATE_LTE_OFF,
	SUB_STATE_LTE_PSM
} sub_state_lte_disconnected;

struct cloud_backoff_delay_lookup
{
	int delay;
};

/* Lookup table for backoff reconnection to cloud. Binary scaling. */
static struct cloud_backoff_delay_lookup backoff_delay[] = {
    {32}, {64}, {128}, {256}, {512}, {2048}, {4096}, {8192}, {16384}, {32768}, {65536}, {131072}, {262144}, {524288}, {1048576}};

static struct k_work_delayable connect_check_work;
const k_tid_t cloud_module_thread;

static void shadow_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(shadow_work, shadow_work_fn);

/* Variable that keeps track of how many times a reconnection to cloud
 * has been tried without success.
 */
static int connect_retries;

/* Last publish message id */
static uint16_t last_message_id = 0;

/* Cloud module message queue. */
#define CLOUD_QUEUE_ENTRY_COUNT 20
#define CLOUD_QUEUE_BYTE_ALIGNMENT 4

K_MSGQ_DEFINE(msgq_cloud, sizeof(struct cloud_msg_data),
	      CLOUD_QUEUE_ENTRY_COUNT, CLOUD_QUEUE_BYTE_ALIGNMENT);

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
	switch (new_state)
	{
	case SUB_STATE_CLOUD_DISCONNECTED:
		return "SUB_STATE_CLOUD_DISCONNECTED";
	case SUB_STATE_CLOUD_CONNECTED:
		return "SUB_STATE_CLOUD_CONNECTED";
	case SUB_STATE_CLOUD_PAUSED:
		return "SUB_STATE_CLOUD_PAUSED";
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

	if (enqueue_msg)
	{
		int err = module_enqueue_msg(&self, &msg);

		if (err)
		{
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(cloud, CLOUD_EVT_ERROR, err);
		}
	}

	return consume;
}

static void connect_check_work_fn(struct k_work *work)
{
	if ((state == STATE_LTE_CONNECTED && sub_state_lte_connected == SUB_STATE_CLOUD_CONNECTED))
	{
		return;
	}

	LOG_DBG("Cloud connection timeout occurred");

	SEND_EVENT(cloud, CLOUD_EVT_CONNECTION_TIMEOUT);
}

static void cloud_module_on_subscribed(const char *buf, const char *topic,
				       size_t topic_len)
{
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
		k_work_schedule(&shadow_work, K_NO_WAIT);
		break;
	}

	case CLOUD_WRAP_EVT_DISCONNECTED:
	{
		LOG_DBG("CLOUD_WRAP_EVT_DISCONNECTED");
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
		cloud_module_on_subscribed(evt->data.buf, NULL,
					   0);
		break;
	}
	case CLOUD_WRAP_EVT_FOTA_START:
	{
		LOG_DBG("CLOUD_WRAP_EVT_FOTA_START");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_START);
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
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, evt->err);
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
		SEND_EVENT(cloud, CLOUD_EVT_DATA_SEND_FAIL);
	}
	case CLOUD_WRAP_EVT_REBOOT_REQUEST:
	{
		SEND_EVENT(cloud, CLOUD_EVT_REBOOT_REQUEST);
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

static void connect_cloud(void)
{
	int backoff_sec = backoff_delay[connect_retries].delay;
	int err = 0;
	LOG_DBG("Connecting to cloud");

	if (connect_retries > MODULE_CLOUD_CONNECT_RETRIES)
	{
		LOG_WRN("Too many failed cloud connection attempts");
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, -ENETUNREACH);
		return;
	}

	if (cloud_wrap_connect() < 0) {
		LOG_WRN("Connecting failed.");
	}

	connect_retries++;

	LOG_WRN("Cloud connection establishment in progress");
	LOG_WRN("New connection attempt in %d seconds if not successful",
		backoff_sec);

	/* Start timer to check connection status after backoff */
	k_work_reschedule(&connect_check_work, K_SECONDS(backoff_sec));
}

static void disconnect_cloud(void)
{
	connect_retries = 0;
	
	cloud_wrap_disconnect();

	k_work_cancel_delayable(&connect_check_work);
}

static void pause_cloud(void)
{
	connect_retries = 0;
	
	cloud_wrap_pause();
}

static void resume_cloud(void)
{
	if (cloud_wrap_resume() != 0) {
		LOG_WRN("resuming failed.");
	}

	sub_state_lte_connected_set(SUB_STATE_CLOUD_DISCONNECTED);

	int backoff_sec = backoff_delay[connect_retries].delay;
	connect_retries++;

	/* Start timer to check connection status after backoff */
	k_work_reschedule(&connect_check_work, K_SECONDS(backoff_sec));
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_INITIALIZED)))
	{
		int err;

		state_set(STATE_LTE_DISCONNECTED);
		sub_state_lte_connected_set(SUB_STATE_CLOUD_DISCONNECTED);
		err = setup();
		__ASSERT(err == 0, "setp() failed");
	}
}

/* Message handler for STATE_LTE_CONNECTED. */
static void on_state_lte_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED))
	{
		sub_state_lte_connected_set(SUB_STATE_CLOUD_DISCONNECTED);
		state_set(STATE_LTE_DISCONNECTED);

		/* Explicitly disconnect cloud when you receive an LTE disconnected event.
		 * This is to clear up the cloud library state.
		 */
		disconnect_cloud();
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		state_set(STATE_LTE_DISCONNECTED);
		sub_state_lte_disconnected_set(SUB_STATE_LTE_PSM);
	}


	if (IS_EVENT(msg, data, DATA_EVT_DATA_SEND)) {
		if (IS_ENABLED(CONFIG_LWM2M_INTEGRATION)) {
			int err;

			struct lwm2m_obj_path paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];

			__ASSERT(ARRAY_SIZE(paths) ==
				 ARRAY_SIZE(msg->module.data.data.buffer.paths),
				 "Path object list not the same size");

			for (int i = 0; i < ARRAY_SIZE(paths); i++) {
				paths[i] = msg->module.data.data.buffer.paths[i];
			}

			err = cloud_wrap_data_send(NULL,
						   msg->module.data.data.buffer.valid_object_paths,
						   true,
						   0,
						   paths);
			if (err) {
				LOG_ERR("cloud_wrap_data_send, err: %d", err);
				SEND_EVENT(cloud, CLOUD_EVT_DATA_SEND_FAIL);
			}

			return;
		}
	}
}

/* Message handler for STATE_LTE_DISCONNECTED. */
static void on_state_lte_disconnected(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)))
	{
		state_set(STATE_LTE_CONNECTED);

		if (sub_state_lte_connected == SUB_STATE_CLOUD_PAUSED) {
			resume_cloud();
		} else {
			/* LTE is now connected, cloud connection can be attempted */
			connect_cloud();
		}
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
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		pause_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_DISCONNECTED. */
static void on_sub_state_cloud_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED))
	{
		sub_state_lte_connected_set(SUB_STATE_CLOUD_CONNECTED);
		connect_retries = 0;
		k_work_cancel_delayable(&connect_check_work);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		pause_cloud();
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT))
	{
		connect_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_PAUSED. */
static void on_sub_state_cloud_paused(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED))
	{
		sub_state_lte_connected_set(SUB_STATE_CLOUD_CONNECTED);
		connect_retries = 0;
		k_work_cancel_delayable(&connect_check_work);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT))
	{
		resume_cloud();
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
#if !defined(CONFIG_APP_AWS_IOT)
		SEND_EVENT(cloud, CLOUD_EVT_DATA_SEND_ACK);
#endif
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED)) {
		sub_state_lte_connected_set(SUB_STATE_CLOUD_PAUSED);
	}
}

static void shadow_work_fn(struct k_work *work) {
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
	sub_state_lte_connected_set(SUB_STATE_CLOUD_DISCONNECTED);

	k_work_init_delayable(&connect_check_work, connect_check_work_fn);

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
			case SUB_STATE_CLOUD_CONNECTED:
				on_sub_state_cloud_connected(&msg);
				break;
			case SUB_STATE_CLOUD_DISCONNECTED:
				on_sub_state_cloud_disconnected(&msg);
				break;
			case SUB_STATE_CLOUD_PAUSED:
				on_sub_state_cloud_paused(&msg);
				break;
			default:
				LOG_ERR("Unknown Cloud module sub state");
				break;
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
APP_EVENT_SUBSCRIBE_FIRST(MODULE, cloud_event);
