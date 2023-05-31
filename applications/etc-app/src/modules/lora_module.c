#include <zephyr/kernel.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/random/rand32.h>
#include "etc_date_time.h"
#include "etc_device.h"
#include "etc_settings.h"
#include "app_version.h"
#include "data/etc_cape.h"
#include "cloud/cloud_codec/data_codec.h"
#include "common.h"
#define MODULE lora_module

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/lora_event.h"
#include "events/util_event.h"
#include "events/cloud_event.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define LORA_ACKUNCRYPT_LEN	128
#define LORA_ACKCRYPT_LEN	128
#define LORA_RETRY_MAX_TIME	5
#define LORA_SYNC_TIME_DIFF_SEC 30
#define LORA_LOGGER_ON_RECV_MODE_MSEC 1500
#define LORA_LOGGER_ID_LEN	(sizeof("FFFFFFFFFFFFFFFF"))
struct lora_msg_data {
	union {
		struct app_event app;
		struct data_event data;
		struct util_event util;
		struct cloud_event cloud;
	} module;
};

/* Lora module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

static enum sub_state_type {
	SUB_STATE_TRANSMIT_MODE,
	SUB_STATE_RECEIVE_MODE,
} sub_state;

struct logger_lora_response {
	bool is_okay;
	char logger_id[LORA_LOGGER_ID_LEN];
	uint16_t tx_interval_in_mins;
	int relay_id;
	int current_time;
	int reclaim_start_time;
	int reclaim_end_time;
};

/* Lora module message queue. */
#define LORA_QUEUE_ENTRY_COUNT	  10
#define LORA_QUEUE_BYTE_ALIGNMENT 4

K_MSGQ_DEFINE(msgq_lora, sizeof(struct lora_msg_data), LORA_QUEUE_ENTRY_COUNT,
	      LORA_QUEUE_BYTE_ALIGNMENT);

static void rx_thread_fn(void);
static K_KERNEL_STACK_DEFINE(lora_rx_stack, 1024);

static char decoded_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static char buf_tmp[ETC_SETTINGS_DEVICE_ID_LEN];
static char encoded_buffer[LORA_ACKCRYPT_LEN] = {0};
static uint8_t lora_rx_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static int lora_parent_id = 0x00;
static uint8_t lora_pkt_counter = 0;
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};
static struct lora_modem_config etc_lora_tx_config = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = true,
};

const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
static struct k_thread lora_rx_thread;
static struct module_data self = {
	.name = "lora",
	.msg_q = &msgq_lora,
	.supports_shutdown = true,
};

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_TRANSMIT_MODE:
		return "SUB_STATE_TRANSMIT_MODE";
	case SUB_STATE_RECEIVE_MODE:
		return "SUB_STATE_RECEIVE_MODE";
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

	LOG_DBG("State transition %s --> %s", state2str(state), state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s", sub_state2str(sub_state),
		sub_state2str(new_state));

	sub_state = new_state;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct lora_msg_data msg = {0};
	bool enqueue_msg = false;

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

	if (is_cloud_event(aeh)) {
		struct cloud_event *event = cast_cloud_event(aeh);

		msg.module.cloud = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(lora, LORA_EVT_ERROR, err);
		}
	}

	return false;
}

static void lora_module_on_stop(void)
{
}

static void lora_module_on_start(void)
{
}

static int setup(void)
{
	if (!device_is_ready(lora_dev)) {
		return -1;
	}

	if (etc_device_get_mode() == ETC_DEVICE_MODE_RELAY) {
		LOG_INF("Start a thread for Relay LORA");
		k_thread_create(&lora_rx_thread, lora_rx_stack,
				K_KERNEL_STACK_SIZEOF(lora_rx_stack),
				(k_thread_entry_t)rx_thread_fn, NULL, NULL, NULL, K_PRIO_COOP(7), 0,
				K_NO_WAIT);
	} else {
		LOG_INF("Logger device mode for Lora");
	}

	return 0;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		state_set(STATE_RUNNING);
		sub_state_set(SUB_STATE_RECEIVE_MODE);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct lora_msg_data *msg)
{
}

static int module_lora_transmit_packet(const uint8_t *decoded_buf, int buf_len)
{
	int ret = lora_config(lora_dev, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
		return -EINVAL;
	}

	ret = lora_send(lora_dev, (uint8_t *)decoded_buf, buf_len);
	if (ret < 0) {
		LOG_ERR("LoRa send failed");
		return -EINVAL;
	}

	return 0;
}

static struct logger_lora_response lora_module_get_sync_data(char *package)
{
	struct logger_lora_response response;
	uint8_t i;
	char *pt;
	char *ptr;
	response.is_okay = false;
	pt = strtok(package, ",");
	if (pt != NULL) { // break down ACK string into parts.
		for (i = 0; i < 6; i++) {
			if (pt == NULL) {
				response.is_okay = false;
				return response;
			}
			if (i == 0) {
				snprintf(response.logger_id, sizeof(response.logger_id), "%s", pt);
			} else if (i == 1) {
				response.tx_interval_in_mins = strtoul(pt, &ptr, 10);
			} else if (i == 2) {
				response.relay_id = strtoul(pt, &ptr, 10);
			} else if (i == 3) {
				response.current_time = strtoul(pt, &ptr, 10);
			} else if (i == 4) {
				response.reclaim_start_time = strtoul(pt, &ptr, 10);
			} else if (i == 5) {
				response.reclaim_end_time = strtoul(pt, &ptr, 10);
				response.is_okay = true;
			}
			pt = strtok(NULL, ",");
		}
	}
	return response;
}

static int module_lora_wait_packet(void)
{
	int ret = 0;
	int16_t rssi;
	int8_t snr;
	memset(lora_rx_buf, 0, sizeof(lora_rx_buf));
	memset(decoded_buf, 0, sizeof(decoded_buf));
	ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
		return -EINVAL;
	}
	ret = lora_recv(lora_dev, lora_rx_buf, sizeof(lora_rx_buf),
			K_MSEC(LORA_LOGGER_ON_RECV_MODE_MSEC), &rssi, &snr);
	if (ret < 0) {
		LOG_DBG("Timeout");
		return -ETIMEDOUT;
	} else {
		etc_cape_decrypt((char *)lora_rx_buf, decoded_buf, ret);
		LOG_DBG("Decoded buf %s", decoded_buf);
		etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);
		struct logger_lora_response response = lora_module_get_sync_data(decoded_buf);
		if (response.is_okay) {
			/* Compare the logger_id from ACK and current logger ID */
			if (strncmp(buf_tmp, response.logger_id, strlen(buf_tmp)) != 0) {
				return -1;
			}

			uint32_t my_time = 0;
			date_time_utc_second(&my_time);
			lora_parent_id = response.relay_id;
			LOG_INF("Sync time %d %d %d", lora_parent_id, my_time,
				response.current_time);
			if ((response.current_time != 0) &&
			    (abs(response.current_time - my_time) >= LORA_SYNC_TIME_DIFF_SEC)) {
				LOG_DBG("Need to sync time");
				date_time_set_second(response.current_time);
			}
			if (response.reclaim_start_time == 0 || response.reclaim_end_time == 0) {
				LOG_DBG("Receive the ACK message from the replay %d at %d",
					response.relay_id, response.current_time);
			} else {
				LOG_DBG("Receive the RECLAIM message from the replay %d from %d to "
					"%d",
					response.relay_id, response.reclaim_start_time,
					response.reclaim_end_time);
				etc_device_reclaim_record(response.reclaim_start_time,
							  response.reclaim_end_time);
			}
			return 0;
		}
	}

	return -EINVAL;
}
#if 0
char decr_buf[LORA_ACKUNCRYPT_LEN + 1];
#endif

static int module_lora_process_packet(union etc_device_record record)
{
	LOG_HEXDUMP_DBG((uint8_t *)&record, sizeof(record), "RECORD");
	etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);
	int decoded_buf_len = 0;

	if (lora_parent_id == 0) {
		decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf),
					    "S,OPEN,%s,%s,%1.2f,%d,%d,", APP_VERSION_STR, buf_tmp,
					    record.battery, lora_pkt_counter, record.timestamp);
	} else {
		decoded_buf_len +=
			snprintf(decoded_buf, sizeof(decoded_buf), "S,%04d,%s,%s,%1.2f,%d,%d,",
				 lora_parent_id, APP_VERSION_STR, buf_tmp, record.battery,
				 lora_pkt_counter, record.timestamp);
	}

	lora_pkt_counter += 1;
	if (lora_pkt_counter >= LOGGER_MAXIMUM_COUNTER) {
		lora_pkt_counter = 0;
	}

	for (int i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		if (data_codec_compare_temperature_is_valid(record.sensor[i])) {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
						    record.sensor[i]);
		} else {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, "*,");
		}
	}
	decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
				    sizeof(decoded_buf) - decoded_buf_len, "*,");
	LOG_DBG("Decoded length %d", decoded_buf_len);
	LOG_DBG("Msg %s", decoded_buf);
	etc_cape_encrypt(decoded_buf, encoded_buffer, decoded_buf_len, 21);
	LOG_HEXDUMP_INF(encoded_buffer, decoded_buf_len, "ENCRYPTED");
#if 0 // Test decrypt the message encoded
	etc_cape_decrypt(encoded_buffer, decr_buf, decoded_buf_len + 1);
	LOG_HEXDUMP_INF(decr_buf, sizeof(decr_buf), "DECRYPTED");
#endif
	int rc = 0;
	uint8_t cnt = 0;
retry:
	if (cnt != 0) {
		/* Generate new TX_DELAY */
		uint16_t new_tx_delay_msec =
			(uint16_t)(sys_rand32_get() % ETC_SETTING_TX_DELAY_MSEC_MAX);
		etc_set_tx_delay_msec(new_tx_delay_msec);
		k_msleep(new_tx_delay_msec);
	} else {
		/* On first try, reload the tx delay and sleep the remaining ms that
		 * are not accounted for by the RTC (only has seconds resolution) */
		uint16_t tx_delay_remain = etc_get_tx_delay_msec() % 1000;
		k_msleep(tx_delay_remain);
	}

	rc = module_lora_transmit_packet(encoded_buffer, decoded_buf_len + 1);
	if (rc == 0) {
		rc = module_lora_wait_packet();
		if (rc == 0) {
			return 0;
		} else {
			if (cnt++ >= LORA_RETRY_MAX_TIME) {
				return rc;
			}
			k_sleep(K_SECONDS(1));
			goto retry;
		}
	} else {
		LOG_ERR("Failed to transmit packet");
		return rc;
	}

	return -EINVAL;
}

/* Message handler for all states. */
static void on_all_states(struct lora_msg_data *msg)
{
	if (etc_device_is_logger_lora()) {
		if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT)) {
			LOG_INF("Logger sending data");
			int rc = 0;
			do {
				union etc_device_record record;
				uint16_t record_id = etc_device_read_record(&record);
				if (record_id > 0) {
					LOG_INF("Sending data over LORA");
					rc = module_lora_process_packet(record);
					if (rc == 0) {
						etc_device_set_ack_record(record_id);
					} else {
						LOG_ERR("Timeout in waiting ACK");
						break;
					}
				} else if (rc < 0) {
					LOG_ERR("Error in reading record");
					break;
				} else {
					LOG_INF("No NACK record");
					break;
				}
			} while (1);
			SEND_EVENT(lora, LORA_EVT_RX_DATA_READY);
		}
	}
}

/* Message handler for SUB_STATE_TRANSMIT_MODE. */
static void on_sub_state_transmit(struct lora_msg_data *msg)
{
}

/* Message handler for SUB_STATE_RECEIVE_MODE. */
static void on_sub_state_receive(struct lora_msg_data *msg)
{
}

static void lora_data_send(const char *msg, int msg_len)
{
	struct lora_event *lora_module_event = new_lora_event();

	memcpy(lora_module_event->data.sensor_msg, msg, msg_len);
	lora_module_event->data.sensor_msg[msg_len] = '\0';
	lora_module_event->data.timestamp = date_time_now_second();
	lora_module_event->type = LORA_EVT_RX_DATA_READY;

	APP_EVENT_SUBMIT(lora_module_event);
}

static void lora_module_report_data(const uint8_t *package)
{
	char *token = strchr((const char *)package, ',');
	int count = 0;
	while (token != NULL) {
		count += 1;
		if (count == 7) {
			LOG_DBG("Token %s", token);
			token += 1;
			lora_data_send(token, strlen(token));
		};
		token = strchr(token + 1, ',');
	}
}

static void rx_thread_fn(void)
{
	uint32_t t0 = k_uptime_get_32();
	int ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
	}
	ret = 0;
	uint8_t rx_buf[128] = {0x00};
	int16_t rssi;
	int8_t snr;
	while (1) {
		ret = lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_FOREVER, &rssi, &snr);
		if (ret < 0) {
			continue;
		} else {
			etc_cape_decrypt((char *)rx_buf, decoded_buf, ret);
			if (decoded_buf[0] == 'S') {
				LOG_INF("Received data: RSSI:%ddBm, SNR:%ddBm", rssi, snr);
				LOG_INF("%.*s", LORA_ACKUNCRYPT_LEN, decoded_buf);
				lora_module_report_data((const uint8_t *)decoded_buf);
			}
			memset(rx_buf, 0, sizeof(rx_buf));
		}
		k_sleep(K_MSEC(500));
	}
}

void lora_module_thread_fn(void)
{
	int err;
	struct lora_msg_data msg = {0};

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(lora, LORA_EVT_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(lora, LORA_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			switch (sub_state) {
			case SUB_STATE_TRANSMIT_MODE:
				on_sub_state_transmit(&msg);
				break;
			case SUB_STATE_RECEIVE_MODE:
				on_sub_state_receive(&msg);
				break;
			default:
				LOG_WRN("Unknown application sub state");
				break;
			}
			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown lora module state.");
			break;
		}

		on_all_states(&msg);
	}
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);