#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <net/aws_iot.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include "data/etc_json.h"

#define MODULE cloud
#define MODULE_CLOUD_CONNECT_RETRIES 5
#define MODULE_CLOUD_THREAD_STACK_SIZE 1024

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
static enum sub_state_type {
	SUB_STATE_CLOUD_DISCONNECTED,
	SUB_STATE_CLOUD_CONNECTED
} sub_state;

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
static uint32_t last_message_id = 0;

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

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state)
	{
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

static void sub_state_set(enum sub_state_type new_state)
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
	if ((state == STATE_LTE_CONNECTED && sub_state == SUB_STATE_CLOUD_CONNECTED) ||
	    (state == STATE_LTE_DISCONNECTED))
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

void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
	switch (evt->type)
	{
	case AWS_IOT_EVT_CONNECTING:
	{
		LOG_DBG("AWS_IOT_EVT_CONNECTING");
		SEND_EVENT(cloud, CLOUD_EVT_CONNECTING);
		break;
	}
	case AWS_IOT_EVT_CONNECTED:
	{
		LOG_DBG("AWS_IOT_EVT_CONNECTED");
		if (evt->data.persistent_session)
		{
			LOG_DBG("Persistent session enabled");
		}
		SEND_EVENT(cloud, CLOUD_EVT_CONNECTED);
		break;
	}
	case AWS_IOT_EVT_READY:
	{
		LOG_DBG("AWS_IOT_EVT_READY");
		k_work_schedule(&shadow_work, K_NO_WAIT);
		break;
	}

	case AWS_IOT_EVT_DISCONNECTED:
	{
		LOG_DBG("AWS_IOT_EVT_DISCONNECTED");
		SEND_EVENT(cloud, CLOUD_EVT_DISCONNECTED);
		break;
	}
	case AWS_IOT_EVT_DATA_RECEIVED:
	{
		LOG_DBG("AWS_IOT_EVT_DATA_RECEIVED");
		cloud_module_on_subscribed(evt->data.msg.ptr, evt->data.msg.topic.str,
					   evt->data.msg.topic.len);
		break;
	}
	case AWS_IOT_EVT_FOTA_START:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_START");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_START);
		break;
	}
	case AWS_IOT_EVT_FOTA_ERASE_PENDING:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_ERASE_PENDING");
		break;
	}

	case AWS_IOT_EVT_FOTA_ERASE_DONE:
	{
		LOG_DBG("AWS_FOTA_EVT_ERASE_DONE");
		break;
	}

	case AWS_IOT_EVT_FOTA_DONE:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_DONE");
		k_sleep(K_SECONDS(10));
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_DONE);
		break;
	}
	case AWS_IOT_EVT_FOTA_DL_PROGRESS:
		LOG_DBG("AWS_IOT_EVT_FOTA_DL_PROGRESS, (%d%%)",
			evt->data.fota_progress);
		break;
	case AWS_IOT_EVT_ERROR:
	{
		LOG_DBG("AWS_IOT_EVT_ERROR, %d", evt->data.err);
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, evt->data.err);
		break;
	}
	case AWS_IOT_EVT_FOTA_ERROR:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_ERROR");
		SEND_EVENT(cloud, CLOUD_EVT_FOTA_ERROR);
		break;
	}
	case AWS_IOT_EVT_PUBACK:
	{
		LOG_DBG("AWS_IOT_EVT_PUBACK %d", evt->data.message_id);
		if (evt->data.message_id == last_message_id) {
			/* Cloud receives data, sleep modem */
			SEND_EVENT(cloud, CLOUD_EVT_DISCONNECTED);
		}
		break;
	}
	case AWS_IOT_EVT_PINGRESP:
	{
		LOG_DBG("AWS_IOT_EVT_PINGRESP");
		break;
	}
	default:
		LOG_DBG("Unknown AWS IoT event type: %d", evt->type);
		break;
	}
}

static int setup(void)
{
#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	/* After a successful initializaton, tell the bootloader that the
	 * current image is confirmed to be working.
	 */
	boot_write_img_confirmed();
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */
	struct aws_iot_config config;
	int len;
	char id[ETC_SETTINGS_DEVICE_ID_LEN + sizeof("urn:dev:mac:")] = "urn:dev:mac:";

	len = strlen(id);
	etc_get_device_id(&id[len], sizeof(id) - len);
	LOG_DBG("id: %s", id);

	config.client_id = id;
	config.client_id_len = strlen(config.client_id);

	int err = aws_iot_init(&config, aws_iot_event_handler);
	if (err)
	{
		LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
		return err;
	}
	LOG_DBG("Setup the AWS IoT successful");
	return 0;
}

static void connect_cloud(void)
{
	int backoff_sec = backoff_delay[connect_retries].delay;
	int err;
	LOG_DBG("Connecting to cloud");

	if (connect_retries > MODULE_CLOUD_CONNECT_RETRIES)
	{
		LOG_WRN("Too many failed cloud connection attempts");
		SEND_ERROR(cloud, CLOUD_EVT_ERROR, -ENETUNREACH);
		return;
	}

	err = aws_iot_connect(NULL);
	if (err)
	{
		LOG_ERR("aws_iot_connect, error: %d", err);
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
	int err = aws_iot_disconnect();
	if (err) {
		LOG_ERR("aws_iot_disconnect, error: %d", err);
	}
	k_work_cancel_delayable(&connect_check_work);
}

/* Message handler for STATE_LTE_INIT. */
static void on_state_init(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_INITIALIZED)))
	{
		int err;

		state_set(STATE_LTE_DISCONNECTED);
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		err = setup();
		__ASSERT(err == 0, "setp() failed");
	}
}

/* Message handler for STATE_LTE_CONNECTED. */
static void on_state_lte_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED))
	{
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		state_set(STATE_LTE_DISCONNECTED);

		/* Explicitly disconnect cloud when you receive an LTE disconnected event.
		 * This is to clear up the cloud library state.
		 */
		disconnect_cloud();
	}
}

/* Message handler for STATE_LTE_DISCONNECTED. */
static void on_state_lte_disconnected(struct cloud_msg_data *msg)
{
	if ((IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)))
	{
		state_set(STATE_LTE_CONNECTED);

		/* LTE is now connected, cloud connection can be attempted */
		connect_cloud();
	}
}

/* Message handler for SUB_STATE_CLOUD_CONNECTED. */
static void on_sub_state_cloud_connected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED))
	{
		state_set(STATE_SHUTDOWN);
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		disconnect_cloud();
		SEND_EVENT(cloud, CLOUD_EVT_REBOOT_REQUEST);
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED))
	{
		sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);
		k_work_reschedule(&connect_check_work, K_SECONDS(1));
		return;
	}
}

/* Message handler for SUB_STATE_CLOUD_DISCONNECTED. */
static void on_sub_state_cloud_disconnected(struct cloud_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED))
	{
		sub_state_set(SUB_STATE_CLOUD_CONNECTED);
		connect_retries = 0;
		k_work_cancel_delayable(&connect_check_work);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTION_TIMEOUT))
	{
		connect_cloud();
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

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(cloud, CLOUD_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}
}

char shadow_msg[256] = {0x00};

static int shadow_update(bool version_number_include)
{
	int err;
	char *message;
	time_t message_ts = 0;
	int16_t bat_voltage = 0;
	int16_t temp = 0;
	int16_t humid = 0;

	cJSON *root_obj = cJSON_CreateObject();;
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *device_obj = cJSON_CreateObject();
	cJSON *data_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL || device_obj == NULL 
		|| data_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(device_obj);
		cJSON_Delete(data_obj);
		err = -ENOMEM;
		return err;
	}

	if (version_number_include) {
		err = json_add_str(reported_obj, "version",
				    APP_VERSION_STR);
	} else {
		err = 0;
	}
	extern char* quectel_bg95_get_imei(void);
	extern char* quectel_bg95_get_revision(void);
	extern char* quectel_bg95_get_sim_number(void);

	err += json_add_str(device_obj, "imei",  (const char*)quectel_bg95_get_imei());
	err += json_add_str(device_obj, "sim",  (const char*)quectel_bg95_get_sim_number());
	err += json_add_str(device_obj, "revision", (const char*)quectel_bg95_get_revision());
	err += json_add_obj(reported_obj, "device", device_obj);
	
	err += json_add_number(data_obj, "ts", message_ts);
	err += json_add_obj(reported_obj, "status", data_obj);
	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		LOG_ERR("Failed to Json Add, error: %d", err);
		goto cleanup;
	}

	cJSON_bool ret = cJSON_PrintPreallocated(root_obj, shadow_msg, sizeof(shadow_msg), false);
	if (ret == false) {
		LOG_ERR("cJSON_Print, error: returned NULL");
		err = -ENOMEM;
		goto cleanup;
	}

	struct aws_iot_data tx_data = {
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
		.ptr = shadow_msg,
		.len = strlen(shadow_msg)
	};

	LOG_INF("Publishing: %s to AWS IoT broker", shadow_msg);

	err = aws_iot_send(&tx_data);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
	}

cleanup:
	cJSON_Delete(root_obj);
	return err;
}

static void shadow_work_fn(struct k_work *work) {
	shadow_update(true);
}

static void module_thread_fn(void)
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
	sub_state_set(SUB_STATE_CLOUD_DISCONNECTED);

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
			switch (sub_state)
			{
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
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE_FIRST(MODULE, cloud_event);
