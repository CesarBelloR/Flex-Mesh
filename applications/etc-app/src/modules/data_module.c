/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include <net/aws_iot.h>
#include "data/data_codec.h"
#include "etc_date_time.h"
#include "etc_settings.h"
#include "etc_device.h"
#define MODULE data_module
#define MODULE_DATA_THREAD_STACK_SIZE 2048
#define MODULE_DATA_SENSOR_BUFFER_COUNT 8
#define MODULE_DATA_BATTERY_BUFFER_COUNT 8
#define MODULE_LORA_SENSOR_BUFFER_COUNT 8

#include "modules_common.h"
#include "events/app_event.h"
#include "events/cloud_event.h"
#include "events/data_event.h"
#include "events/modem_event.h"
#include "events/sensor_event.h"
#include "events/ui_event.h"
#include "events/util_event.h"
#include "events/lora_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define DEVICE_SETTINGS_KEY			"data_module"
#define DEVICE_SETTINGS_CONFIG_KEY		"config"

struct data_msg_data {
	union {
		struct modem_event modem;
		struct cloud_event cloud;
		struct ui_event ui;
		struct sensor_event sensor;
		struct data_event data;
		struct app_event app;
		struct util_event util;
		struct lora_event lora;
	} module;
};

/* Data module super states. */
static enum state_type {
	STATE_CLOUD_DISCONNECTED,
	STATE_CLOUD_CONNECTED,
	STATE_SHUTDOWN
} state;

static struct data_sensors sensors_buf[MODULE_DATA_SENSOR_BUFFER_COUNT];
static struct data_battery bat_buf[MODULE_DATA_BATTERY_BUFFER_COUNT];
static struct data_lora_sensors lora_buf[MODULE_LORA_SENSOR_BUFFER_COUNT];

static struct data_modem_static modem_stat;

/* Size of the static modem (modem_stat) data structure.
 * Used to provide an array size when encoding batch data.
 */
#define MODEM_STATIC_ARRAY_SIZE 1

/* Head of ringbuffers. */
static int head_lora_buf = 0;
static int head_sensor_buf = 0;
static int head_modem_dyn_buf = 0;
static int head_bat_buf = 0;

/* Initialize publish timeout for data publish as forever */
static k_timeout_t data_publish_timeout = K_FOREVER; 

static K_SEM_DEFINE(config_load_sem, 0, 1);

static struct k_work_delayable data_send_work;

/* List used to keep track of responses from other modules with data that is
 * requested to be sampled/published.
 */
static enum app_data_type req_type_list[APP_DATA_COUNT];

/* Total number of data types requested for a particular sample/publish
 * cycle.
 */
static int recv_req_data_count;

/* Counter of data types received from other modules. When this number
 * matches the affirmed_data_type variable all requested data has been
 * received by the Data module.
 */
static int req_data_count;

/* List of data types that are supported to be sent based on LTE connection evaluation. */
enum coneval_supported_data_type {
	UNUSED,
	GENERIC,
	BATCH,
	NEIGHBOR_CELLS,
	COUNT,
};

/* Data module message queue. */
#define DATA_QUEUE_ENTRY_COUNT		10
#define DATA_QUEUE_BYTE_ALIGNMENT	4

K_MSGQ_DEFINE(msgq_data, sizeof(struct data_msg_data),
	      DATA_QUEUE_ENTRY_COUNT, DATA_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "data",
	.msg_q = &msgq_data,
	.supports_shutdown = true,
};

/* Forward declarations */
static void data_send_work_fn(struct k_work *work);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_CLOUD_DISCONNECTED:
		return "STATE_CLOUD_DISCONNECTED";
	case STATE_CLOUD_CONNECTED:
		return "STATE_CLOUD_CONNECTED";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
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

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct data_msg_data msg = {0};
	bool enqueue_msg = false;
	if (is_modem_event(aeh)) {
		struct modem_event *event = cast_modem_event(aeh);

		msg.module.modem = *event;
		enqueue_msg = true;
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *event = cast_cloud_event(aeh);

		msg.module.cloud = *event;
		enqueue_msg = true;
	}

	if (is_sensor_event(aeh)) {
		struct sensor_event *event = cast_sensor_event(aeh);
		
		msg.module.sensor = *event;
		enqueue_msg = true;
	}

	if (is_ui_event(aeh)) {
		struct ui_event *event = cast_ui_event(aeh);

		msg.module.ui = *event;
		enqueue_msg = true;
	}

	if (is_app_event(aeh)) {
		struct app_event *event = cast_app_event(aeh);

		msg.module.app = *event;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);

		msg.module.data = *event;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *event = cast_util_event(aeh);

		msg.module.util = *event;
		enqueue_msg = true;
	}

	if (is_lora_event(aeh)) {
		struct lora_event *event = cast_lora_event(aeh);

		msg.module.lora = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(data, DATA_EVT_ERROR, err);
		}
	}

	return false;
}

static int setup(void)
{
	return 0;
}

static void config_get(void)
{
	SEND_EVENT(data, DATA_EVT_CONFIG_GET);
}

static void data_module_send_message_id(uint32_t message_id)
{
	struct data_event *data_event = new_data_event();
	data_event->type = DATA_EVT_DATA_SEND;
	data_event->data.message_id = message_id;
	APP_EVENT_SUBMIT(data_event);
}

static void data_encode(void) 
{
	if (head_sensor_buf == 0) {
		return;
	}

	LOG_INF("Head sensor buf %d", head_sensor_buf);
	char* data_msg = data_codec_prepare_cloud_packet(NULL, 0, sensors_buf, head_sensor_buf,
		&modem_stat, NULL);
	if (data_msg == NULL) {
		LOG_WRN("No message to publish");
		return;
	}
	const char topic_lora_data[] = "exact/core/readings/old";

	uint16_t message_id = (uint16_t)k_uptime_get_32();
	struct aws_iot_data tx_data = {
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.topic.str = topic_lora_data,
		.topic.len = strlen(topic_lora_data),
		.ptr = data_msg,
		.len = strlen(data_msg),
		.message_id = message_id,
	};

	LOG_INF("Publishing: %s", data_msg);

	int err = aws_iot_send(&tx_data);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
	}

	head_lora_buf = 0;
	head_sensor_buf = 0;
	data_module_send_message_id(message_id);
}

static void data_send_work_fn(struct k_work *work)
{
	k_work_reschedule(&data_send_work, data_publish_timeout);
}

/* Message handler for STATE_CLOUD_DISCONNECTED. */
static void on_cloud_state_disconnected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		state_set(STATE_CLOUD_CONNECTED);	
		if ((head_sensor_buf != 0) ||
		    (head_lora_buf != 0)) {
			SEND_EVENT(data, DATA_EVT_DATA_READY);
		}
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONFIG_EMPTY) &&
	    IS_ENABLED(CONFIG_NRF_CLOUD_MQTT)) {
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_cloud_state_connected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_DATA_READY)) {
		data_encode();
		return;
	}

	if (IS_EVENT(msg, app, APP_EVT_CONFIG_GET)) {
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED)) {
		state_set(STATE_CLOUD_DISCONNECTED);
		return;
	}
}

/* Message handler for all states. */
static void on_all_states(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(data, DATA_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {
		LOG_INF("APP_EVT_DATA_GET");
		return;
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_MODEM_STATIC_DATA_READY)) {
		modem_stat.ts = msg->module.modem.data.modem_static.timestamp;
		modem_stat.queued = true;

		BUILD_ASSERT(sizeof(modem_stat.brdv) >=
			     sizeof(msg->module.modem.data.modem_static.board_version));

		BUILD_ASSERT(sizeof(modem_stat.fw) >=
			     sizeof(msg->module.modem.data.modem_static.modem_fw));

		BUILD_ASSERT(sizeof(modem_stat.imei) >=
			     sizeof(msg->module.modem.data.modem_static.imei));

		strcpy(modem_stat.brdv, msg->module.modem.data.modem_static.board_version);
		strcpy(modem_stat.fw, msg->module.modem.data.modem_static.modem_fw);
		strcpy(modem_stat.imei, msg->module.modem.data.modem_static.imei);

	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY)) {
		etc_device_write_record_sensor(msg->module.sensor.data.sensors);		
		struct data_sensors new_sensor_data = {
			.queued = true
		};

		memcpy(&new_sensor_data.data, msg->module.sensor.data.sensors, sizeof(struct sensor_data));
		data_codec_populate_sensor_internal_buffer(sensors_buf, &new_sensor_data, &head_sensor_buf, ARRAY_SIZE(sensors_buf));
		/* Send data to cloud right now after they were taken */
		SEND_EVENT(data, DATA_EVT_DATA_READY);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_NOT_SUPPORTED)) {
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RX_DATA_READY)) {
		#if 0 /* NO MVP */
		struct data_lora_sensors new_lora_data = {
			.queued = true,
			.env_ts = msg->module.lora.data.timestamp,
		};
		memcpy(new_lora_data.sensor_msg, msg->module.lora.data.sensor_msg, LORA_EVENT_MSG_DATA_LEN);
		data_codec_populate_lora_sensor_buffer(lora_buf, &new_lora_data, &head_lora_buf, ARRAY_SIZE(lora_buf));
		#endif
	}
}

static void module_thread_fn(void)
{
	int err;
	struct data_msg_data msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(data, DATA_EVT_ERROR, err);
	}

	state_set(STATE_CLOUD_DISCONNECTED);
	int transmission_in_seconds = etc_get_time_transmission_interval();
	data_publish_timeout = K_SECONDS(transmission_in_seconds);
	k_work_init_delayable(&data_send_work, data_send_work_fn);
	k_work_reschedule(&data_send_work, data_publish_timeout);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(data, DATA_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_CLOUD_DISCONNECTED:
			on_cloud_state_disconnected(&msg);
			break;
		case STATE_CLOUD_CONNECTED:
			on_cloud_state_connected(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown sub state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(data_module_thread, MODULE_DATA_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, lora_event);