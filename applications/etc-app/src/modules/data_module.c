/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology Corporation
 */

#include <zephyr/kernel.h>
#include <app_event_manager.h>
#include <zephyr/settings/settings.h>
#include "cloud/cloud_codec/data_codec.h"
#include "etc_date_time.h"
#include "etc_settings.h"
#include "etc_device.h"
#include "cloud/cloud_wrapper.h"

#define MODULE data_module
#define MODULE_DATA_SENSOR_BUFFER_COUNT 8
#define MODULE_DATA_BATTERY_BUFFER_COUNT 8
#define MODULE_LORA_SENSOR_BUFFER_COUNT 8

#include "etc_memfault.h"

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

struct send_msg_status {
	/* Current record id being sent */
	uint16_t record_id;
	/* Is there a send ongoing? */
	bool active_send;
};

/* Data module super states. */
static enum state_type {
	STATE_CLOUD_DISCONNECTED,
	STATE_CLOUD_CONNECTED,
	STATE_SHUTDOWN
} state;

static struct data_battery bat_buf[MODULE_DATA_BATTERY_BUFFER_COUNT];
static struct data_lora_sensors lora_buf[MODULE_LORA_SENSOR_BUFFER_COUNT];

static struct data_modem_static modem_stat;
static struct data_modem_dynamic modem_dynamic;

static bool first_send = true;

/* Size of the static modem (modem_stat) data structure.
 * Used to provide an array size when encoding batch data.
 */
#define MODEM_STATIC_ARRAY_SIZE 1

/* Head of ringbuffers. */
static int head_lora_buf = 0;
static int head_sensor_buf = 0;
static int head_modem_dyn_buf = 0;
static int head_bat_buf = 0;

struct cloud_codec_data codec = { 0 };
struct cloud_codec_data codec_backup = { 0 };

struct send_msg_status send_status;

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
#define DATA_QUEUE_ENTRY_COUNT		20
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

		__ASSERT_NO_MSG(err == 0);
		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(data, DATA_EVT_ERROR, err);
		}
	}

	return false;
}

static void new_config_handle(const struct etc_config *new_config)
{
	etc_settings_update(new_config);
}

static void cloud_codec_event_handler(const struct cloud_codec_evt *evt)
{
	if (evt->type == CLOUD_CODEC_EVT_CONFIG_UPDATE) {
		new_config_handle(&evt->config_update);
	} else {
		LOG_ERR("Unknown event");
	}
}

static int setup(void)
{
	int err;
	struct etc_config cfg;

	etc_settings_get_config(&cfg);
	
	err = data_codec_init(&cfg, cloud_codec_event_handler);
	if (err) {
		LOG_ERR("cloud_codec_init, error: %d", err);
		return err;
	}

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

static inline void reset_send_status(struct send_msg_status *status)
{
	status->active_send = false;
	status->record_id = 0;
}

static void data_send(enum data_event_type event,
		      struct cloud_codec_data *data)
{
	struct data_event *module_event = new_data_event();

	__ASSERT(module_event, "Not enough heap left to allocate event");

	module_event->type = event;

	BUILD_ASSERT((sizeof(data->paths) == sizeof(module_event->data.buffer.paths)),
			"Size of the object path list does not match");
	BUILD_ASSERT((sizeof(data->paths[0]) == sizeof(module_event->data.buffer.paths[0])),
			"Size of an entry in the object path list does not match");

	send_status.active_send = true;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LWM2M)) {
		memcpy(module_event->data.buffer.paths, data->paths, sizeof(data->paths));
		module_event->data.buffer.valid_object_paths = data->valid_object_paths;
	} else {
		module_event->data.buffer.buf = data->buf;
		module_event->data.buffer.len = data->len;
	}

	APP_EVENT_SUBMIT(module_event);
}

/**
 * Encode the current LwM2M data to be sent in a message.
 * 
 * @param split If true, split new record for transmission, as it might
 * 		be too large to fit into one message.
*/
static void data_encode(bool split) 
{
	union etc_device_record record;
	int ret;

	if (send_status.active_send) {
		LOG_WRN("Not sending new record."
			"Record ID %u is already being sent.", send_status.record_id);
		return;
	}

	if (first_send) {
		data_codec_prepare_update_packet(&codec);
		first_send = false;
	}

	/* Send previously stored data first, then send new measurement record. */
	if (data_codec_has_data(&codec_backup) && 
	    (!split || !data_codec_has_data(&codec))) {
		LOG_DBG("Recovering previously backed up data.");
		data_codec_recover_data(&codec, &codec_backup);
	} else {
		modem_dynamic.rsrp = quectel_bg95_get_rssi();
		modem_dynamic.qual = quectel_bg95_get_qual();
		modem_dynamic.queued = 1;

		send_status.record_id = etc_device_read_record(&record);
		if (send_status.record_id == 0) {
			LOG_INF("No record found");
			/* Report data send complete, so other modules can start
			 * sending data.
			 */
			SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
			return;
		}

		ret = data_codec_prepare_cloud_packet(&codec, &record, &modem_dynamic);
		if (ret != 0) {
			LOG_WRN("No message to publish");
			return;
		}
	}
	
	/* If splitting data is requested, move data to backup codec */
	if (split) {
		LOG_DBG("Data is too large, split.");
		data_codec_split_data(&codec, &codec_backup);
	}

	data_send(DATA_EVT_DATA_SEND, &codec);
}

static void relay_data_encode(struct relay_data_buffer *relay_data)
{
	int ret;

	__ASSERT_NO_MSG(relay_data != NULL);

	ret = data_codec_prepare_relay_packet(&codec, relay_data->data, 
					      relay_data->data_len);
	if (ret != 0) {
		LOG_WRN("Publishing relay message failed");
	}
}

static void data_send_work_fn(struct k_work *work)
{
	k_work_reschedule(&data_send_work, data_publish_timeout);
}

/* Message handler for STATE_CLOUD_DISCONNECTED. */
static void on_cloud_state_disconnected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED) &&
	    etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) {
		state_set(STATE_CLOUD_CONNECTED);	
		data_encode(false);
		return;
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_cloud_state_connected(struct data_msg_data *msg)
{
	if ((IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) &&
	    etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) || 
		(IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT_CLOUD_IN_LORA) &&
	    etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER)) 
	{
		data_encode(false);
		return;
	}

	if (IS_EVENT(msg, app, APP_EVT_CONFIG_GET)) {
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTING)) {
		/* Reset send status to allow future sends. */
		reset_send_status(&send_status);
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

	if (IS_EVENT(msg, modem, MODEM_EVT_MODEM_STATIC_DATA_READY)) {
		modem_stat.ts = msg->module.modem.data.modem_static.timestamp;
		modem_stat.queued = true;

		BUILD_ASSERT(sizeof(modem_stat.manufacturer) >=
			     sizeof(msg->module.modem.data.modem_static.manufacturer));
		BUILD_ASSERT(sizeof(modem_stat.model) >=
			     sizeof(msg->module.modem.data.modem_static.board_version));
		BUILD_ASSERT(sizeof(modem_stat.fw) >=
			     sizeof(msg->module.modem.data.modem_static.modem_fw));
		BUILD_ASSERT(sizeof(modem_stat.imei) >=
			     sizeof(msg->module.modem.data.modem_static.imei));
		BUILD_ASSERT(sizeof(modem_stat.imsi) >=
			     sizeof(msg->module.modem.data.modem_static.imsi));
		BUILD_ASSERT(sizeof(modem_stat.iccid) >=
			     sizeof(msg->module.modem.data.modem_static.iccid));

		strcpy(modem_stat.manufacturer, msg->module.modem.data.modem_static.manufacturer);
		strcpy(modem_stat.model, msg->module.modem.data.modem_static.board_version);
		strcpy(modem_stat.fw, msg->module.modem.data.modem_static.modem_fw);
		strcpy(modem_stat.imei, msg->module.modem.data.modem_static.imei);
		strcpy(modem_stat.imsi, msg->module.modem.data.modem_static.imsi);
		strcpy(modem_stat.iccid, msg->module.modem.data.modem_static.iccid);

		data_codec_prepare_modem_static_packet(&codec, &modem_stat);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_MODEM_DYNAMIC_DATA_READY)) {
		modem_dynamic.ts = msg->module.modem.data.modem_dynamic.timestamp;
		modem_dynamic.queued = true;

		modem_dynamic.nw_mode = msg->module.modem.data.modem_dynamic.act;
		modem_dynamic.area = msg->module.modem.data.modem_dynamic.tac;
		modem_dynamic.cell = msg->module.modem.data.modem_dynamic.cell_id;
		modem_dynamic.mcc = msg->module.modem.data.modem_dynamic.mcc;
		modem_dynamic.mnc = msg->module.modem.data.modem_dynamic.mnc;
		modem_dynamic.psm_active_time_s = msg->module.modem.data.modem_dynamic.active_time_s;
		modem_dynamic.psm_periodic_atu_s = msg->module.modem.data.modem_dynamic.periodic_tau_s;

		data_codec_prepare_modem_dynamic_packet(&codec, &modem_dynamic);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY)) {
		etc_device_write_record_sensor(msg->module.sensor.data.sensors);
		SEND_EVENT(data, DATA_EVT_DATA_READY);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY)) {
		etc_device_write_record_sensor(msg->module.sensor.data.sensors);
		SEND_EVENT(data, DATA_EVT_TEST_DATA_READY);
	}

	if (IS_EVENT(msg, data, DATA_EVT_RELAY_DATA_READY)) {
		relay_data_encode(&msg->module.data.data.relay_data);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
		data_codec_clear_data(&codec);
		if (send_status.record_id > 0) {
			/* Acknowledge record and encode more data, if connected to cloud */
			etc_device_set_ack_record(send_status.record_id);
		}
		reset_send_status(&send_status);
		if (state == STATE_CLOUD_CONNECTED) {
			data_encode(false);
		}
	}
		
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_FAIL)) {
		ETC_MEMFAULT_TRACE_EVENT(send_fail);
		bool split = false;
		if (msg->module.cloud.data.err == -ENOMEM ||
		    msg->module.cloud.data.err == -ECONNREFUSED) {
			split = true;
		}
		/* Reset send status on fail */
		reset_send_status(&send_status);
		if (state == STATE_CLOUD_CONNECTED) {
			data_encode(split);
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RX_OFF)) {
		reset_send_status(&send_status);
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

void data_module_thread_fn(void)
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
	int transmission_in_seconds = etc_get_tx_interval_secs();
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

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ui_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, sensor_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, lora_event);

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_relay_send(const struct shell *shell, size_t argc, char **argv)
{
	static const uint8_t cbor_data[] = {
		0xA3, 0x01, 0x19, 0x04, 0xD2, 0x02, 0x1A, 0x61, 0x89, 0x3F, 0x00,
		0x03, 0x81, 0xA4, 0x01, 0x18, 0x7B, 0x02, 0x1A, 0x64, 0x9B, 0x2D,
		0xDB, 0x03, 0x01, 0x04, 0xA5, 0x00, 0x81, 0xA4, 0x01, 0x00, 0x02, 
		0x00, 0x03, 0x63, 0x43, 0x65, 0x6C, 0x04, 0xFB, 0x40, 0x4B, 0x19, 
		0x99, 0x99, 0x99, 0x99, 0x9A, 0x01, 0x82, 0xA3, 0x01, 0x00, 0x02, 
		0x00, 0x04, 0xFB, 0x40, 0x4B, 0x19, 0x99, 0x99, 0x99, 0x99, 0x9A, 
		0xA4, 0x01, 0x01, 0x02, 0x01, 0x03, 0x63, 0x25, 0x52, 0x48, 0x04, 
		0xFB, 0x40, 0x4B, 0x19, 0x99, 0x99, 0x99, 0x99, 0x9A, 0x02, 0x81, 
		0xA3, 0x01, 0x00, 0x02, 0x00, 0x04, 0xFB, 0x40, 0x4B, 0x19, 0x99, 
		0x99, 0x99, 0x99, 0x9A, 0x03, 0x81, 0xA3, 0x01, 0x00, 0x02, 0x00, 
		0x04, 0xFB, 0x40, 0x4B, 0x19, 0x99, 0x99, 0x99, 0x99, 0x9A, 0x04, 
		0x81, 0xA3, 0x01, 0x00, 0x02, 0x00, 0x04, 0xFB, 0x40, 0x4B, 0x19, 
		0x99, 0x99, 0x99, 0x99, 0x9A};
	struct data_event *data_event = new_data_event();

	data_event->type = DATA_EVT_RELAY_DATA_READY;
	data_event->data.relay_data.data = cbor_data;
	data_event->data.relay_data.data_len = sizeof(cbor_data);
	APP_EVENT_SUBMIT(data_event);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_data,
	SHELL_CMD(relay_send, NULL, "Schedule relay data for send", cmd_relay_send),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(data, &sub_data, "ETC Data module commands", NULL);

#endif