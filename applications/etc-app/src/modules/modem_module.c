#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <math.h>
#include <zephyr/devicetree.h>
#include <modem_api.h>
#include "etc_device.h"
#define MODULE modem_module

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/modem_event.h"
#include "events/cloud_event.h"
#include "events/util_event.h"
#include "events/sensor_event.h"
#include "events/lora_event.h"

#if defined(CONFIG_MEMFAULT)
#include <memfault/core/trace_event.h>
#endif

#ifdef CONFIG_PM_DEVICE
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#endif

#ifdef CONFIG_LWM2M_CARRIER
#include <lwm2m_carrier.h>
#endif /* CONFIG_LWM2M_CARRIER */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

struct modem_msg_data {
	union {
		struct app_event app;
		struct cloud_event cloud;
		struct util_event util;
		struct modem_event modem;
		struct data_event data;
		struct lora_event lora;
	} module;
};

/* Modem module super states. */
static enum state_type {
	/* Initialization state where all libraries that the module depends
	 * on need to be initialized before you can enter any other state.
	 */
	STATE_INIT,
	STATE_DISCONNECTED,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_SHUTDOWN,
} state;

/* Cloud module sub states. */
static enum sub_state_lte_connected {
	SUB_STATE_MODEM_OFF,
	SUB_STATE_MODEM_PSM,
} sub_state;


/* Enumerator that specifies the data type that is sampled. */
enum sample_type {
	MODEM_STATIC,
};

/* Value that holds the latest RSRP value. */
static int16_t rsrp_value_latest;

const k_tid_t module_thread;

const struct device *modem_dev = DEVICE_DT_GET(DT_NODELABEL(quectel_bg95));

int64_t modem_wakeup_time = -1;

static bool modem_module_is_sleep = false;
/* Modem module message queue. */
#define MODEM_QUEUE_ENTRY_COUNT		10
#define MODEM_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(msgq_modem, sizeof(struct modem_msg_data),
	      MODEM_QUEUE_ENTRY_COUNT, MODEM_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "modem",
	.msg_q = &msgq_modem,
	.supports_shutdown = true,
};

static int static_modem_data_get(void);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type state)
{
	switch (state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_DISCONNECTED:
		return "STATE_DISCONNECTED";
	case STATE_CONNECTING:
		return "STATE_CONNECTING";
	case STATE_CONNECTED:
		return "STATE_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown state";
	}
}

/* Convenience functions used in internal state handling. */
static char *sub_state2str(enum state_type state)
{
	switch (state)
	{
	case SUB_STATE_MODEM_OFF:
		return "SUB_STATE_MODEM_OFF";
	case SUB_STATE_MODEM_PSM:
		return "SUB_STATE_MODEM_PSM";
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

static void sub_state_lte_connected_set(enum sub_state_lte_connected new_state)
{
	if (new_state == sub_state)
	{
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
	struct modem_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_modem_event(aeh)) {
		struct modem_event *evt = cast_modem_event(aeh);

		msg.module.modem = *evt;
		enqueue_msg = true;
	}

	if (is_app_event(aeh)) {
		struct app_event *evt = cast_app_event(aeh);

		msg.module.app = *evt;
		enqueue_msg = true;
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *evt = cast_cloud_event(aeh);

		msg.module.cloud = *evt;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *evt = cast_util_event(aeh);

		msg.module.util = *evt;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *evt = cast_data_event(aeh);

		msg.module.data = *evt;
		enqueue_msg = true;
	}

	if (is_lora_event(aeh)) {
		struct lora_event *evt = cast_lora_event(aeh);

		msg.module.lora = *evt;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(modem, MODEM_EVT_ERROR, err);
		}
	}

	return false;
}

static void modem_set_connected(void)
{
	struct modem_event *module_event = new_modem_event();

	// Do not retrieve static modem data when waking up from PSM.
	if (!(state == STATE_DISCONNECTED && sub_state == SUB_STATE_MODEM_PSM)) {
		static_modem_data_get();
	}
	state_set(STATE_CONNECTED);
	
	module_event->data.time_to_connect = modem_wakeup_time != -1 ?
				k_uptime_get() - modem_wakeup_time : -1;
	module_event->type = MODEM_EVT_LTE_CONNECTED;
	APP_EVENT_SUBMIT(module_event);
}

static void modem_evt_handler(const struct modem_api_evt *const evt)
{
	switch (evt->type) {
	case MODEM_API_CONNECTED_EVT: {
		modem_module_is_sleep = false;
		modem_set_connected();
		break;
	}
	case MODEM_API_DISCONNECTED_EVT: {
		state_set(STATE_DISCONNECTED);
		SEND_EVENT(modem, MODEM_EVT_LTE_DISCONNECTED);
		break;
	}
	case MODEM_API_PSM_ENTERED_EVT: {
		state_set(STATE_DISCONNECTED);
		sub_state_lte_connected_set(SUB_STATE_MODEM_PSM);
		SEND_EVENT(modem, MODEM_EVT_PSM_ENTERED);
	}
	}
}

static int static_modem_data_get(void)
{	
	int err;
	struct modem_event *modem_event = new_modem_event();
	struct modem_static_info modem_info = {0};

	modem_get_static_info(modem_dev, &modem_info);

	strncpy(modem_event->data.modem_static.manufacturer,
		modem_info.manufacturer,
		sizeof(modem_event->data.modem_static.manufacturer) - 1);

	strncpy(modem_event->data.modem_static.board_version,
		modem_info.model,
		sizeof(modem_event->data.modem_static.board_version) - 1);

	strncpy(modem_event->data.modem_static.modem_fw,
		modem_info.revision,
		sizeof(modem_event->data.modem_static.modem_fw) - 1);

	strncpy(modem_event->data.modem_static.iccid,
		modem_info.iccid,
		sizeof(modem_event->data.modem_static.iccid) - 1);

	strncpy(modem_event->data.modem_static.imei,
		modem_info.imei,
		sizeof(modem_event->data.modem_static.imei) - 1);
	
	strncpy(modem_event->data.modem_static.imsi,
		modem_info.imsi,
		sizeof(modem_event->data.modem_static.imsi) - 1);

	modem_event->data.modem_static.manufacturer
		[sizeof(modem_event->data.modem_static.manufacturer) - 1] = '\0';

	modem_event->data.modem_static.board_version
		[sizeof(modem_event->data.modem_static.board_version) - 1] = '\0';

	modem_event->data.modem_static.modem_fw
		[sizeof(modem_event->data.modem_static.modem_fw) - 1] = '\0';

	modem_event->data.modem_static.iccid
		[sizeof(modem_event->data.modem_static.iccid) - 1] = '\0';

	modem_event->data.modem_static.imei
		[sizeof(modem_event->data.modem_static.imei) - 1] = '\0';

	modem_event->data.modem_static.imsi
		[sizeof(modem_event->data.modem_static.imsi) - 1] = '\0';

	modem_event->data.modem_static.timestamp = k_uptime_get();
	modem_event->type = MODEM_EVT_MODEM_STATIC_DATA_READY;

	APP_EVENT_SUBMIT(modem_event);
	return 0;
}

static bool data_type_is_requested(enum app_data_type *data_list,
				   size_t count,
				   enum app_data_type type)
{
	for (size_t i = 0; i < count; i++) {
		if (data_list[i] == type) {
			return true;
		}
	}

	return false;
}

static int modem_enter_sleep(void)
{
	int rc = 0;
#ifdef CONFIG_PM_DEVICE
	rc = pm_device_action_run(modem_dev, PM_DEVICE_ACTION_SUSPEND);
	if (rc) {
		LOG_ERR("Failed to suspend the modem %d", rc);
	}
#endif
	modem_module_is_sleep = true;
	return rc;
}

static int modem_enter_wakeup(void) 
{
	int rc = 0;
#ifdef CONFIG_PM_DEVICE
	rc = pm_device_action_run(modem_dev, PM_DEVICE_ACTION_RESUME);
	if (rc) {
		LOG_ERR("Failed to suspend the modem %d", rc);
	}
#endif
	modem_module_is_sleep = false;
	return rc;
}
static int lte_connect(void)
{
	return 0;
}

static int modem_data_init(void)
{
	return 0;
}

static int setup(void)
{
	if (quectel_bg95_is_ready()) {
		modem_set_connected();
	} else {
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	}
	if (modem_dev != NULL) {
		modem_evt_handler_init(modem_dev, modem_evt_handler);
	}
	return 0;
}

/* Message handler for STATE_INIT */
static void on_state_init(struct modem_msg_data *msg)
{
	LOG_DBG("");
	int err = setup();
	__ASSERT(err == 0, "Failed running setup()");
	SEND_EVENT(modem, MODEM_EVT_INITIALIZED);
}

/* Message handler for STATE_DISCONNECTED, sub state SUB_STATE_MODEM_OFF. */
static void on_sub_state_modem_off(struct modem_msg_data *msg)
{
}

/* Message handler for STATE_DISCONNECTED, sub state SUB_STATE_MODEM_PSM. */
static void on_sub_state_modem_psm(struct modem_msg_data *msg)
{
	if ((IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) ||
	     IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT)) &&
	     etc_device_get_mode() == ETC_DEVICE_MODE_LTE_LOGGER) {
		modem_wakeup_time = k_uptime_get();
		modem_psm_cmd(modem_dev, MODEM_API_PSM_CMD_WAKEUP, NULL);
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	}
}

/* Message handler for STATE_CONNECTING. */
static void on_state_connecting(struct modem_msg_data *msg)
{
	if ((IS_EVENT(msg, app, APP_EVT_LTE_DISCONNECT)) ||
	    (IS_EVENT(msg, cloud, CLOUD_EVT_LTE_DISCONNECT))) {
		int err;
		state_set(STATE_DISCONNECTED);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
		state_set(STATE_CONNECTED);
	}
}

/* Message handler for STATE_CONNECTED. */
static void on_state_connected(struct modem_msg_data *msg)
{
}

/* Message handler for STATE_SHUTDOWN. */
static void on_state_shutdown(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_WAKEUP_REQUEST)) {
		LOG_INF("Wakeup");
		state_set(STATE_CONNECTING);
		modem_enter_wakeup();
	}
}


/* Message handler for all states. */
static void on_all_states(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		modem_enter_sleep();
		SEND_SHUTDOWN_ACK(modem, MODEM_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}
}

void modem_module_thread_fn(void)
{
	int err;
	struct modem_msg_data msg = { 0 };

	self.thread_id = k_current_get();
	LOG_INF("Go to modem");
	state_set(STATE_DISCONNECTED);
	SEND_EVENT(modem, MODEM_EVT_INITIALIZED);

	err = setup();
	if (err) {
		LOG_ERR("Failed setting up the modem, error: %d", err);
		SEND_ERROR(modem, MODEM_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_DISCONNECTED:
			switch (sub_state)
			{
			case SUB_STATE_MODEM_OFF:
				on_sub_state_modem_off(&msg);
				break;
			case SUB_STATE_MODEM_PSM:
				on_sub_state_modem_psm(&msg);
				break;
			}
			break;
		case STATE_CONNECTING:
			on_state_connecting(&msg);
			break;
		case STATE_CONNECTED:
			on_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			on_state_shutdown(&msg);
			break;
		default:
			LOG_WRN("Invalid state: %d", state);
			break;
		}

		on_all_states(&msg);
	}
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, lora_event);
APP_EVENT_SUBSCRIBE_FINAL(MODULE, util_event);
