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
#include "cloud/cloud_codec/data_codec_signal.h"
#include "etc_date_time.h"
#include "etc_settings.h"
#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_ble.h"
#include "etc_battery.h"
#include "etc_calibration.h"
#include "cloud/lwm2m/lwm2m_firmware.h"
#include "cloud/cloud_wrapper.h"

#define MODULE data_module

#include "etc_memfault.h"
#include "etc_functional_test.h"

#include "modules_common.h"
#include "data_send_state.h"
#include "threshold_alert_state.h"
#include "threshold_eval.h"
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

/* Oldest signal measurement still worth reporting to the cloud. */
#define DATA_SIGNAL_MAX_AGE_MS (6 * 60 * 60 * (int64_t)MSEC_PER_SEC)

/* Maximum consecutive immediate retries after a failed cloud send; further
 * retries wait for the next send trigger (transmit interval, reconnect, ACK).
 */
#define DATA_SEND_FAIL_RETRY_MAX 10

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

/* Data module super states. */
static enum state_type {
	STATE_CLOUD_DISCONNECTED,
	STATE_CLOUD_CONNECTED,
	STATE_SHUTDOWN
} state;

/* Relay send type */
static enum state_relay_send_type {
	STATE_RELAY_SEND_META_MODEL,
	STATE_RELAY_SEND_SENSOR,
	STATE_RELAY_SEND_RECORD_READY,
	STATE_RELAY_SEND_RECORD_DONE
} state_relay_send = STATE_RELAY_SEND_SENSOR;

/* Data cloud encode status */
enum status_data_cloud_process {
	STATUS_DONE,
	STATUS_IN_PROCESS
};

static struct etc_gnss_data gnss_data = { 0 };

static struct data_modem_static modem_stat;
static struct data_modem_dynamic modem_dynamic;

static bool first_send = true;
static bool need_interval_tx_send = true;
/* One-shot: flag the next record sent as a priority reading so the Portal
 * processes it immediately. Initialised true so the first reading after boot is
 * priority; set again whenever a magnet/button (user-triggered) reading is
 * taken. Consumed by data_encode_for_logger() on the first record actually sent.
 */
static bool priority_reading_pending = true;
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

/* Edge state of the immediate report thresholds, carried between samples. */
static struct threshold_eval threshold_eval_state;
/* Alert flags of the threshold slots awaiting delivery to the cloud. */
static struct threshold_alert_state threshold_alerts;

/* One send in flight per channel; a BLE ACK must never settle a cloud send. */
static struct data_send_state send_status_cloud;
static struct data_send_state send_status_ble;

/* Consecutive failed cloud sends; bounds the immediate-retry loop. */
static uint32_t send_fail_count;
/* Consecutive failed BLE sends; bounds the retry of the current record. */
static uint32_t ble_send_fail_count;

/* Initialize publish timeout for data publish as forever */
static k_timeout_t data_publish_timeout = K_FOREVER; 

static K_SEM_DEFINE(config_load_sem, 0, 1);

static struct k_work_delayable data_send_work;

/* Save the current/previous reclaim state, so we can change the LwM2M reclaim
 * status accordingly.
 */
static bool reclaim_active;

/* Set when a reclaim cancel has been requested. Consumed by data_encode_for_logger()
 * so the CANCELLED status is emitted as part of building a send — like the other
 * reclaim status transitions — rather than added out-of-band where a concurrent
 * send's ACK would clear it from the codec before it is transmitted. */
static bool reclaim_cancel_pending;

/* Set when a reclaim is requested over a period with no records. Consumed by
 * data_encode_for_logger() so the NO_RECORDS status is emitted (and sent) the
 * same way as the other reclaim status transitions — see reclaim_cancel_pending. */
static bool reclaim_no_records_pending;

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
	int rc = etc_device_verify_to_set_psm(new_config);
	if (rc == 1) {
		SEND_EVENT(data, DATA_EVT_CONFIG_EXIT_ALWAYS_ON_MODE);
	} else if (rc == 2) {
		SEND_EVENT(data, DATA_EVT_CONFIG_ENTER_ALWAYS_ON_MODE);
	}
	etc_settings_update(new_config);
}

static void cloud_codec_event_handler(const struct cloud_codec_evt *evt)
{
	if (evt->type == CLOUD_CODEC_EVT_CONFIG_UPDATE) {
		new_config_handle(&evt->config_update);
	} else if (evt->type == CLOUD_CODEC_EVT_CONFIG_SYNC) {
		SEND_EVENT(data, DATA_EVT_CONFIG_SYNC);
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

	threshold_eval_init(&threshold_eval_state);

	etc_settings_get_config(&cfg);
	
	err = data_codec_init(&cfg, cloud_codec_event_handler);
	if (err) {
		LOG_ERR("cloud_codec_init, error: %d", err);
		return err;
	}

	err = etc_device_retrieve_location(&gnss_data);
	if (err) {
		LOG_WRN("Could not retrieve location");
		return 0;
	}
	data_codec_update_location(&codec, &gnss_data);

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

static void data_module_send_calibration_status(enum data_event_type type)
{
	struct data_event *data_event = new_data_event();
	data_event->type = type;
	data_event->data.calibration_result = etc_calibration_get_calibration_result();
	APP_EVENT_SUBMIT(data_event);
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

	data_send_state_begin(&send_status_cloud);

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

	data_send_state_begin(&send_status_ble);
	/* Update data buffer */
	module_event->data.buffer.buf = buf;
	module_event->data.buffer.buf_len = buf_len;
	APP_EVENT_SUBMIT(module_event);
}
#endif
static void data_encode_prepare_modem_info(struct data_modem_dynamic *modem_info)
{
	struct modem_signal_sample sample;
	int err = quectel_bg95_get_signal(&sample);

	modem_info->signal_valid =
		data_codec_signal_is_reportable(err, &sample, DATA_SIGNAL_MAX_AGE_MS);
	if (modem_info->signal_valid) {
		modem_info->rsrp = sample.rsrp;
		modem_info->qual = sample.rsrq;
	}
	modem_info->queued = 1;

	LOG_INF("Signal report rsrp=%d rsrq=%d age_s=%lld valid=%d", modem_info->rsrp,
		modem_info->qual, err ? -1LL : sample.age_ms / MSEC_PER_SEC,
		modem_info->signal_valid);
}

/*
 * @brief To send heart-beat/meta-data first. 
 * If no meta-data, send relay data or next state is RECORD_READY
 */
static int data_encode_for_relay() {
	int ret = 0;
	if (state_relay_send == STATE_RELAY_SEND_META_MODEL || 
	    state_relay_send == STATE_RELAY_SEND_SENSOR) {
		LOG_DBG("Sending meta data for Relay");
		union etc_device_record record_sensor;
		ret = etc_device_relay_read_record_sensor(&record_sensor);
		if (ret == 0) {
			data_encode_prepare_modem_info(&modem_dynamic);
			ret = data_codec_prepare_cloud_packet(&codec, &record_sensor,
							      &modem_dynamic);
			if (ret != 0) {
				LOG_WRN("Error populating data codec");
				return ret;
			}
			return STATUS_IN_PROCESS;
		}
		if (state_relay_send == STATE_RELAY_SEND_META_MODEL) {
			return STATUS_IN_PROCESS;
		} 
	}
	/* If no meta-data or record sensor, send Relay queued data */
	LOG_DBG("Sending relay data for Relay");
	struct etc_device_relay_packet packet = {0x00};
	ret = etc_device_read_relay_data_packet(&packet);
	if (!ret) {
		int data_len = 0;
		ret = etc_common_prepare_relay_legacy_packet(&packet, data_relay_buf, &data_len,
							     sizeof(data_relay_buf));
		if (!ret) {
			/* The encoder may have packed fewer records than were peeked if
			 * they did not all fit; commit only the records actually forwarded
			 * so the remainder ships on the next interval. */
			etc_device_relay_set_last_read_count(packet.num_records);
			LOG_DBG("Relay message %s", data_relay_buf);
			ret = data_codec_prepare_relay_packet(&codec, data_relay_buf, data_len,
							      true);
			if (ret) {
				LOG_WRN("Error populating data codec");
				return ret;
			}
			state_relay_send = STATE_RELAY_SEND_RECORD_DONE;
		} else {
			LOG_ERR("Can't prepare packet for relay");
			return ret;
		}
		return STATUS_IN_PROCESS;
	}

	/* Reset the send meta for next interval */
	LOG_DBG("Finished sending");
	state_relay_send = STATE_RELAY_SEND_SENSOR;
	return STATUS_DONE;
}

static int data_encode_for_logger() {
	int ret = 0;
	union etc_device_record record;
	bool reclaim_status;	
	data_encode_prepare_modem_info(&modem_dynamic);
	send_status_cloud.record_id = etc_device_read_record(&record, &reclaim_status);
	/* Only add a record if it is valid. */
	if (send_status_cloud.record_id > 0) {
		ret = data_codec_prepare_cloud_packet(&codec, &record, &modem_dynamic);
		if (ret != 0) {
			LOG_WRN("Error populating data codec");
		} else {
			/* Records are served newest first, so this send carries the
			 * breaching sample.
			 */
			uint8_t alerts = threshold_alert_take_for_send(&threshold_alerts);

			if (alerts != 0 && data_codec_add_threshold_alerts(&codec, alerts) != 0) {
				/* Back to pending so the next send carries them. */
				threshold_alert_send_failed(&threshold_alerts);
				alerts = 0;
			}

			/* Priority makes the Portal process the reading immediately.
			 * One-shot for the magnet / first reading; derived from the
			 * alert mask for a threshold report, so a re-sent alert is
			 * flagged again after the ack cleared the resource.
			 */
			if ((priority_reading_pending || alerts != 0) &&
			    data_codec_add_priority(&codec) == 0) {
				priority_reading_pending = false;
			}
		}
	}

	/* A cancel takes precedence over the natural reclaim status transition
	 * (which would otherwise report SUCCESS once the reclaim is no longer
	 * active). Emit CANCELLED here so it is carried by this send. */
	if (reclaim_cancel_pending) {
		data_codec_update_reclaim_state(&codec, RECLAIM_CANCELLED);
		reclaim_active = false;
		reclaim_cancel_pending = false;
		return STATUS_IN_PROCESS;
	}

	/* A reclaim over an empty period emits NO_RECORDS here for the same
	 * reason as the cancel case above. */
	if (reclaim_no_records_pending) {
		data_codec_update_reclaim_state(&codec, RECLAIM_NO_RECORDS);
		reclaim_active = false;
		reclaim_no_records_pending = false;
		return STATUS_IN_PROCESS;
	}

	/* Update reclaim status */
	if (reclaim_status != reclaim_active) {
		if (reclaim_status) {
			data_codec_update_reclaim_state(&codec, RECLAIM_IN_PROGRESS);
			reclaim_active = true;
		} else {
			data_codec_update_reclaim_state(&codec, RECLAIM_SUCCESS);
			reclaim_active = false;
		}
	} else if (send_status_cloud.record_id == 0) {
		return STATUS_DONE;
	}
	return STATUS_IN_PROCESS;
}

/**
 * Encode the current LwM2M data to be sent in a message.
 * 
 * @param split If true, split new record for transmission, as it might
 * 		be too large to fit into one message.
 * @param is_relay If true, the device is relay and sending relay msg. 
*/
static void data_encode_for_cloud(bool split, bool is_relay) 
{
	int ret;

	if (send_status_cloud.active) {
		LOG_WRN("Not sending new record."
			"Record ID %u is already being sent.",
			send_status_cloud.record_id);
		return;
	}

	if (first_send) {
		state_relay_send = STATE_RELAY_SEND_META_MODEL;
		data_codec_prepare_update_packet(&codec);
		first_send = false;
	}

	if (need_interval_tx_send) {
		data_codec_prepare_next_tx_transmit_info(&codec);
		need_interval_tx_send = false;
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
		if (is_relay) {
			ret = data_encode_for_relay();
		} else {
			ret = data_encode_for_logger();
		}
		if (ret == STATUS_DONE && !split) {
			LOG_INF("No record found");
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

	if (send_status_ble.active) {
		LOG_WRN("Not sending new record over BLE. "
			"Record ID %d is already being sent.",
			send_status_ble.record_id);
		return;
	}

	send_status_ble.record_id = etc_device_read_record(&record, &reclaim_status);
	/* Only add a record if it is valid. */
	if (send_status_ble.record_id > 0) {
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
			LOG_WRN("No message to send over BLE (err %d)", ret);
			data_send_state_finish(&send_status_ble);
			if (reclaim_active) {
				etc_ble_notify_error(ETC_BLE_ERR_RECLAIM_TYPE, ret);
				reclaim_active = false;
			} else {
				etc_ble_notify_error(ETC_BLE_ERR_RETRIEVE_TYPE, ret);
			}
			return;
		}
	}

	/* Update reclaim status */
	if (send_status_ble.record_id == 0) {
		LOG_INF("No record found");
		if (reclaim_active) {
			etc_ble_notify_reclaim_status(0);
			reclaim_active = false;
		} else {
			etc_ble_notify_status(ETC_BLE_ERR_RETRIEVE_TYPE, 0);
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

static void data_do_and_send_calibration(void)
{
	int calib_status = etc_calibration_get_calibration_status();
	if (calib_status == ETC_SENSOR_CALIB_IDLE) {
		return;
	}

	if (calib_status == ETC_SENSOR_CALIB_DATA_UPLOAD) {
		/* A completed calibration result is still awaiting its upload
		 * acknowledgement (e.g. the original send was never ACKed before a
		 * disconnect). Re-encode and re-send the stored result without
		 * re-measuring; the status is cleared on ACK. */
		data_codec_update_calibration(&codec);
		data_codec_update_calibration_status(&codec);
		data_send(DATA_EVT_DATA_SEND, &codec);
		return;
	}

	/* Fresh calibration request: run the measurement, then power the hardware
	 * down without releasing the front-end lock in between. The status stays at
	 * DATA_UPLOAD so the result can be re-sent until the cloud acknowledges it;
	 * etc_calibration_set_idle() is called from the data-send ACK handler. */
	/* Keep LwM2M running! */
	lwm2m_rd_client_update();
	etc_calibration_run_and_teardown();
	if ((etc_calibration_get_calibration_status()) == ETC_SENSOR_CALIB_DATA_UPLOAD) {
		if ((etc_calibration_get_calibration_result() != ETC_SENSOR_CALIB_SUCCESS)) {
			data_module_send_calibration_status(DATA_EVT_CALIBRATION_ERROR);
		}
		data_codec_update_calibration(&codec);
		data_codec_update_calibration_status(&codec);
	}
	data_send(DATA_EVT_DATA_SEND, &codec);
}

static void data_do_check_calibration(void)
{
	int status = etc_calibration_get_calibration_status();

	/* Abort a session that was armed by a magnet swipe but never reached its
	 * upload. DATA_UPLOAD is deliberately left alone: that result is complete
	 * and must survive a disconnect so it can be re-sent on reconnect (FW-611).
	 */
	if (status > ETC_SENSOR_CALIB_IDLE && status < ETC_SENSOR_CALIB_DATA_UPLOAD) {
		data_module_send_calibration_status(DATA_EVT_CALIBRATION_ERROR);
		etc_calibration_exit();
	}
}

/**
 * @brief Evaluate a sample against the immediate report thresholds.
 *
 * Flags the crossing slots and requests an immediate upload when the rate limit
 * allows one.
 *
 * @param sensors Sample just written to the record store.
 */
static void evaluate_thresholds(const struct sensor_data *sensors)
{
	struct etc_threshold cfg[ETC_THRESHOLD_SLOT_COUNT];
	struct threshold_eval_result res;
	enum etc_device_mode mode = etc_get_device_mode();
	uint8_t changed;

	/* Thresholds are a logger feature. */
	if ((mode != ETC_DEVICE_MODE_LTE_LOGGER) && (mode != ETC_DEVICE_MODE_LORA_LOGGER)) {
		return;
	}

	changed = etc_take_thresholds(cfg);
	threshold_eval_update(&threshold_eval_state, cfg, changed, sensors, k_uptime_get(), &res);

	if (res.crossed != 0) {
		data_codec_note_threshold_trigger(res.crossed);
		/* The v1 LoRa packet has no field for the alert flag, and a flag left
		 * standing would ride the daily LTE sync hours later.
		 */
		if (mode == ETC_DEVICE_MODE_LTE_LOGGER) {
			threshold_alert_set(&threshold_alerts, res.crossed);
		}
		LOG_INF("Threshold crossed: slots 0x%02x upload %u", res.crossed,
			res.request_upload);
	}

	if (res.request_upload) {
		SEND_EVENT(data, DATA_EVT_THRESHOLD_TRIGGERED);
	}
}

static void save_new_sensor_data(struct sensor_data *sensors)
{
	if (etc_device_is_relay()) {
		etc_device_relay_write_record_sensor(sensors);
	} else {
		etc_device_write_record_sensor(sensors);
		uint8_t bat_percent = etc_battery_percentage_from_voltage(sensors->battery_mV);
		etc_ble_notify_battery(bat_percent);
		evaluate_thresholds(sensors);
	}
}

/** @brief Abandon the cloud send in flight; its alert flags return to pending. */
static void data_cloud_send_abort(void)
{
	data_send_state_finish(&send_status_cloud);
	threshold_alert_send_failed(&threshold_alerts);
}

static void data_send_work_fn(struct k_work *work)
{
	k_work_reschedule(&data_send_work, data_publish_timeout);
}

/* Message handler for STATE_CLOUD_DISCONNECTED. */
static void on_cloud_state_disconnected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		send_fail_count = 0;
		state_set(STATE_CLOUD_CONNECTED);

		/* Retry to calibration check */
		data_do_and_send_calibration();

		if (functional_test_get_state() == FUNC_TEST_STATE_COLLECTING_DATA) {
			functional_test_schedule_send();
		} else if ((etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) || 
			   (etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_MAGNET) ||
			   ((etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) && 
			   (etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_CLOUD_LORA))) {
			need_interval_tx_send = true;
			data_encode_for_cloud(false, etc_device_is_relay());
		} else if (etc_device_is_relay()) {
			data_send_state_finish(&send_status_cloud);
			need_interval_tx_send = true;
			data_encode_for_cloud(false, true);
		}
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED_READY)) {
		if ((etc_calibration_get_calibration_status() != ETC_SENSOR_CALIB_IDLE)) {
			/* Request cloud sync */
			SEND_EVENT(data, DATA_EVT_REQUEST_CALIBRATION);
		}
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_cloud_state_connected(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) && 
	    etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) {
		need_interval_tx_send = true;
		data_encode_for_cloud(false, false);
		return;
	}

	/* Send relay heartbeat when new data is ready
	 */
	if (IS_EVENT(msg, data, DATA_EVT_DATA_READY) && etc_device_is_relay()) {
		data_encode_for_cloud(false, true);
		return;
	}

	if ((IS_EVENT(msg, app, APP_EVT_DATA_SYNC_CLOUD) && 
		(((etc_get_device_mode() == ETC_DEVICE_MODE_LORA_LOGGER) && 
		(etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_CLOUD_LORA)) || 
		(etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) || 
		(etc_device_get_transmit_sub_job() == ETC_TRANSMIT_SYNC_MAGNET)))) {
		need_interval_tx_send = true;
		data_encode_for_cloud(false, etc_device_is_relay());
		return;
	}

	if (IS_EVENT(msg, app, APP_EVT_CONFIG_GET)) {
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTING)) {
		/* Reset send status to allow future sends. */
		data_do_check_calibration();
		data_cloud_send_abort();
		send_fail_count = 0;
		state_set(STATE_CLOUD_DISCONNECTED);
		return;
	}

	if (IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_START) ||
	    IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_SEND_DATA)) {
		struct modem_signal_sample sample;
		int err = quectel_bg95_get_signal(&sample);

		/* Leave the test's own invalid marker in place when there is no
		 * measurement worth reporting.
		 */
		if (data_codec_signal_is_reportable(err, &sample, DATA_SIGNAL_MAX_AGE_MS)) {
			int rsrp = sample.rsrp;

			track_functional_test(DATA_TYPE_MODEM, &rsrp);
		}
		functional_test_set_state(FUNC_TEST_STATE_SENDING_DATA);
		data_encode_for_cloud(false, false);
	}

	if (IS_EVENT(msg, app, APP_EVT_REQUEST_CALIBRATION)) {
		/* Run calibration when system already connected to cloud */
		data_do_and_send_calibration();
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED)) {
		data_do_check_calibration();
	}
}

/* Message handler for all states. */
static void on_all_states(struct data_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* Save the record stats to non-volatile memory before shutting down.
		 */
		etc_device_record_save();
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

		/* This event carries no signal data, so refresh it here rather
		 * than publishing whatever the last send left behind.
		 */
		data_encode_prepare_modem_info(&modem_dynamic);

		data_codec_prepare_modem_dynamic_packet(&codec, &modem_dynamic);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY)) {
		save_new_sensor_data(msg->module.sensor.data.sensors);
		SEND_EVENT(data, DATA_EVT_DATA_READY);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_USER_TRIGGERED_DATA_READY)) {
		save_new_sensor_data(msg->module.sensor.data.sensors);
		/* A magnet swipe / button press is a priority reading: flag the
		 * next record sent so the Portal processes it immediately.
		 */
		priority_reading_pending = true;
		int64_t time_now = k_uptime_get();
		/* Save current device record stat when a sample is triggered by a user.
		 * This ensures that data can be recovered properly after a magnet hard
		 * reset.
		 */
		etc_device_record_save();
		LOG_INF("Saving time: %lld", k_uptime_delta(&time_now));
		SEND_EVENT(data, DATA_EVT_DATA_READY);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY)) {
		etc_device_write_record_sensor(msg->module.sensor.data.sensors);
		SEND_EVENT(data, DATA_EVT_TEST_DATA_READY);
	}

	if (IS_EVENT(msg, data, DATA_EVT_RELAY_DATA_READY)) {
		return;
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
		if (functional_test_get_state() == FUNC_TEST_STATE_WAITING_FOR_ACK) {
			bool ack = true;
			track_functional_test(DATA_TYPE_ACK, (void *)&ack);
			stop_functional_test();
		}
		if (etc_calibration_get_calibration_status() == ETC_SENSOR_CALIB_DATA_UPLOAD) {
			/* A pending calibration result has now been delivered and
			 * acknowledged. Signal the UI success LED only on an actual pass
			 * (a failure already drove the fail LED via
			 * DATA_EVT_CALIBRATION_ERROR), then close the session so the
			 * result is no longer re-uploaded on reconnect. */
			if (etc_calibration_get_calibration_result() == ETC_SENSOR_CALIB_SUCCESS) {
				data_module_send_calibration_status(DATA_EVT_CALIBRATION_COMPLETE);
			}
			etc_calibration_set_idle();
		}
		int record_id = data_send_state_finish(&send_status_cloud);

		if (record_id > 0) {
			/* Acknowledge record and encode more data, if connected to cloud */
			etc_device_set_ack_record(record_id);
		}
		send_fail_count = 0;
		data_codec_clear_data(&codec);
		/* The flag (if any) was carried by the send just acknowledged; clear it
		 * so it is not reported on later whole-Info-object sends.
		 */
		data_codec_reset_priority();
		uint8_t cleared = threshold_alert_send_acked(&threshold_alerts);

		if (cleared != 0) {
			data_codec_clear_threshold_alerts(cleared);
		}
		if (state == STATE_CLOUD_CONNECTED) {
			if (etc_device_is_relay()) {
				if (state_relay_send == STATE_RELAY_SEND_META_MODEL ||
				    state_relay_send == STATE_RELAY_SEND_SENSOR) {
					state_relay_send = STATE_RELAY_SEND_RECORD_READY;
				} else if (state_relay_send == STATE_RELAY_SEND_RECORD_DONE) {
					etc_device_sync_relay_data();
					state_relay_send = STATE_RELAY_SEND_RECORD_READY;
				}
			}
			data_encode_for_cloud(false, etc_device_is_relay());
		}
	}

	if (IS_EVENT(msg, ble, BLE_EVT_DATA_SEND_ACK)) {
		int record_id = data_send_state_finish(&send_status_ble);

		LOG_DBG("Record ID %d", record_id);
		if (record_id > 0) {
			etc_device_set_ack_record(record_id);
		}
		ble_send_fail_count = 0;
		data_encode_for_ble();
	}

	if (IS_EVENT(msg, ble, BLE_EVT_DISCONNECTED)) {
		data_send_state_finish(&send_status_ble);
		ble_send_fail_count = 0;
	}

	if (IS_EVENT(msg, ble, BLE_EVT_DATA_SEND_FAIL)) {
		int err = msg->module.ble.data.err;

		data_send_state_finish(&send_status_ble);
		/* The record is still unacknowledged, so a retry resends it. */
		if (err != -ENOTCONN && ++ble_send_fail_count <= DATA_SEND_FAIL_RETRY_MAX) {
			data_encode_for_ble();
			return;
		}
		ble_send_fail_count = 0;
		if (reclaim_active) {
			etc_ble_notify_error(ETC_BLE_ERR_RECLAIM_TYPE, err);
			reclaim_active = false;
		}
		SEND_EVENT(data, DATA_EVT_SEND_COMPLETE);
	}
 
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_FAIL)) {
		bool split = false;
		if (msg->module.cloud.data.err == -ENOMEM ||
		    msg->module.cloud.data.err == -ECONNREFUSED ||
		    msg->module.cloud.data.err == -E2BIG) {
			split = true;
		}
		/* Reset send status on fail */
		data_cloud_send_abort();
		if (state == STATE_CLOUD_CONNECTED) {
			if (++send_fail_count > DATA_SEND_FAIL_RETRY_MAX) {
				LOG_WRN("Send failed %u times in a row, "
					"waiting for next send trigger",
					send_fail_count);
			} else {
				data_encode_for_cloud(split, etc_device_is_relay());
			}
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RX_OFF)) {
		data_cloud_send_abort();
	}
	
	if (IS_EVENT(msg, lora, LORA_EVT_RX_READY)) {
		if (state == STATE_CLOUD_CONNECTED) {
			need_interval_tx_send = true;
			data_encode_for_cloud(false, true);
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
			data_encode_for_cloud(false, false);
		} else {
			data_codec_update_reclaim_state(&codec,
							RECLAIM_ERROR);
			reclaim_active = false;
		}
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RECLAIM_CANCEL)) {
		int ret = etc_device_record_reclaim_cancel();
		if (ret != 0) {
			LOG_ERR("Reclaim cancel failed, %d", ret);
		}

		/* Emit the CANCELLED status from the send builder so it is not
		 * cleared by a concurrent send's ACK. If a send is already in
		 * flight, the pending flag is consumed on the next send. */
		reclaim_cancel_pending = true;
		data_encode_for_cloud(false, false);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_RECLAIM_NO_RECORDS)) {
		/* The Execute already failed on the call channel; emit the
		 * NO_RECORDS status from the send builder (see cancel above) so
		 * the device sends it rather than relying on a server read. */
		reclaim_no_records_pending = true;
		data_encode_for_cloud(false, false);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_LOCATION_REQUEST) ||
	    IS_EVENT(msg, app, APP_EVT_REQUEST_LOCATION)) {
		etc_device_set_location_request(ETC_GNSS_LOCATION_REQUESTED);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_TEST_START)) {
		bool device_id_is_default = etc_device_id_is_default();
		bool battery_connected = etc_battery_is_connected();
		functional_test_start(functional_test_event_handler);
		SEND_EVENT(data, DATA_EVT_FUNCTIONAL_TEST_START);

		track_functional_test(DATA_TYPE_SENSOR, (void *)msg->module.sensor.data.sensors);
		track_functional_test(DATA_TYPE_DEVICE_ID_DEFAULT, (void *)&device_id_is_default);
		track_functional_test(DATA_TYPE_BATTERY_CONNECTED, (void *)&battery_connected);
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

	if (IS_EVENT(msg, ble, BLE_EVT_QUERY_UNACK)) {
		etc_ble_notify_query_unack(etc_device_nack_count());
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

	if (IS_EVENT(msg, modem, MODEM_EVT_GNSS_ACQUIRED)) {
		int ret;
		uint32_t time_utc;
		gnss_data.latitude = msg->module.modem.data.gnss_data.latitude;
		gnss_data.longitude = msg->module.modem.data.gnss_data.longitude;
		date_time_utc_second(&time_utc);
		gnss_data.timestamp = time_utc;
		data_codec_update_location(&codec, &gnss_data);
		ret = etc_device_set_location(&gnss_data);
		if (ret) {
			LOG_WRN("Could not save location");
		}
	}

	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_SYNC)) {
		data_codec_prepare_config_packet(&codec);
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