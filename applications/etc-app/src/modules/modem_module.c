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

#define MODEM_RETRY_SUSPEND_COUNT	10

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
	SUB_STATE_MODEM_SLEEP
} sub_state;

/* Enumerator that specifies the data type that is sampled. */
enum sample_type {
	MODEM_STATIC,
};

/* Value that holds the latest RSRP value. */
static int16_t rsrp_value_latest;

const k_tid_t module_thread;

const struct device *modem_dev = DEVICE_DT_GET(DT_NODELABEL(quectel_bg95));

static void modem_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(modem_work, modem_work_fn);

int64_t modem_wakeup_time_ms = -1;

/* Modem module message queue. */
#define MODEM_QUEUE_ENTRY_COUNT		20
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
	case SUB_STATE_MODEM_SLEEP:
		return "SUB_STATE_MODEM_SLEEP";
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

		__ASSERT_NO_MSG(err == 0);
		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(modem, MODEM_EVT_ERROR, err);
		}
	}

	return false;
}

static int modem_enter_sleep(void)
{
	int rc = -ENOTSUP;
#ifdef CONFIG_PM_DEVICE
	int count = 0;
	do {
		rc = pm_device_action_run(modem_dev, PM_DEVICE_ACTION_SUSPEND);
		count++;
		if (rc == -EAGAIN) {
			LOG_WRN("Modem suspend timed out.");
		}
	}
	while (rc == -EAGAIN && count < MODEM_RETRY_SUSPEND_COUNT);
#endif

	/* If the modem can't enter sleep, we have no way of recovering
		* and risk of draining the battery. Issue an assert in this case. */
	__ASSERT_NO_MSG((rc == 0) || (rc == -EALREADY));
	if ((rc == 0) || (rc == -EALREADY)) {
		state_set(STATE_DISCONNECTED);
		sub_state_lte_connected_set(SUB_STATE_MODEM_SLEEP);
		k_work_cancel_delayable(&modem_work);

		SEND_EVENT(modem, MODEM_EVT_LTE_DISCONNECTED);
	} else {
		LOG_ERR("Failed to suspend the modem %d", rc);
	}

	return rc;
}

static int modem_enter_wakeup(void) 
{
	int rc = -ENOTSUP;
#ifdef CONFIG_PM_DEVICE
	rc = pm_device_action_run(modem_dev, PM_DEVICE_ACTION_RESUME);
	if (rc) {
		LOG_ERR("Failed to suspend the modem %d", rc);
	}
#endif
	/* If we can't turn on modem, we are in an unrecoverable state.
	 * Issue assert in this case. */
	__ASSERT_NO_MSG(rc == 0);
	if (rc == 0) {
		state_set(STATE_CONNECTING);
		k_work_reschedule(&modem_work,
				K_SECONDS(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S));
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	}

	return rc;
}

static void modem_work_fn(struct k_work *work)
{
	int ret;

	if (state == STATE_CONNECTING) {
		SEND_EVENT(modem, MODEM_EVT_CONNECT_TIMEOUT);
	}
}

static void modem_set_connected(void)
{
	struct modem_event *module_event = new_modem_event();

	// Do not retrieve static modem data when waking up from PSM.
	if (!(state == STATE_DISCONNECTED && sub_state == SUB_STATE_MODEM_PSM)) {
		static_modem_data_get();
	}
	k_work_cancel_delayable(&modem_work);
	state_set(STATE_CONNECTED);
	
	module_event->data.time_to_connect_ms = modem_wakeup_time_ms != -1 ?
				k_uptime_get() - modem_wakeup_time_ms : -1;
	module_event->type = MODEM_EVT_LTE_CONNECTED;
	APP_EVENT_SUBMIT(module_event);
}

static void new_dynamic_modem_data(const struct modem_network_data *mdm_data) {
	struct modem_event *modem_event = new_modem_event();

	modem_event->data.modem_dynamic.act = mdm_data->act;
	modem_event->data.modem_dynamic.tac = mdm_data->tac;
	modem_event->data.modem_dynamic.cell_id = mdm_data->cell_id;
	modem_event->data.modem_dynamic.active_time_s = mdm_data->active_time_s;
	modem_event->data.modem_dynamic.periodic_tau_s = mdm_data->periodic_tau_s;
	modem_event->data.modem_dynamic.cops_mode = mdm_data->cops_mode;
	modem_event->data.modem_dynamic.mcc = mdm_data->mcc;
	modem_event->data.modem_dynamic.mnc = mdm_data->mnc;

	modem_event->data.modem_dynamic.timestamp = k_uptime_get();

	modem_event->type = MODEM_EVT_MODEM_DYNAMIC_DATA_READY;
	APP_EVENT_SUBMIT(modem_event);
}

/**
 * Get the current modem on time and reset, if requested.
 * 
 * @param reset Reset the modem wakeup time to an invalid value. To be used
 * when the modem turns off.
 * 
 * @retval modem on time in ms
 * @retval -1 on error
*/
static int64_t get_modem_on_time_ms(bool reset)
{
	int64_t on_time;

	if (modem_wakeup_time_ms < 0) {
		return -1;
	}
	
	on_time = k_uptime_get() - modem_wakeup_time_ms;

	if (reset) {
		modem_wakeup_time_ms = -1;
	}

	return on_time;
}

static void modem_evt_handler(const struct modem_api_evt *const evt)
{
	LOG_DBG("Modem event %s", modem_evt_to_str(evt->type));
	switch (evt->type) {
	case MODEM_API_CONNECTED_EVT: {
		modem_set_connected();
		break;
	}
	case MODEM_API_DISCONNECTED_EVT: {
		state_set(STATE_CONNECTING);
		k_work_reschedule(&modem_work,
				  K_SECONDS(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S));
		SEND_EVENT(modem, MODEM_EVT_LTE_DISCONNECTED);
		break;
	}
	case MODEM_API_PSM_ENTERED_EVT: {
		struct modem_event *module_event = new_modem_event();

		k_work_cancel_delayable(&modem_work);
		state_set(STATE_DISCONNECTED);
		sub_state_lte_connected_set(SUB_STATE_MODEM_PSM);

		module_event->data.on_time_ms = get_modem_on_time_ms(true);
		module_event->type = MODEM_EVT_PSM_ENTERED;
		APP_EVENT_SUBMIT(module_event);
		break;
	}
	case MODEM_API_SOFT_PSM_EVT: {
		struct modem_event *module_event = new_modem_event();

		k_work_cancel_delayable(&modem_work);
		state_set(STATE_DISCONNECTED);
		sub_state_lte_connected_set(SUB_STATE_MODEM_OFF);

		module_event->data.on_time_ms = get_modem_on_time_ms(true);
		module_event->type = MODEM_EVT_PSM_ENTERED;
		APP_EVENT_SUBMIT(module_event);
		break;
	}
	case MODEM_API_PSM_WAKEUP_EVT: {
		break;
	}

	/* Power down event is not sent on PSM power down, only on regular power down. */
	case MODEM_API_POWER_DOWN_EVT: {
		struct modem_event *module_event = new_modem_event();		
		
		module_event->data.on_time_ms = get_modem_on_time_ms(true);
		module_event->type = MODEM_EVT_POWERED_DOWN;
		APP_EVENT_SUBMIT(module_event);
		break;
	}

	case MODEM_API_DYNAMIC_DATA_UPDATE_EVT: {
		new_dynamic_modem_data(evt->dynamic_data);
	}
	}
}

static int dynamic_modem_data_get(void)
{
	struct modem_api_data modem_data = {0};
	int ret;

	ret = modem_get_data(modem_dev, MODEM_API_DATA_REQUEST_DYNAMIC, &modem_data);
	if (ret != 0) {
		LOG_ERR("Can't retrieve dynamic modem data: %d", ret);
		return ret;
	}

	new_dynamic_modem_data(&modem_data.modem_network);
	return 0;
}

static int static_modem_data_get(void)
{	
	int err;
	struct modem_event *modem_event = new_modem_event();
	struct modem_api_data modem_data = {0};
	struct modem_static_info *modem_info = &modem_data.modem_info;

	modem_get_data(modem_dev, MODEM_API_DATA_REQUEST_STATIC, &modem_data);

	strncpy(modem_event->data.modem_static.manufacturer,
		modem_info->manufacturer,
		sizeof(modem_event->data.modem_static.manufacturer) - 1);

	strncpy(modem_event->data.modem_static.board_version,
		modem_info->model,
		sizeof(modem_event->data.modem_static.board_version) - 1);

	strncpy(modem_event->data.modem_static.modem_fw,
		modem_info->revision,
		sizeof(modem_event->data.modem_static.modem_fw) - 1);

	strncpy(modem_event->data.modem_static.iccid,
		modem_info->iccid,
		sizeof(modem_event->data.modem_static.iccid) - 1);

	strncpy(modem_event->data.modem_static.imei,
		modem_info->imei,
		sizeof(modem_event->data.modem_static.imei) - 1);
	
	strncpy(modem_event->data.modem_static.imsi,
		modem_info->imsi,
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

static int setup(void)
{
	struct modem_api_data modem_data;
	int ret;

	if (modem_dev != NULL) {
		modem_evt_handler_init(modem_dev, modem_evt_handler);
	}

	ret = modem_get_data(modem_dev, MODEM_API_DATA_REQUEST_POWER_STATE, 
			     &modem_data);
	/* There's something seriously wrong if this request does not return 0. */
	__ASSERT_NO_MSG(ret == 0);

	if (quectel_bg95_is_ready()) {
		modem_set_connected();
		dynamic_modem_data_get();
	} else if (modem_data.power_state == MODEM_POWER_ON) {
		state_set(STATE_CONNECTING);
		k_work_reschedule(&modem_work,
				K_SECONDS(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S));
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	} else {
		/* Power is off. Set the state machine state accordingly. */
		state_set(STATE_DISCONNECTED);

		if (modem_data.power_state == MODEM_POWER_PSM_PENDING ||
		    modem_data.power_state == MODEM_POWER_PSM) {
			sub_state_lte_connected_set(SUB_STATE_MODEM_PSM);
		} else {
			sub_state_lte_connected_set(SUB_STATE_MODEM_OFF);
		}
	}
	return 0;
}
 
/** Check if conditions are met to wake up the modem.
 * 
*/
static bool is_wakeup_modem(struct modem_msg_data *msg)
{
	bool is_wakeup;
	
	is_wakeup = (((IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) ||
		       IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT)) &&
		      etc_device_get_mode() == ETC_DEVICE_MODE_LTE_LOGGER)) ||
		    IS_EVENT(msg, app, APP_EVT_DATA_SYNC_CLOUD) ||
		    IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_START);
	return is_wakeup;
}

/* Message handler for STATE_DISCONNECTED, sub state SUB_STATE_MODEM_OFF. */
static void on_sub_state_modem_off(struct modem_msg_data *msg)
{
	if  (is_wakeup_modem(msg)) {
		int ret;
		modem_wakeup_time_ms = k_uptime_get();
		ret = modem_cmd(modem_dev, MODEM_API_CMD_POWER_ON, NULL);
		__ASSERT_NO_MSG(ret == 0);
		k_work_reschedule(&modem_work,
				K_SECONDS(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S));
		state_set(STATE_CONNECTING);
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	}
}

/* Message handler for STATE_DISCONNECTED, sub state SUB_STATE_MODEM_PSM. */
static void on_sub_state_modem_psm(struct modem_msg_data *msg)
{
	if  (is_wakeup_modem(msg)) {
		modem_wakeup_time_ms = k_uptime_get();
		modem_cmd(modem_dev, MODEM_API_CMD_PSM_WAKEUP, NULL);
		k_work_reschedule(&modem_work,
				K_SECONDS(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S));
		state_set(STATE_CONNECTING);
		SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	}
}

static void on_sub_state_modem_sleep(struct modem_msg_data *msg)
{
	if  (is_wakeup_modem(msg)) {
		int ret;
		modem_wakeup_time_ms = k_uptime_get();
		ret = modem_enter_wakeup();
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

	if (IS_EVENT(msg, modem, MODEM_EVT_CONNECT_TIMEOUT)) {
		LOG_INF("Modem connect timeout. Put to sleep.");
		/* Send modem to sleep when modem is still trying to connect
		 * and work timer expired. */
		modem_enter_sleep();
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_POWERED_DOWN)) {
		LOG_DBG("Modem powered down. Sleeping.");
		modem_enter_sleep();
	}
}

/* Message handler for STATE_CONNECTED. */
static void on_state_connected(struct modem_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_POWERED_DOWN)) {
		LOG_DBG("Modem powered down. Sleeping.");
		modem_enter_sleep();
	}
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
			case SUB_STATE_MODEM_SLEEP:
				on_sub_state_modem_sleep(&msg);
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
