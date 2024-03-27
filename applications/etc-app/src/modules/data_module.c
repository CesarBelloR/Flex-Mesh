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
#include "etc_ble.h"
#include "etc_battery.h"
#include "cloud/lwm2m/lwm2m_firmware.h"
#include "cloud/cloud_wrapper.h"

#define MODULE data_module

#include "etc_memfault.h"
#include "etc_functional_test.h"

#include "modules_common.h"
#include "events/app_event.h"
#include "events/cloud_event.h"
#include "events/ble_event.h"
#include "events/data_event.h"
#include "events/modem_event.h"
#include "events/sensor_event.h"
#include "events/ui_event.h"
#include "events/util_event.h"
#include "events/lora_event.h"
#include "common.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define DEVICE_SETTINGS_KEY			"data_module"
#define DEVICE_SETTINGS_CONFIG_KEY		"config"
#define DEVICE_PAYLOAD_LEGACY_LEN	128

struct data_msg_data {
	union {
		struct modem_event modem;
		struct cloud_event cloud;
		struct ble_event ble;
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

static struct data_modem_static modem_stat;
static struct data_modem_dynamic modem_dynamic;

static bool first_send = true;

/* Size of the static modem (modem_stat) data structure.
 * Used to provide an array size when encoding batch data.
 */
#define MODEM_STATIC_ARRAY_SIZE 1

/* Head of ringbuffers. */
static int head_lora_buf = 0;
static int head_modem_dyn_buf = 0;

/* Buffer to save relay data */
static uint8_t data_relay_buf[CONFIG_LWM2M_ETC_RELAY_OBJ_DATA_SIZE] = {0x00};

struct cloud_codec_data codec = { 0 };
struct cloud_codec_data ble_codec = { 0 };
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

/* Save the current/previous reclaim state, so we can change the LwM2M reclaim
 * status accordingly.
 */
static bool reclaim_active;

/* Define a buffer to save data encoded*/
static struct data_module_data_buffers data_encoded_buffers;

/* Define a payload buffer with legacy format */
static char data_payload_buf[DEVICE_PAYLOAD_LEGACY_LEN] = {0x00};

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

	if (is_ble_event(aeh)) {
		struct ble_event *event = cast_ble_event(aeh);

		msg.module.ble = *event;
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

static void functional_test_event_handler(const enum functional_test_evt evt)
{
	switch (evt) {
	case FUNC_TEST_EVT_SEND_DATA: {
		SEND_EVENT(data, DATA_EVT_FUNCTIONAL_TEST_SEND_DATA);
	}
		break;
	case FUNC_TEST_EVT_TIMEOUT: {
		struct data_event *data_event = new_data_event();
		data_event->type = DATA_EVT_FUNCTIONAL_TEST_COMPLETE;
		data_event->data.test_result = functional_test_get_result();
		APP_EVENT_SUBMIT(data_event);
		break;
	}
	}
}

static void stop_functional_test(void)
{
	if (functional_test_stop()) {
		enum functional_test_result result = functional_test_get_result();

		if (result == FUNC_TEST_SUCCESS) {
			etc_set_power_mode(ETC_POWER_MODE_PROBE);
		}

		struct data_event *data_event = new_data_event();
		data_event->type = DATA_EVT_FUNCTIONAL_TEST_COMPLETE;
		data_event->data.test_result = result;
		APP_EVENT_SUBMIT(data_event);
	}
}

static int setup(void)
{
	int err;
	struct etc_config cfg;

	int transmission_in_seconds = etc_get_tx_interval_secs();
	data_publish_timeout = K_SECONDS(transmission_in_seconds);
	k_work_init_delayable(&data_send_work, data_send_work_fn);
	k_work_reschedule(&data_send_work, data_publish_timeout);

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

	BUILD_ASSERT((sizeof(data->paths) == sizeof(data_encoded_buffers.paths)),
			"Size of the object path list does not match");
	BUILD_ASSERT((sizeof(data->paths[0]) == sizeof(data_encoded_buffers.paths[0])),
			"Size of an entry in the object path list does not match");

	send_status.active_send = true;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LWM2M)) {
		memcpy(data_encoded_buffers.paths, data->paths, sizeof(data->paths));
		data_encoded_buffers.valid_object_paths = data->valid_object_paths;
	} else {
		data_encoded_buffers.buf = data->buf;
		data_encoded_buffers.len = data->len;
	}

	/* Update data buffer */
	module_event->data.buffer.buf = (uint8_t*)&data_encoded_buffers;

	APP_EVENT_SUBMIT(module_event);
}

#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT
static void data_send_buf(enum data_event_type event, uint8_t* buf, uint8_t buf_len)
{
	struct data_event *module_event = new_data_event();

	__ASSERT(module_event, "Not enough heap left to allocate event");

	module_event->type = event;

	/* Update data buffer */
	module_event->data.buffer.buf = buf;
	module_event->data.buffer.buf_len = buf_len;
	APP_EVENT_SUBMIT(module_event);
}
#endif

/**
 * Encode the current LwM2M data to be sent in a message.
 * 
 * @param split If true, split new record for transmission, as it might
 * 		be too large to fit into one message.
*/
static void data_encode_for_cloud(bool split) 
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

	if (functional_test_get_state() == FUNC_TEST_STATE_SENDING_DATA) {
		struct functional_test_data test_data;
		LOG_DBG("Sending functional test data");

		functional_test_get_data(&test_data);
		/* Store currently queued data in backup codec. */
		data_codec_move_data(&codec, &codec_backup);
		data_codec_prepare_functional_test_data(&codec,
							&test_data);
		functional_test_set_state(FUNC_TEST_STATE_WAITING_FOR_ACK);
	} else if (data_codec_has_data(&codec_backup) && 
	    (!split || !data_codec_has_data(&codec))) {
		/* Send previously stored data first, then send new measurement record. */
		LOG_DBG("Recovering previously backed up data.");
		data_codec_recover_data(&codec, &codec_backup);
	} else {
		bool reclaim_status;
		modem_dynamic.rsrp = quectel_bg95_get_rsrp();
		modem_dynamic.qual = quectel_bg95_get_rsrq();
		modem_dynamic.queued = 1;

		send_status.record_id = etc_device_read_record(&record, 
							       &reclaim_status);
		/* Only add a record if it is valid. */
		if (send_status.record_id != 0) {
			ret = data_codec_prepare_cloud_packet(&codec, &record, &modem_dynamic);
			if (ret != 0) {
				LOG_WRN("Error populating data codec");
			}
		}

		/* Update reclaim status */							   
		if (reclaim_status != reclaim_active) {
			if (reclaim_status) {
				data_codec_update_reclaim_state(&codec,
								RECLAIM_IN_PROGRESS);
				reclaim_active = true;
			} else {
				data_codec_update_reclaim_state(&codec,
								RECLAIM_SUCCESS);
				reclaim_active = false;
			}
		} else if (send_status.record_id == 0) {
			LOG_INF("No record found");
			/* Return early and report data send complete if we don't
			 * have any new data to send, so other modules can start
			 * sending data.
			 */
			SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
			/* Trigger the OTA pending job */
			lwm2m_firmware_start_pending_job();
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

static void data_encode_for_ble() 
{
	if (!etc_ble_get_is_connected()) return;
	union etc_device_record record;
	int ret;
	uint8_t data_payload_len = sizeof(data_payload_buf);
	bool reclaim_status;
	send_status.record_id = etc_device_read_record(&record, &reclaim_status);
	/* Only add a record if it is valid. */
	if (send_status.record_id != 0) {
#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT
		ret = etc_common_prepare_logger_legacy_data(record, reclaim_status,
			data_payload_buf, &data_payload_len);
		if (ret == 0) {
			LOG_DBG("Legacy payload: %.*s", data_payload_len, data_payload_buf);
		}
#else
		ret = data_codec_prepare_ble_packet(&ble_codec, &record);
#endif
		if (ret != 0) {
			LOG_WRN("No message to send over BLE");
			return;
		}
	}

	/* Update reclaim status */							   
	if (send_status.record_id == 0) {
		LOG_INF("No record found");
		if (reclaim_active) {
			etc_ble_notify_reclaim_status(0);
			reclaim_active = false;
		}
		/* Return early and report data send complete if we don't
		 * have any new data to send, so other modules can start
		 * sending data.
		 */
		SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
		return;
	}

#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT
	data_send_buf(DATA_EVT_DATA_SEND_BLE, data_payload_buf, data_payload_len);
#else
	data_send(DATA_EVT_DATA_SEND_BLE, &ble_codec);
#endif
}

static void relay_data_encode(void)
{
	if (send_status.active_send) {
		LOG_WRN("Not sending new record."
			"Record ID %u is already being sent.", send_status.record_id);
		return;
	}

	struct etc_device_relay_record record = {0x00};
	int ret = etc_device_read_relay_data(&record);
	if (!ret) {
		int data_len = sizeof(data_relay_buf);
		ret = etc_common_prepare_relay_legacy_data(&record, data_relay_buf, &data_len);
		if (!ret) {
			LOG_DBG("Relay message %s", data_relay_buf);
			ret = data_codec_prepare_relay_packet(&codec, data_relay_buf, 
							     data_len, true);
			if (ret) {
				LOG_WRN("Error populating data codec");
				return;
			} else {
				data_send(DATA_EVT_DATA_SEND, &codec);
			}
		} else {
			LOG_ERR("Can't prepare package for relay");
		}
	} else {
		LOG_INF("No record found");
		SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
		/* Trigger the OTA pending job */
		lwm2m_firmware_start_pending_job();
	}
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
		if (functional_test_get_state() == FUNC_TEST_STATE_COLLECTING_DATA) {
			functional_test_schedule_send();
		} else if ((etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) || 
			   (etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_MAGNET) ||
			   ((etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) && 
			   (etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_CLOUD_LORA))) {
			data_encode_for_cloud(false);
		} else if (etc_device_is_relay()) {
			reset_send_status(&send_status);
			relay_data_encode();
		}
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_cloud_state_connected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) && 
	    etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER)
	{
		data_encode_for_cloud(false);
		return;
	}

	if ((IS_EVENT(msg, app, APP_EVT_DATA_SYNC_CLOUD) && 
		(((etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) && 
		(etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_CLOUD_LORA)) || 
		(etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) || 
		(etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_MAGNET)))) {
		data_encode_for_cloud(false);
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

	if (IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_START) ||
	    IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_SEND_DATA)) {
		int rsrp = quectel_bg95_get_rsrp();
		track_functional_test(DATA_TYPE_MODEM, &rsrp);
		functional_test_set_state(FUNC_TEST_STATE_SENDING_DATA);
		data_encode_for_cloud(false);
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

		etc_set_relay_iccid(modem_stat.iccid);		
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
		if (etc_device_is_relay()) {
			/* No action required */
		} else {
			etc_device_write_record_sensor(msg->module.sensor.data.sensors);
			uint8_t bat_percent = 
				etc_battery_percentage_from_voltage(msg->module.sensor.data.sensors->battery_mV);
			etc_ble_notify_battery(bat_percent);
			SEND_EVENT(data, DATA_EVT_DATA_READY);
		}

	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY)) {
		etc_device_write_record_sensor(msg->module.sensor.data.sensors);
		SEND_EVENT(data, DATA_EVT_TEST_DATA_READY);
	}

	if (IS_EVENT(msg, data, DATA_EVT_RELAY_DATA_READY)) {
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
		if (etc_device_is_relay()) {
			reset_send_status(&send_status);
			data_codec_clear_data(&codec);
			if (state == STATE_CLOUD_CONNECTED) {
				relay_data_encode();
			}
		} else {
			if (functional_test_get_state() == FUNC_TEST_STATE_WAITING_FOR_ACK) {
				bool ack = true;
				track_functional_test(DATA_TYPE_ACK, (void *)&ack);
				stop_functional_test();
			}
			if (send_status.record_id > 0) {
				/* Acknowledge record and encode more data, if connected to cloud */
				etc_device_set_ack_record(send_status.record_id);
			}
			reset_send_status(&send_status);
			data_codec_clear_data(&codec);
			if (state == STATE_CLOUD_CONNECTED) {
				data_encode_for_cloud(false);
			}
		}
	}

	if (IS_EVENT(msg, ble, BLE_EVT_DATA_SEND_ACK)) {
		data_codec_clear_data(&codec);
		LOG_DBG("Record ID %d", send_status.record_id);
		if (send_status.record_id > 0) {
			/* Acknowledge record and encode more data, if connected to cloud */
			etc_device_set_ack_record(send_status.record_id);
		}
		reset_send_status(&send_status);
		data_encode_for_ble();
	}

	if (IS_EVENT(msg, ble, BLE_EVT_DATA_SEND_FAIL)) {
		/* Reset send status on fail */
		reset_send_status(&send_status);
		if (msg->module.ble.data.err == -ENOTCONN) {
			/* No connection, notify send complete */
			SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
		}
	}
 
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_FAIL)) {
		bool split = false;
		if (msg->module.cloud.data.err == -ENOMEM ||
		    msg->module.cloud.data.err == -ECONNREFUSED ||
		    msg->module.cloud.data.err == -E2BIG) {
			split = true;
		}
		/* Reset send status on fail */
		reset_send_status(&send_status);
		if (state == STATE_CLOUD_CONNECTED) {
			data_encode_for_cloud(split);
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RX_OFF)) {
		reset_send_status(&send_status);
	}
	
	if (IS_EVENT(msg, lora, LORA_EVT_RX_READY)) {
		if (state == STATE_CLOUD_CONNECTED) {
			relay_data_encode();
		}
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RECLAIM_REQUEST)) {
		int ret;
		bool err = false;
		
		ret = etc_device_record_reclaim(
			msg->module.cloud.data.reclaim.start_time_s,
			msg->module.cloud.data.reclaim.end_time_s, false);
		if (ret != 0) {
			LOG_ERR("Reclaim failed, %d", err);
			err = true;
		}

		if (!err) {
			data_codec_update_reclaim_state(&codec,
							RECLAIM_IN_PROGRESS);
			reclaim_active = true;
			data_encode_for_cloud(false);
		} else {
			data_codec_update_reclaim_state(&codec,
							RECLAIM_ERROR);
			reclaim_active = false;
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_TEST_START)) {
		bool device_id_is_default = etc_device_id_is_default();
		functional_test_start(functional_test_event_handler);
		SEND_EVENT(data, DATA_EVT_FUNCTIONAL_TEST_START);

		track_functional_test(DATA_TYPE_SENSOR, (void *)msg->module.sensor.data.sensors);
		track_functional_test(DATA_TYPE_DEVICE_ID_DEFAULT, (void *)&device_id_is_default);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_TEST_END)) {
		stop_functional_test();
	}

	if (IS_EVENT(msg, ble, BLE_EVT_RECLAIM_REQUEST)) {
		int ret;
		bool err = false;
		
		ret = etc_device_record_reclaim(
			msg->module.ble.data.reclaim.start_time_s,
			msg->module.ble.data.reclaim.end_time_s, false);
		if (ret != 0) {
			LOG_ERR("Reclaim failed, %d", err);
			err = true;
			etc_ble_notify_error(ETC_BLE_ERR_QUERY_TYPE, err);
		}

		if (!err) {
			int num_records = etc_device_record_num_reclaim_records();
			if (num_records > 0) {
				reclaim_active = true;
				data_encode_for_ble();
			}
			etc_ble_notify_reclaim_status(num_records);
		}
		return;
	}

	if (IS_EVENT(msg, ble, BLE_EVT_QUERY_RECLAIM)) {
		int ret;
		bool err = false;
		
		ret = etc_device_record_reclaim(
			msg->module.ble.data.reclaim.start_time_s,
			msg->module.ble.data.reclaim.end_time_s, true);
		if (ret < 0) {
			LOG_ERR("Reclaim failed, %d", err);
			etc_ble_notify_error(ETC_BLE_ERR_QUERY_TYPE, err);
			err = true;
		} else {
			etc_ble_notify_query_reclaim(ret);
		}
		return;
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) && 
		etc_get_device_mode() == ETC_DEVICE_MODE_BLE) {
		data_encode_for_ble();
		return;
	}

	if (IS_EVENT(msg, ble, BLE_EVT_CONN_READY)) {
		data_encode_for_ble();
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
APP_EVENT_SUBSCRIBE_EARLY(MODULE, ble_event);
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