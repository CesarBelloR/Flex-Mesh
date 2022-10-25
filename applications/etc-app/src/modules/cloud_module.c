#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>

#define MODULE cloud
#define MODULE_CLOUD_CONNECT_RETRIES 5
#define MODULE_CLOUD_THREAD_STACK_SIZE 2048

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

struct cloud_msg_data {
	union {
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
static enum sub_state_type {
	SUB_STATE_CLOUD_DISCONNECTED,
	SUB_STATE_CLOUD_CONNECTED
} sub_state;

struct cloud_backoff_delay_lookup {
	int delay;
};

/* Lookup table for backoff reconnection to cloud. Binary scaling. */
static struct cloud_backoff_delay_lookup backoff_delay[] = {
	{ 32 }, { 64 }, { 128 }, { 256 }, { 512 },
	{ 2048 }, { 4096 }, { 8192 }, { 16384 }, { 32768 },
	{ 65536 }, { 131072 }, { 262144 }, { 524288 }, { 1048576 }
};

static struct k_work_delayable connect_check_work;
const k_tid_t cloud_module_thread;

/* Variable that keeps track of how many times a reconnection to cloud
 * has been tried without success.
 */
static int connect_retries;

/* Cloud module message queue. */
#define CLOUD_QUEUE_ENTRY_COUNT		20
#define CLOUD_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(msgq_cloud, sizeof(struct cloud_msg_data),
	      CLOUD_QUEUE_ENTRY_COUNT, CLOUD_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "cloud",
	.msg_q = &msgq_cloud,
	.supports_shutdown = true
};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state) {
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

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_CLOUD_DISCONNECTED:
		return "SUB_STATE_CLOUD_DISCONNECTED";
	case SUB_STATE_CLOUD_CONNECTED:
		return "SUB_STATE_CLOUD_CONNECTED";
	default:
		return "Unknown";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state2str(sub_state),
		sub_state2str(new_state));

	sub_state = new_state;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct cloud_msg_data msg = {0};
	bool enqueue_msg = false, consume = false;

	if (is_app_event(aeh)) {
		struct app_event *evt = cast_app_event(aeh);

		msg.module.app = *evt;
		enqueue_msg = true;
	}

	if (is_modem_event(aeh)) {
		struct modem_event *evt = cast_modem_event(aeh);

		msg.module.modem = *evt;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *evt = cast_data_event(aeh);

		msg.module.data = *evt;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *evt = cast_util_event(aeh);

		msg.module.util = *evt;
		enqueue_msg = true;
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *evt = cast_cloud_event(aeh);

		msg.module.cloud = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(cloud, CLOUD_EVT_ERROR, err);
		}
	}

	return consume;
}

static void connect_check_work_fn(struct k_work *work)
{
	if ((state == STATE_LTE_CONNECTED && sub_state == SUB_STATE_CLOUD_CONNECTED) ||
	    (state == STATE_LTE_DISCONNECTED)) {
		return;
	}

	LOG_DBG("Cloud connection timeout occurred");

	SEND_EVENT(cloud, CLOUD_EVT_CONNECTION_TIMEOUT);
}

static int setup(void)
{
#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	/* After a successful initializaton, tell the bootloader that the
	 * current image is confirmed to be working.
	 */
	boot_write_img_confirmed();
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */

	return 0;
}

static void connect_cloud(void)
{
	int backoff_sec = backoff_delay[connect_retries].delay;

	LOG_DBG("Connecting to cloud");

	if (connect_retries > MODULE_CLOUD_CONNECT_RETRIES) {
		LOG_WRN("Too many failed cloud connection attempts");
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, -ENETUNREACH);
		return;
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
	k_work_cancel_delayable(&connect_check_work);
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_INITIALIZED))) {
		int err;

		state_set(STATE_LTE_DISCONNECTED);

		err = setup();
		__ASSERT(err == 0, "setp() failed");
	}
}

/* Message handler for STATE_LTE_CONNECTED. */
static void on_state_lte_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		state_set(STATE_LTE_DISCONNECTED);

		/* Explicitly disconnect cloud when you receive an LTE disconnected event.
		 * This is to clear up the cloud library state.
		 */
		disconnect_cloud();
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_CARRIER_FOTA_PENDING)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		disconnect_cloud();
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_CARRIER_FOTA_STOPPED)) {
		connect_cloud();
	}
}

/* Message handler for STATE_LTE_DISCONNECTED. */
static void on_state_lte_disconnected(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED))) {
		state_set(STATE_LTE_CONNECTED);

		/* LTE is now connected, cloud connection can be attempted */
		connect_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_CONNECTED. */
static void on_sub_state_cloud_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);

		k_work_reschedule(&connect_check_work, K_SECONDS(1));

		return;
	}
}

/* Message handler for SUB_STATE_CLOUD_DISCONNECTED. */
static void on_sub_state_cloud_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		sub_state_set(SUB_STATE_CLOUD_CONNECTED);

		connect_retries = 0;
		k_work_cancel_delayable(&connect_check_work);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT)) {
		connect_cloud();
	}
}

/* Message handler for all states. */
static void on_all_states(struct cloud_msg_data *msg)
{
	if (is_data_event(&msg->module.data.header)) {
		switch (msg->module.data.type) {
		case DATA_EVT_CONFIG_INIT:
			/* Fall through. */
		case DATA_EVT_CONFIG_READY:
			break;
		default:
			break;
		}
	}
}

static void module_thread_fn(void)
{
	int err;
	struct cloud_msg_data msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, err);
	}

	state_set(STATE_LTE_INIT);
	sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);

	k_work_init_delayable(&connect_check_work, connect_check_work_fn);

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_LTE_INIT:
			on_state_init(&msg);
			break;
		case STATE_LTE_CONNECTED:
			switch (sub_state) {
			case SUB_STATE_CLOUD_CONNECTED:
				on_sub_state_cloud_connected(&msg);
				break;
			case SUB_STATE_CLOUD_DISCONNECTED:
				on_sub_state_cloud_disconnected(&msg);
				break;
			default:
				LOG_ERR("Unknown Cloud module sub state");
				break;
			}

			on_state_lte_connected(&msg);
			break;
		case STATE_LTE_DISCONNECTED:
			on_state_lte_disconnected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_ERR("Unknown Cloud module state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(cloud_module_thread, MODULE_CLOUD_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, modem_event);
APP_EVENT_SUBSCRIBE_FIRST(MODULE, cloud_module_event);
