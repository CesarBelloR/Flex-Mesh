#include <zephyr/kernel.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
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
#define LORA_RETRY_RECV_TIMEOUT_MS	1000
#define LORA_RETRY_MAX_TIME	5
#define LORA_SYNC_TIME_DIFF_SEC 30
#define LORA_LOGGER_ON_RECV_MODE_MSEC 1500
#define LORA_LOGGER_ID_LEN	ETC_DEVICE_LORA_LOGGER_ID_SIZE

struct lora_msg_data {
	union {
		struct app_event app;
		struct util_event util;
		struct cloud_event cloud;
	} module;
};

struct logger_lora_response {
	bool is_okay;
	char logger_id[LORA_LOGGER_ID_LEN];
	uint16_t tx_interval_in_mins;
	int relay_id;
	int current_time;
	int reclaim_start_time;
	int reclaim_end_time;
};

struct relay_lora_message {
	bool is_okay;
	struct etc_device_relay_record record;
};

enum lora_request_type {
	LORA_REQUEST_IN_IDLE,
	LORA_REQUEST_IN_RUN_RELAY,
	LORA_REQUEST_IN_RUN_LOGGER,	
};

/* Lora module message queue. */
#define LORA_QUEUE_ENTRY_COUNT	  36
#define LORA_QUEUE_BYTE_ALIGNMENT 4
#define LORA_REQUEST_QUEUE_ENTRY_COUNT 10
#define LORA_REQUEST_QUEUE_BYTE_ALIGNMENT 1

K_MSGQ_DEFINE(msgq_lora, sizeof(struct lora_msg_data), LORA_QUEUE_ENTRY_COUNT,
	      LORA_QUEUE_BYTE_ALIGNMENT);

K_SEM_DEFINE(lora_request_sem, 0, 1);
static enum lora_request_type lora_request;

static struct k_thread module_lora_rx_thread;
static k_tid_t module_lora_thread_id;
static void module_lora_rx_thread_fn(void);
static K_KERNEL_STACK_DEFINE(module_lora_rx_stack, CONFIG_ETC_LORA_MODULE_STACK_SIZE);

static char decoded_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static char buf_tmp[ETC_SETTINGS_DEVICE_ID_LEN];
static char encoded_buffer[LORA_ACKCRYPT_LEN] = {0};
static uint8_t lora_rx_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static int lora_parent_id = -1;
static uint8_t lora_pkt_counter = 0;
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = CONFIG_ETC_LORA_MODULE_RX_FREQUENCY,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};
static struct lora_modem_config etc_lora_tx_config = {
	.frequency = CONFIG_ETC_LORA_MODULE_TX_FREQUENCY,
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

		__ASSERT_NO_MSG(err == 0);
		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(lora, LORA_EVT_ERROR, err);
		}
	}

	return false;
}

static int setup(void)
{
	if (!device_is_ready(lora_dev)) {
		return -1;
	}

	module_lora_thread_id = k_thread_create(&module_lora_rx_thread, module_lora_rx_stack, 
						K_KERNEL_STACK_SIZEOF(module_lora_rx_stack),
						(k_thread_entry_t)module_lora_rx_thread_fn, NULL, NULL, NULL,
						K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

	return 0;
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
			switch (i) {
				case 0: 
					snprintf(response.logger_id, sizeof(response.logger_id), "%s", pt); 
					break;
				case 1: 
					response.tx_interval_in_mins = strtoul(pt, &ptr, 10); 
					break;
				case 2:	
					response.relay_id = strtoul(pt, &ptr, 10); 
					break;
				case 3: 
					response.current_time = strtoul(pt, &ptr, 10); 
					break;
				case 4: 
					response.reclaim_start_time = strtoul(pt, &ptr, 10); 
					break;
				case 5: 
					response.reclaim_end_time = strtoul(pt, &ptr, 10);
					response.is_okay = true;
					break;
				default: break;
			}
			pt = strtok(NULL, ",");
		}
	}
	return response;
}

static int lora_module_relay_get_message(char *package, int16_t rssi,
					 struct relay_lora_message *message)
{
	__ASSERT(package != NULL, "Empty input package");
	__ASSERT(message != NULL, "Empty message");
	uint8_t i = 0;
	char *pt;
	char *ptr;
	message->is_okay = false;

	/* Set extra element with -1 as initialization value */
	for (int i = 0; i < ETC_DEVICE_NUM_EXTRA_ELEMENT; i++) {
		message->record.data[i] = ETC_DEVICE_INVALID_VALUE_ELEMENT;
	}

	pt = strtok(package, ",");
	do {
		if (pt == NULL) {
			message->is_okay = false;
			return -EINVAL;
		}
		switch (i) {
		case 0:
			if (pt[0] != 'S') {
				message->is_okay = false;
				return -EINVAL;
			}
			break;
		case 1:
			snprintf(message->record.relay_id, sizeof(message->record.relay_id), "%s",
				 pt);
			break;
		case 2:
			snprintf(message->record.logger_ver, sizeof(message->record.logger_ver),
				 "%s", pt);
			break;
		case 3:
			snprintf(message->record.logger_id, sizeof(message->record.logger_id), "%s",
				 pt);
			message->is_okay = true;
			message->record.logger_rssi = rssi;
			break;
		case 4:
			message->record.battery = atof(pt);
			break;
		case 5:
			message->record.packet_number = atoi(pt);
			break;
		case 6:
			if (strstr(pt, "*") == NULL) {
				message->record.timestamp = atoi(pt);
				message->record.is_reclaim = true;
			} else {
				/* If timestamp is *, use relay time */
				message->record.timestamp = date_time_now_second();
				message->record.is_reclaim = false;
			}
			break;
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			if ((strstr(pt, "*") != NULL) ||
			    (parse_for_float(pt, &message->record.sensor[i - 7]) != 0)) {
				message->record.sensor[i - 7] = SENSOR_TEMP_NO_CONNECTED;
			}
			break;
		case 12:
			if (strstr(pt, "*") == NULL) {
				message->record.sensor[SENSOR_INPUT_HUMID] = atof(pt);
			} else {
				message->record.sensor[SENSOR_INPUT_HUMID] =
					SENSOR_HUMID_NO_CONNECTED;
			}
			break;
		default: {
			if ((i >= 13) && i <= (13 + ETC_DEVICE_NUM_EXTRA_ELEMENT)) {
				if (strstr(pt, "*") == NULL) {
					message->record.data[i - 13] = atoi(pt);
				} else {
					message->record.data[i - 13] = -1;
				}
			}
			break;
		}
		}
		pt = strtok(NULL, ",");
		i += 1;
	} while (pt != NULL);
	return 0;
}

static int module_lora_wait_packet(void)
{
	int ret = 0;
	int16_t rssi;
	int8_t snr;
	int64_t start_time = k_uptime_get();
	SEND_EVENT(lora, LORA_EVT_RX_READY);
	memset(lora_rx_buf, 0, sizeof(lora_rx_buf));
	memset(decoded_buf, 0, sizeof(decoded_buf));
	ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
		return -EINVAL;
	}

retry_recv:
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

		/* Retry receiving if the response is invalid or if the ACK's logger id
		 * does not match this logger's id */
		if (!response.is_okay || 
		    (strncmp(buf_tmp, response.logger_id, strlen(buf_tmp)) != 0)) {
			if ((k_uptime_get() - start_time) < LORA_RETRY_RECV_TIMEOUT_MS) {
				goto retry_recv;
			}
			return -1;
		} else {
			/* Process ACK and ACK response parameters */
			uint32_t my_time = 0;
			date_time_utc_second(&my_time);
			/* Update tx interval based on relay */
			etc_set_tx_interval_secs(response.tx_interval_in_mins * 60);
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
				SEND_EVENT(lora, LORA_EVT_ACK);
			} else {
				LOG_DBG("Receive the RECLAIM message from the replay %d from %d to "
					"%d",
					response.relay_id, response.reclaim_start_time,
					response.reclaim_end_time);
				etc_device_record_reclaim(response.reclaim_start_time,
							  response.reclaim_end_time, false);
			}
			return 0;
		}
	}

	return -EINVAL;
}
#if 0
char decr_buf[LORA_ACKUNCRYPT_LEN + 1];
#endif

static int module_lora_prepare_packet(const char* logger_id, const char* relay_iccid)
{
	int decoded_buf_len = 0;
	int tx_delay_min = etc_get_tx_interval_secs() / 60;
	int now = date_time_now_second();
	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), "%s,%d,%s,%d,0,0", logger_id, 
				   tx_delay_min, relay_iccid, now);
	__ASSERT(decoded_buf_len + 1 <= sizeof(decoded_buf), "Out of buffer memory");
	LOG_DBG("Decoded length %d", decoded_buf_len);
	LOG_DBG("Msg %s", decoded_buf);
	etc_cape_encrypt(decoded_buf, encoded_buffer, decoded_buf_len, decoded_buf_len + 1, 21);
	LOG_HEXDUMP_INF(encoded_buffer, decoded_buf_len, "ENCRYPTED");

	int rc = lora_config(lora_dev, &etc_lora_tx_config);
	if (rc < 0) {
		LOG_ERR("Lora_config failed error %d", rc);
		return -EINVAL;
	}

	rc = module_lora_transmit_packet(encoded_buffer, decoded_buf_len + 1);
	if (rc) {
		LOG_ERR("Failed to transmit packet");
		return rc;
	}

	return 0;
}

static void lora_data_send(void)
{
	struct lora_event *lora_module_event = new_lora_event();

	lora_module_event->type = LORA_EVT_RX_READY;

	APP_EVENT_SUBMIT(lora_module_event);
}

static int module_lora_relay_wait_packet(void)
{
	int ret = 0;
	int16_t rssi;
	int8_t snr;
	
	int64_t start_time = k_uptime_get();
	int64_t start_waiting_time_ms = 0;
	int64_t max_waiting_time_ms = 0;

	memset(lora_rx_buf, 0, sizeof(lora_rx_buf));
	memset(decoded_buf, 0, sizeof(decoded_buf));
	ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
		return -EINVAL;
	}
	
	char relay_iccid[ETC_SETTING_RELAY_ICCID_LEN + 1] = {0};
	etc_get_relay_iccid(relay_iccid, sizeof(relay_iccid));
	relay_iccid[ETC_SETTING_RELAY_ICCID_LEN] = '\0';

	start_waiting_time_ms = k_uptime_get();
	max_waiting_time_ms = etc_device_get_rx_timeout() * 1000;
	LOG_INF("Max waiting time %lld", max_waiting_time_ms);
retry_recv:
	ret = lora_recv(lora_dev, lora_rx_buf, sizeof(lora_rx_buf), 
			K_MSEC(LORA_LOGGER_ON_RECV_MODE_MSEC), &rssi, &snr);
	if (ret < 0) {
		LOG_DBG("No message");
	} else {
		etc_cape_decrypt((char *)lora_rx_buf, decoded_buf, ret);
		LOG_HEXDUMP_DBG(decoded_buf, ret, "Decrypted data");
		struct relay_lora_message message;
		ret = lora_module_relay_get_message(decoded_buf, rssi, &message);
		if (!ret && message.is_okay) {
			LOG_DBG("Relay ID %s - Logger ID %s", message.record.relay_id, message.record.logger_id);
			LOG_DBG("Logger info %s", message.record.logger_ver);
			LOG_DBG("rssi %d - battery %.2f - timestamp %d", message.record.logger_rssi, 
				message.record.battery, message.record.timestamp);
			LOG_DBG("Sensor %.2f %.2f %.2f %.2f %.2f %.2f", message.record.sensor[0],
				message.record.sensor[1], message.record.sensor[2], message.record.sensor[3],
				message.record.sensor[4], message.record.sensor[5]);
			if (etc_common_is_packet_from_parent(relay_iccid, message.record.relay_id)) {
				ret = etc_device_write_relay_data(&message.record);
				if (ret == 0) {
					/* Send ACK message */
					module_lora_prepare_packet(message.record.logger_id, relay_iccid);
					lora_data_send();
				}
			} else {
				LOG_WRN("Unknow packet from parent");
			}	
		} else {
			LOG_WRN("Unknown start message");
		}
	}

	int64_t delta = k_uptime_get() - start_waiting_time_ms;
	if (delta >= max_waiting_time_ms) {
		LOG_DBG("Done time: %lld", delta);
		return 0;
	} else {
		LOG_DBG("Run time: %lld", delta);
		goto retry_recv;
	}

	return 0;
}

static int module_lora_process_packet(union etc_device_record record)
{
	LOG_HEXDUMP_DBG((uint8_t *)&record, sizeof(record), "RECORD");
	int now = date_time_now_second();
	int decoded_buf_len = 0;
	etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);

	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), "S,XXXX,%s,%s,%1.2f,%d,", 
				    APP_VERSION_STR, buf_tmp, record.battery, lora_pkt_counter);

	if (record.timestamp > (now - 120) && record.timestamp < now) { 
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "*,");
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "%d,", record.timestamp);
	}

	lora_pkt_counter += 1;
	if (lora_pkt_counter >= LOGGER_MAXIMUM_COUNTER) {
		lora_pkt_counter = 0;
	}

	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++) {
		if (data_codec_compare_temperature_is_valid(record.sensor[i])) {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
						    record.sensor[i]);
		} else {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, "*,");
		}
	}
	
	if (data_codec_compare_humidity_is_valid(record.sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
			record.sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "*,");
	}

#if 0 // Test decrypt the message encoded
	etc_cape_decrypt(encoded_buffer, decr_buf, decoded_buf_len + 1);
	LOG_HEXDUMP_INF(decr_buf, sizeof(decr_buf), "DECRYPTED");
#endif
	int rc = 0;
	uint8_t cnt = 0;
	char ack_id[sizeof("XXXX")];
retry:
	if (lora_parent_id == -1 || cnt != 0) {
		/* Reset parent ID on retry (or if it is already invalid) */
		lora_parent_id = -1;
		snprintf(ack_id, sizeof(ack_id), "OPEN");
	} else {
		snprintf(ack_id, sizeof(ack_id), "%04d", lora_parent_id);
	}

	strncpy(&decoded_buf[sizeof("S,") - 1], ack_id, sizeof("XXXX") - 1);
	LOG_DBG("Decoded length %d", decoded_buf_len);
	LOG_DBG("Msg %s", decoded_buf);
	etc_cape_encrypt(decoded_buf, encoded_buffer, decoded_buf_len, decoded_buf_len + 1, 21);
	LOG_HEXDUMP_INF(encoded_buffer, decoded_buf_len, "ENCRYPTED");

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
	SEND_EVENT(lora, LORA_EVT_SEND);
	rc = module_lora_transmit_packet(encoded_buffer, decoded_buf_len + 1);
	if (rc == 0) {
		/* Backup decoded_buf before enter to wait packet API - erase decoded_buf */
		memcpy(encoded_buffer, decoded_buf, sizeof(encoded_buffer));
		rc = module_lora_wait_packet();
		if (rc == 0) {
			return 0;
		} else {
			if (cnt++ >= LORA_RETRY_MAX_TIME) {
				SEND_EVENT(lora, LORA_EVT_NACK);
				k_sleep(K_SECONDS(1));
				return rc;
			}
			
			/* In retry step, copy data back to decoded_buf for updating relay ID */
			memcpy(decoded_buf, encoded_buffer, sizeof(decoded_buf));
			k_sleep(K_SECONDS(1));
			goto retry;
		}
	} else {
		LOG_ERR("Failed to transmit packet");
		SEND_EVENT(lora, LORA_EVT_ERROR);
		return rc;
	}

	return -EINVAL;
}

/* Message handler for all states. */
static void on_all_states(struct lora_msg_data *msg)
{
	if (etc_device_is_logger_lora()) {
		if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) || 
			IS_EVENT(msg, app, APP_EVT_DATA_SYNC_CLOUD)) {
			lora_request = LORA_REQUEST_IN_RUN_LOGGER;
			k_sem_give(&lora_request_sem);
		}
	}

	if (etc_device_is_relay()) {
		if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
			lora_request = LORA_REQUEST_IN_RUN_RELAY;
			k_sem_give(&lora_request_sem);
		}
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(lora, LORA_EVT_SHUTDOWN_READY, self.id);
	}
}

static void module_lora_rx_thread_fn(void)
{
	LOG_INF("Thread RX is running");
	while (1) {
		int err = k_sem_take(&lora_request_sem, K_FOREVER);
		if (err == 0) {
			LOG_INF("On request message");
			switch (lora_request) {
				case LORA_REQUEST_IN_IDLE: {
					LOG_DBG("LORA_REQUEST_IN_IDLE");
					break;
				}
				case LORA_REQUEST_IN_RUN_RELAY: {
					LOG_INF("Relay is listening for data");
					int rc = module_lora_relay_wait_packet();
					if (rc == 0) {
						LOG_INF("Waiting time is done. Go to sleep");
					} else {
						LOG_DBG("Error in waiting packet %d", rc);
					}
					SEND_EVENT(lora, LORA_EVT_RX_DATA_READY);
					break;
				}
				case LORA_REQUEST_IN_RUN_LOGGER: {
					LOG_INF("Logger sending data");
					int rc = 0;
					do {
						union etc_device_record record;
						uint16_t record_id;
						record_id = etc_device_read_record(&record, NULL);
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
					break;
				}
			}
		}
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

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(lora, LORA_EVT_ERROR, err);
	}

	while (true) {
		module_get_next_msg(&self, &msg);
		on_all_states(&msg);
	}
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
APP_EVENT_SUBSCRIBE(MODULE, cloud_event);