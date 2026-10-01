#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <app_event_manager.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/random/random.h>
#include <string.h>
#include "common.h"
#include "etc_date_time.h"
#include "etc_device.h"
#include "etc_relay_reclaim.h"
#include "etc_device_record.h"
#include "etc_settings.h"
#include "app_version.h"
#include "data/etc_cape.h"
#include "cloud/cloud_codec/data_codec.h"
#include "common.h"
#include "etc_util.h"
#include "etc_sensor.h"
#include "etc_battery.h"
#define MODULE lora_module

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

#include "modules_common.h"
#include "app_module_helper.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/lora_event.h"
#include "events/util_event.h"
#include "events/cloud_event.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define LORA_ACKUNCRYPT_LEN	      128
#define LORA_ACKCRYPT_LEN	      128
#define LORA_RETRY_RECV_TIMEOUT_MS    1000
#define LORA_RETRY_MAX_TIME	      5
#define LORA_SYNC_TIME_DIFF_SEC	      30
#define LORA_LOGGER_ON_RECV_MODE_MSEC 1500
#define LORA_LOGGER_ID_LEN	      ETC_DEVICE_LORA_LOGGER_ID_SIZE
/* FW-965: while a reclaim is active and we are outside the regular transmit
 * window, retries use this shorter random tx delay cap so reclaim records keep
 * streaming within the relay's listening window. This does not overwrite the
 * standard (persisted) tx delay. */
#define LORA_TX_DELAY_RECLAIM_MSEC_MAX 20000
/* FW-965: relay extends its listening window by this many seconds (instead of
 * the default rx timeout) while it has an active reclaim queued. */
#define LORA_RX_TIMEOUT_RECLAIM_SECS   20

struct lora_msg_data {
	union {
		struct app_event app;
		struct util_event util;
		struct data_event data;
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

enum logger_msg_pos {
	MSG_POS_S = 0,
	MSG_POS_PARENT,
	MSG_POS_VERSION,
	MSG_POS_ID,
	MSG_POS_BAT,
	MSG_POS_PKT_NUM,
	MSG_POS_TIMESTAMP,
	MSG_POS_TEMP1,
	MSG_POS_TEMP2,
	MSG_POS_TEMP3,
	MSG_POS_TEMP4,
	MSG_POS_TEMP_AMBIENT,
	MSG_POS_HUMIDITY,
	MSG_POS_EXTRA_ELEMENT, /* Single error-code field, kept right after humidity */
	MSG_POS_TEMP1_B,
	MSG_POS_TEMP2_B,
	MSG_POS_TEMP3_B,
	MSG_POS_TEMP4_B,
};

/* Lora module message queue. */
#define LORA_QUEUE_ENTRY_COUNT		  36
#define LORA_QUEUE_BYTE_ALIGNMENT	  4
#define LORA_REQUEST_QUEUE_ENTRY_COUNT	  10
#define LORA_REQUEST_QUEUE_BYTE_ALIGNMENT 1

K_MSGQ_DEFINE(msgq_lora, sizeof(struct lora_msg_data), LORA_QUEUE_ENTRY_COUNT,
	      LORA_QUEUE_BYTE_ALIGNMENT);

K_SEM_DEFINE(lora_request_sem, 0, 1);
K_MUTEX_DEFINE(lora_request_mutex);
K_MUTEX_DEFINE(lora_hw_mutex);
#ifdef CONFIG_SHELL
static const struct shell *g_lora_monitor_sh = NULL;
#endif
static struct lora_request {
	enum lora_request_type type;
	int64_t uptime_ms;
} lora_request;

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
/* FW-965: set once the logger has logged that it entered the shorter reclaim tx
 * delay regime for the current send burst. Reset at the start of each burst. */
static bool reclaim_tx_delay_logged;

const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
/* Canonical settings matching Heltec WiFi LoRa 32 V3 (xMesh v2.1):
 * 915 MHz, BW 125 kHz, SF9, CR 4/5, sync 0x34 (public_network=true), preamble 8
 */
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = CONFIG_ETC_LORA_MODULE_RX_FREQUENCY,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_9,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
	.public_network = true,
};
static struct lora_modem_config etc_lora_tx_config = {
	.frequency = CONFIG_ETC_LORA_MODULE_TX_FREQUENCY,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_9,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = true,
	.public_network = true,
};

/* xMesh v2.1 Wire Protocol Definitions (matching Heltec V3 packets.h) */
#define XMESH_PKT_DATA         0xEA
#define XMESH_PKT_ACK          0xF3
#define XMESH_PKT_JOIN_REQUEST 0xF0
#define XMESH_PKT_JOIN_OFFER   0xF1

#define XMESH_DATA_PKT_SIZE    18
#define XMESH_ACK_PKT_SIZE     13
#define XMESH_JOIN_REQ_SIZE    4
#define XMESH_JOIN_OFFER_SIZE  10

static uint16_t g_mesh_parent_id = 0; /* 0 = Orphan (no parent adopted) */
static uint8_t  g_mesh_req_seq = 0;

static uint16_t xmesh_crc16_ccitt(const uint8_t *d, size_t n)
{
	uint16_t c = 0xFFFF;
	for (size_t i = 0; i < n; i++) {
		c ^= (uint16_t)d[i] << 8;
		for (int b = 0; b < 8; b++) {
			c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
		}
	}
	return c;
}

static uint16_t xmesh_get_origin_id(void)
{
	uint16_t saved_id = 0;
	if (etc_device_read_setting(ETC_SETTING_LORA_NODE_ID, &saved_id, sizeof(saved_id)) == 0 &&
	    saved_id != 0 && saved_id != 0xFFFF) {
		return saved_id;
	}

	uint8_t dev_id[8];
	ssize_t len = hwinfo_get_device_id(dev_id, sizeof(dev_id));
	if (len >= 2) {
		return (uint16_t)dev_id[len - 2] | ((uint16_t)dev_id[len - 1] << 8);
	}
	return 0xF1E1;
}

static bool xmesh_is_origin_id_persistent(void)
{
	uint16_t saved_id = 0;
	return (etc_device_read_setting(ETC_SETTING_LORA_NODE_ID, &saved_id, sizeof(saved_id)) == 0 &&
		saved_id != 0 && saved_id != 0xFFFF);
}

static int xmesh_set_origin_id(uint16_t node_id)
{
	if (node_id == 0 || node_id == 0xFFFF) {
		return etc_device_delete_setting(ETC_SETTING_LORA_NODE_ID);
	}
	return etc_device_write_setting(ETC_SETTING_LORA_NODE_ID, &node_id, sizeof(node_id));
}

static int xmesh_join_parent(uint16_t origin_id)
{
	uint8_t join_req[XMESH_JOIN_REQ_SIZE];
	join_req[0] = XMESH_PKT_JOIN_REQUEST;
	join_req[1] = (uint8_t)(origin_id & 0xFF);
	join_req[2] = (uint8_t)((origin_id >> 8) & 0xFF);
	join_req[3] = ++g_mesh_req_seq;

	LOG_INF("[XMESH] Discovering parents: JOIN_REQUEST seq %u (origin 0x%04X)",
		join_req[3], origin_id);
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa TX] Parent Discovery: JOIN_REQUEST seq %u (origin 0x%04X)",
			    join_req[3], origin_id);
	}
#endif

	int ret = lora_config(lora_dev, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("[XMESH] lora_config TX failed: %d", ret);
		return ret;
	}

	ret = lora_send(lora_dev, join_req, sizeof(join_req));
	if (ret < 0) {
		LOG_ERR("[XMESH] lora_send JOIN_REQUEST failed: %d", ret);
		return ret;
	}

	ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("[XMESH] lora_config RX failed: %d", ret);
		return ret;
	}

	uint8_t rx_buf[32];
	int16_t rssi;
	int8_t snr;
	ret = lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_MSEC(2500), &rssi, &snr);
	if (ret >= XMESH_JOIN_OFFER_SIZE && rx_buf[0] == XMESH_PKT_JOIN_OFFER) {
		uint16_t to_id = (uint16_t)rx_buf[3] | ((uint16_t)rx_buf[4] << 8);
		if (to_id == origin_id) {
			uint16_t from_id = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
			uint16_t path_cost = (uint16_t)rx_buf[6] | ((uint16_t)rx_buf[7] << 8);
			uint8_t hop = rx_buf[8];
			g_mesh_parent_id = from_id;
			LOG_INF("[XMESH] Adopted parent 0x%04X (hop %u, cost %u, rssi %d dBm)",
				from_id, hop, path_cost, rssi);
#ifdef CONFIG_SHELL
			if (g_lora_monitor_sh != NULL) {
				shell_print(g_lora_monitor_sh,
					    "[LoRa RX] Adopted parent 0x%04X (hop %u, cost %u, RSSI %d dBm)",
					    from_id, hop, path_cost, rssi);
			}
#endif
			return 0;
		}
	}
	LOG_DBG("[XMESH] No join offer received; proceeding in orphan mode (parent=0)");
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa RX] No join offer received; proceeding in orphan mode (parent=0)");
	}
#endif
	return -ENOENT;
}
#else
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
#endif

static struct k_thread lora_rx_thread;
static struct module_data self = {
	.name = "lora",
	.msg_q = &msgq_lora,
	.supports_shutdown = true,
};

static enum state_type {
	STATE_CLOUD_DISCONNECTED,
	STATE_CLOUD_CONNECTED
} state = STATE_CLOUD_DISCONNECTED;

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

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);

		msg.module.data = *event;
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

static char *state2str(enum state_type state)
{
	switch (state) {
	case STATE_CLOUD_CONNECTED:
		return "STATE_CLOUD_CONNECTED";
	case STATE_CLOUD_DISCONNECTED:
		return "STATE_CLOUD_DISCONNECTED";
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

static void request_relay_run(void)
{
	k_mutex_lock(&lora_request_mutex, K_FOREVER);
	lora_request.type = LORA_REQUEST_IN_RUN_RELAY;
	lora_request.uptime_ms = k_uptime_get();
	k_mutex_unlock(&lora_request_mutex);
	k_sem_give(&lora_request_sem);
}

static void request_logger_run(void)
{
	k_mutex_lock(&lora_request_mutex, K_FOREVER);
	lora_request.type = LORA_REQUEST_IN_RUN_LOGGER;
	lora_request.uptime_ms = k_uptime_get();
	k_mutex_unlock(&lora_request_mutex);
	k_sem_give(&lora_request_sem);
}

static inline void get_lora_request(struct lora_request *req)
{
	k_mutex_lock(&lora_request_mutex, K_FOREVER);
	memcpy(req, &lora_request, sizeof(*req));
	k_mutex_unlock(&lora_request_mutex);
}

static int setup(void)
{
	if (!device_is_ready(lora_dev)) {
		return -1;
	}

	module_lora_thread_id =
		k_thread_create(&module_lora_rx_thread, module_lora_rx_stack,
				K_KERNEL_STACK_SIZEOF(module_lora_rx_stack),
				(k_thread_entry_t)module_lora_rx_thread_fn, NULL, NULL, NULL,
				K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

	/* Device is relay, and power mode is always ON */
	if (etc_device_is_relay() && etc_device_is_always_on()) {
		request_relay_run();
	}
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
	char *saveptr;
	response.is_okay = false;
	pt = strtok_r(package, ",", &saveptr);
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
				default:
					break;
			}
			pt = strtok_r(NULL, ",", &saveptr);
		}
	}
	return response;
}

static inline bool is_empty_value(const char *val_buf)
{
	if (*val_buf == '*' && strlen(val_buf) == 1) {
		return true;
	}
	return false;
}

static int populate_logger_sensor_value(const char *val_buf, float *sensor_val)
{
	if (is_empty_value(val_buf)) {
		*sensor_val = SENSOR_TEMP_NO_CONNECTED;
	} else {
		if (parse_for_float(val_buf, sensor_val) != 0) {
			return -ENOMSG;
		}
	}
	return 0;
}

static int lora_module_relay_get_message(char *package, int16_t rssi,
					 struct relay_lora_message *message)
{
	__ASSERT(package != NULL, "Empty input package");
	__ASSERT(message != NULL, "Empty message");
	enum logger_msg_pos msg_pos = 0;
	int ret;
	char *pt;
	char *ptr;
	char *saveptr;
	message->is_okay = false;

	/* Set extra element with -1 as initialization value */
	for (int i = 0; i < ETC_DEVICE_NUM_EXTRA_ELEMENT; i++) {
		message->record.data[i] = ETC_DEVICE_INVALID_VALUE_ELEMENT;
	}

	/* Set sensor values as invalid. */
	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		message->record.sensor[i] = SENSOR_TEMP_NO_CONNECTED;
	}

	message->record.logger_rssi = rssi;

	pt = strtok_r(package, ",", &saveptr);
	do {
		if (pt == NULL) {
			message->is_okay = false;
			return -EINVAL;
		}
		switch (msg_pos) {
		case MSG_POS_S:
			if (pt[0] != 'S') {
				message->is_okay = false;
				return -EINVAL;
			}
			break;
		case MSG_POS_PARENT:
			snprintf(message->record.relay_id, sizeof(message->record.relay_id), "%s",
				 pt);
			break;
		case MSG_POS_VERSION:
			snprintf(message->record.logger_ver, sizeof(message->record.logger_ver),
				 "%s", pt);
			break;
		case MSG_POS_ID:
			snprintf(message->record.logger_id, sizeof(message->record.logger_id), "%s",
				 pt);
			break;
		case MSG_POS_BAT:
			ret = parse_for_float(pt, &message->record.battery);
			if (ret != 0) {
				goto exit_error;
			}
			break;
		case MSG_POS_PKT_NUM:
			ret = parse_for_int(pt, (int *)&message->record.packet_number);
			if (ret != 0) {
				goto exit_error;
			}
			break;
		case MSG_POS_TIMESTAMP:
			if (!is_empty_value(pt)) {
				ret = parse_for_uint(pt, &message->record.timestamp);
				/* Exit early with error if timestamp is corrupted. */
				if (ret != 0) {
					goto exit_error;
				}
				message->record.is_reclaim = true;
			} else {
				/* If timestamp is *, use relay time */
				message->record.timestamp = date_time_now_second();
				message->record.is_reclaim = false;
			}
			break;
		case MSG_POS_TEMP1:
		case MSG_POS_TEMP2:
		case MSG_POS_TEMP3:
		case MSG_POS_TEMP4:
			ret = populate_logger_sensor_value(
				pt, &message->record.sensor[msg_pos - MSG_POS_TEMP1]);
			if (ret != 0) {
				goto exit_error;
			}
			break;
		case MSG_POS_TEMP_AMBIENT:
			ret = populate_logger_sensor_value(
				pt, &message->record.sensor[SENSOR_INPUT_AMBIENT]);
			if (ret != 0) {
				goto exit_error;
			}
			/* Mark message as valid when all values up to ambient temperature
			 * have been received.
			 */
			message->is_okay = true;
			break;
		case MSG_POS_HUMIDITY:
			ret = populate_logger_sensor_value(
				pt, &message->record.sensor[SENSOR_INPUT_HUMID]);
			if (ret != 0) {
				goto exit_error;
			}
			break;
		case MSG_POS_EXTRA_ELEMENT:
			/* Single error-code field. An empty value keeps data[0] at its
			 * INVALID initialization so the relay-to-cloud encoder omits it
			 * (trailing-empty omission); a splitter logger sends it as '*' to
			 * reserve this position ahead of the sub-port temps.
			 */
			if (!is_empty_value(pt)) {
				ret = parse_for_int(pt, &message->record.data[0]);
				if (ret != 0) {
					goto exit_error;
				}
			}
			break;
		case MSG_POS_TEMP1_B:
		case MSG_POS_TEMP2_B:
		case MSG_POS_TEMP3_B:
		case MSG_POS_TEMP4_B:
			ret = populate_logger_sensor_value(
				pt,
				&message->record
					 .sensor[SENSOR_INPUT_IN5 + (msg_pos - MSG_POS_TEMP1_B)]);
			if (ret != 0) {
				goto exit_error;
			}
			break;
		default:
			break;
		}
		pt = strtok_r(NULL, ",", &saveptr);
		msg_pos += 1;
	} while (pt != NULL);
	return 0;

exit_error:
	/* Do not mark message as invalid if IV was found. */
	if (ret == 1) {
		return 0;
	}
	message->is_okay = false;
	return ret;
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
#ifdef CONFIG_SHELL
				if (g_lora_monitor_sh != NULL) {
					shell_print(g_lora_monitor_sh,
						    "[LoRa RX] Received Legacy ACK from Relay %d at time %d",
						    response.relay_id, response.current_time);
				}
#endif
				SEND_EVENT(lora, LORA_EVT_ACK);
			} else {
				LOG_DBG("Receive the RECLAIM message from the replay %d from %d to "
					"%d",
					response.relay_id, response.reclaim_start_time,
					response.reclaim_end_time);
#ifdef CONFIG_SHELL
				if (g_lora_monitor_sh != NULL) {
					shell_print(g_lora_monitor_sh,
						    "[LoRa RX] Received Legacy RECLAIM from Relay %d (%d to %d)",
						    response.relay_id, response.reclaim_start_time,
						    response.reclaim_end_time);
				}
#endif
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

static int module_lora_prepare_packet(const char *logger_id, const char *relay_iccid)
{
	int decoded_buf_len = 0;
	struct etc_device_reclaim_request reclaim_request;
	int tx_interval_mins = etc_get_tx_interval_secs() / 60;
	int now = date_time_now_second();
	int rc = etc_relay_reclaim_get_by_logger_id(logger_id, &reclaim_request);
	if (rc == 0) {
		decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), "%s,%d,%s,%d,%d,%d",
					    logger_id, tx_interval_mins, relay_iccid, now,
					    reclaim_request.start_time, reclaim_request.stop_time);
	} else {
		decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), "%s,%d,%s,%d,0,0",
					    logger_id, tx_interval_mins, relay_iccid, now);
	}

	__ASSERT(decoded_buf_len + 1 <= sizeof(decoded_buf), "Out of buffer memory");
	LOG_DBG("Decoded length %d", decoded_buf_len);
	LOG_DBG("Msg %s", decoded_buf);
	etc_cape_encrypt(decoded_buf, encoded_buffer, decoded_buf_len, decoded_buf_len + 1, 21);
	LOG_HEXDUMP_INF(encoded_buffer, decoded_buf_len, "ENCRYPTED");

	rc = lora_config(lora_dev, &etc_lora_tx_config);
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

static int module_lora_relay_wait_packet(int64_t uptime_start_ms)
{
	int ret = 0;
	int16_t rssi;
	int8_t snr;
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

	start_waiting_time_ms = uptime_start_ms;
	/* FW-965: extend the listening window while a reclaim is queued so the burst
	 * can drain in one rendezvous. Latched at entry because the reclaim entry is
	 * cleared once the logger sends its first reclaimed reading. */
	uint32_t rx_timeout_secs = etc_relay_reclaim_count() > 0 ? LORA_RX_TIMEOUT_RECLAIM_SECS
								 : etc_get_rx_timeout_secs();
	uint32_t extra_waiting_time_ms = rx_timeout_secs * 1000;
	uint32_t rx_duration_time_ms = etc_device_get_rx_duration() * 1000;
	/* FW-125: Point 1 (Increase a RX Timeout by default ) */
	max_waiting_time_ms = rx_duration_time_ms + extra_waiting_time_ms;
	LOG_INF("Max waiting time %lld", max_waiting_time_ms);
	/* If the current time is past the requested start time + listening duration,
	 * exit and do not perform a LoRa receive (unless in always on mode)
	 */
	if ((k_uptime_get() > start_waiting_time_ms + (int64_t)max_waiting_time_ms) &&
	    !etc_device_is_always_on()) {
		LOG_INF("Runtime exceeded. Not receiving data.");
		return 0;
	}
retry_recv:
	ret = lora_recv(lora_dev, lora_rx_buf, sizeof(lora_rx_buf),
			K_MSEC(LORA_LOGGER_ON_RECV_MODE_MSEC), &rssi, &snr);
	if (ret <= 0 || ret > sizeof(decoded_buf)) {
		LOG_DBG("No message %d", ret);
	} else {
		memset(decoded_buf, 0, sizeof(decoded_buf));
		etc_cape_decrypt((char *)lora_rx_buf, decoded_buf, ret);
		LOG_HEXDUMP_DBG(decoded_buf, ret, "Decrypted data");
		struct relay_lora_message message;
		ret = lora_module_relay_get_message(decoded_buf, rssi, &message);
		if (!ret && message.is_okay) {
			LOG_DBG("Relay ID %s - Logger ID %s", message.record.relay_id,
				message.record.logger_id);
			LOG_DBG("Logger info %s", message.record.logger_ver);
			LOG_DBG("rssi %d - battery %.2f - timestamp %d", message.record.logger_rssi,
				message.record.battery, message.record.timestamp);
			LOG_DBG("Sensor %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f",
				message.record.sensor[0], message.record.sensor[1],
				message.record.sensor[2], message.record.sensor[3],
				message.record.sensor[4], message.record.sensor[5],
				message.record.sensor[6], message.record.sensor[7],
				message.record.sensor[8], message.record.sensor[9]);
			if (etc_common_is_packet_from_parent(relay_iccid,
							     message.record.relay_id)) {
				/* FW-125: Point 2 (Valid message) */
				int64_t current_delta = k_uptime_get() - start_waiting_time_ms;
				if (current_delta > rx_duration_time_ms) {
					/* It is running rx_timeout, reset timeout */
					max_waiting_time_ms = current_delta + extra_waiting_time_ms;
				}
				ret = etc_device_write_relay_data(&message.record);
				if (ret == 0) {
					/* Send ACK message */
					module_lora_prepare_packet(message.record.logger_id,
								   relay_iccid);
					lora_data_send();
					if (message.record.is_reclaim) {
						etc_relay_reclaim_clear_if_satisfied(
							message.record.logger_id,
							(int)message.record.timestamp);
					}
				}
			} else {
				LOG_WRN("Unknown packet from parent");
			}
		} else {
			LOG_WRN("Bad message received");
		}
	}

	/* In always ON, keep listen RX */
	if (etc_device_is_always_on()) {
		goto retry_recv;
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

#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
static int module_lora_process_packet_xmesh(union etc_device_record record)
{
	uint16_t origin_id = xmesh_get_origin_id();

	/* Attempt parent discovery if we don't have an adopted parent */
	if (g_mesh_parent_id == 0) {
		(void)xmesh_join_parent(origin_id);
	}

	/* Extract Input 1 (temp_c_x100) and Input 2 (rh_pct_x100) only */
	int16_t temp_c_x100 = 0;
	if (sensor_temperature_is_valid(record.sensor[SENSOR_INPUT_IN1])) {
		float t1 = record.sensor[SENSOR_INPUT_IN1];
		temp_c_x100 = (int16_t)(t1 >= 0.0f ? (t1 * 100.0f + 0.5f) : (t1 * 100.0f - 0.5f));
	}

	uint16_t rh_pct_x100 = 0;
	if (sensor_temperature_is_valid(record.sensor[SENSOR_INPUT_IN2])) {
		float t2 = record.sensor[SENSOR_INPUT_IN2];
		rh_pct_x100 = (uint16_t)(t2 >= 0.0f ? (t2 * 100.0f + 0.5f) : 0);
	} else if (sensor_humidity_is_valid(record.sensor[SENSOR_INPUT_HUMID])) {
		float h = record.sensor[SENSOR_INPUT_HUMID];
		rh_pct_x100 = (uint16_t)(h >= 0.0f ? (h * 100.0f + 0.5f) : 0);
	}

	uint16_t batt_mv = (uint16_t)(record.battery >= 0.0f ? (record.battery * 1000.0f + 0.5f) : 0);
	uint32_t sampled_utc_sec = record.timestamp;

	/* Calculate CRC-16/CCITT over the 6 sensor bytes (temp_c_x100, rh_pct_x100, batt_mv) */
	uint8_t sensor_bytes[6];
	sensor_bytes[0] = (uint8_t)(temp_c_x100 & 0xFF);
	sensor_bytes[1] = (uint8_t)((temp_c_x100 >> 8) & 0xFF);
	sensor_bytes[2] = (uint8_t)(rh_pct_x100 & 0xFF);
	sensor_bytes[3] = (uint8_t)((rh_pct_x100 >> 8) & 0xFF);
	sensor_bytes[4] = (uint8_t)(batt_mv & 0xFF);
	sensor_bytes[5] = (uint8_t)((batt_mv >> 8) & 0xFF);
	uint16_t orig_crc16 = xmesh_crc16_ccitt(sensor_bytes, sizeof(sensor_bytes));

	/* Build binary DataPkt (18 bytes on air when path_len == 0) */
	uint8_t tx_buf[XMESH_DATA_PKT_SIZE];
	tx_buf[0] = XMESH_PKT_DATA; /* 0xEA */
	tx_buf[1] = (uint8_t)(g_mesh_parent_id & 0xFF);
	tx_buf[2] = (uint8_t)((g_mesh_parent_id >> 8) & 0xFF);
	tx_buf[3] = 0x00; /* path_len = 0 at origin */
	tx_buf[4] = (uint8_t)(origin_id & 0xFF);
	tx_buf[5] = (uint8_t)((origin_id >> 8) & 0xFF);
	tx_buf[6] = (uint8_t)(orig_crc16 & 0xFF);
	tx_buf[7] = (uint8_t)((orig_crc16 >> 8) & 0xFF);
	tx_buf[8] = (uint8_t)(sampled_utc_sec & 0xFF);
	tx_buf[9] = (uint8_t)((sampled_utc_sec >> 8) & 0xFF);
	tx_buf[10] = (uint8_t)((sampled_utc_sec >> 16) & 0xFF);
	tx_buf[11] = (uint8_t)((sampled_utc_sec >> 24) & 0xFF);
	memcpy(&tx_buf[12], sensor_bytes, sizeof(sensor_bytes));

	LOG_INF("[XMESH] TX DATA: parent=0x%04X, origin=0x%04X, In1=%.2f C, In2=%.2f, batt=%u mV, crc=0x%04X",
		g_mesh_parent_id, origin_id, (double)temp_c_x100 / 100.0,
		(double)rh_pct_x100 / 100.0, batt_mv, orig_crc16);
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa TX] XMESH DATA Packet:\n"
			    "  Origin ID  : 0x%04X\n"
			    "  Parent ID  : 0x%04X%s\n"
			    "  In1 (Temp) : %.2f C\n"
			    "  In2 (RH)   : %.2f\n"
			    "  Battery    : %u mV\n"
			    "  CRC16      : 0x%04X\n"
			    "  UTC Time   : %u\n"
			    "  Raw Bytes  : %02X %02X %02X %02X %02X %02X %02X %02X ...",
			    origin_id, g_mesh_parent_id,
			    (g_mesh_parent_id == 0) ? " (Orphan mode)" : "",
			    (double)temp_c_x100 / 100.0, (double)rh_pct_x100 / 100.0,
			    batt_mv, orig_crc16, sampled_utc_sec,
			    tx_buf[0], tx_buf[1], tx_buf[2], tx_buf[3],
			    tx_buf[4], tx_buf[5], tx_buf[6], tx_buf[7]);
	}
#endif

	int rc = 0;
	uint8_t retry_cnt = 0;

retry_tx:
	{
		SEND_EVENT(lora, LORA_EVT_SEND);
	}

	rc = lora_config(lora_dev, &etc_lora_tx_config);
	if (rc < 0) {
		LOG_ERR("[XMESH] lora_config TX failed: %d", rc);
		{
			SEND_EVENT(lora, LORA_EVT_ERROR);
		}
		return rc;
	}

	rc = lora_send(lora_dev, tx_buf, sizeof(tx_buf));
	if (rc < 0) {
		LOG_ERR("[XMESH] lora_send failed: %d", rc);
		{
			SEND_EVENT(lora, LORA_EVT_ERROR);
		}
		return rc;
	}

	/* Orphan mode (parent_id == 0): mesh repeaters and gateways ingest and
	 * forward without sending ACK over air. Return success immediately. */
	if (g_mesh_parent_id == 0) {
		LOG_INF("[XMESH] Transmitted as orphan (no ACK expected)");
#ifdef CONFIG_SHELL
		if (g_lora_monitor_sh != NULL) {
			shell_print(g_lora_monitor_sh,
				    "[LoRa TX] Transmitted as orphan (parent=0). No ACK expected over the air.");
		}
#endif
		{
			SEND_EVENT(lora, LORA_EVT_ACK);
		}
		return 0;
	}

	/* Joined mode: wait for PKT_ACK (0xF3) from parent */
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa RX] Waiting up to 3000 ms for ACK from 0x%04X...",
			    g_mesh_parent_id);
	}
#endif
	rc = lora_config(lora_dev, &etc_lora_rx_config);
	if (rc < 0) {
		LOG_ERR("[XMESH] lora_config RX failed: %d", rc);
		return rc;
	}

	uint8_t rx_buf[32];
	int16_t rssi;
	int8_t snr;
	rc = lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_MSEC(3000), &rssi, &snr);
	if (rc >= XMESH_ACK_PKT_SIZE && rx_buf[0] == XMESH_PKT_ACK) {
		uint16_t ack_from = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
		uint16_t ack_to   = (uint16_t)rx_buf[3] | ((uint16_t)rx_buf[4] << 8);
		uint16_t ack_crc  = (uint16_t)rx_buf[5] | ((uint16_t)rx_buf[6] << 8);
		uint8_t status    = rx_buf[7];
		uint32_t utc_sec  = (uint32_t)rx_buf[9] | ((uint32_t)rx_buf[10] << 8) |
				    ((uint32_t)rx_buf[11] << 16) | ((uint32_t)rx_buf[12] << 24);

		if (ack_to == origin_id && ack_crc == orig_crc16) {
			LOG_INF("[XMESH] ACK from 0x%04X (status=%u, utc=%u, rssi=%d dBm)",
				ack_from, status, utc_sec, rssi);
#ifdef CONFIG_SHELL
			if (g_lora_monitor_sh != NULL) {
				shell_print(g_lora_monitor_sh,
					    "[LoRa RX] XMESH ACK Received!\n"
					    "  From Parent: 0x%04X\n"
					    "  To Node    : 0x%04X (match)\n"
					    "  CRC Match  : 0x%04X (MATCH)\n"
					    "  Status     : %u\n"
					    "  Network UTC: %u\n"
					    "  Signal     : RSSI %d dBm, SNR %d dB",
					    ack_from, ack_to, ack_crc, status, utc_sec, rssi, snr);
			}
#endif
			if (utc_sec >= 1700000000UL) {
				uint32_t my_time = 0;
				date_time_utc_second(&my_time);
				if (abs((int)(utc_sec - my_time)) >= LORA_SYNC_TIME_DIFF_SEC) {
					LOG_INF("[XMESH] Syncing RTC time to %u", utc_sec);
					date_time_set_second(utc_sec);
				}
			}
			{
				SEND_EVENT(lora, LORA_EVT_ACK);
			}
			return 0;
		}
	}

	/* ACK timeout or mismatch */
	if (++retry_cnt < LORA_RETRY_MAX_TIME) {
		LOG_WRN("[XMESH] ACK timeout, retry %u/%u", retry_cnt, LORA_RETRY_MAX_TIME);
#ifdef CONFIG_SHELL
		if (g_lora_monitor_sh != NULL) {
			shell_print(g_lora_monitor_sh,
				    "[LoRa RX] ACK timeout (retry %u/%u, retrying)...",
				    retry_cnt, LORA_RETRY_MAX_TIME);
		}
#endif
		k_msleep(1000 + (sys_rand32_get() % 1000));
		goto retry_tx;
	}

	/* Failed to receive ACK after retries: invalidate parent and notify */
	LOG_WRN("[XMESH] Max retries reached; clearing parent 0x%04X", g_mesh_parent_id);
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa RX] NACK: Max retries (%u) reached! Parent 0x%04X cleared.",
			    LORA_RETRY_MAX_TIME, g_mesh_parent_id);
	}
#endif
	g_mesh_parent_id = 0;
	{
		SEND_EVENT(lora, LORA_EVT_NACK);
	}
	return -ETIMEDOUT;
}
#endif

static int module_lora_process_packet(union etc_device_record record)
{
#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
	return module_lora_process_packet_xmesh(record);
#else
	LOG_HEXDUMP_DBG((uint8_t *)&record, sizeof(record), "RECORD");
	int now = date_time_now_second();
	int decoded_buf_len = 0;
	etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);

	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), "S,XXXX,%s,%s,%1.2f,%d,",
				    APP_VERSION_STRING, buf_tmp, record.battery, lora_pkt_counter);

	if (record.timestamp > (now - 120) && record.timestamp < now) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "*,");
	} else {
		decoded_buf_len +=
			snprintf(decoded_buf + decoded_buf_len,
				 sizeof(decoded_buf) - decoded_buf_len, "%d,", record.timestamp);
	}

	lora_pkt_counter += 1;
	if (lora_pkt_counter >= LOGGER_MAXIMUM_COUNTER) {
		lora_pkt_counter = 0;
	}

	for (int i = 0; i <= SENSOR_INPUT_IN4; i++) {
		etc_common_add_sensor_value(decoded_buf, &decoded_buf_len, sizeof(decoded_buf),
					    record.sensor[i]);
	}
	etc_common_add_sensor_value(decoded_buf, &decoded_buf_len, sizeof(decoded_buf),
				    record.sensor[SENSOR_INPUT_AMBIENT]);

	if (sensor_humidity_is_valid(record.sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
					    record.sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "*,");
	}

	/* Splitter sub-port temperatures: 1.B (IN5), 2.B (IN6), 3.B (IN7), 4.B (IN8)
	 * Only include if at least one sub-port has a valid reading.
	 */
	bool has_splitter = false;
	for (int i = SENSOR_INPUT_IN5; i <= SENSOR_INPUT_IN8; i++) {
		if (sensor_temperature_is_valid(record.sensor[i])) {
			has_splitter = true;
			break;
		}
	}
	if (has_splitter) {
		/* Reserve the error-code field (position right after humidity) with an
		 * empty marker so the sub-port temps follow it, keeping the error field
		 * at its original position for backward compatibility.
		 */
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "*,");
		for (int i = SENSOR_INPUT_IN5; i <= SENSOR_INPUT_IN8; i++) {
			etc_common_add_sensor_value(decoded_buf, &decoded_buf_len,
						    sizeof(decoded_buf), record.sensor[i]);
		}
	}

#if 0 // Test decrypt the message encoded
	etc_cape_decrypt(encoded_buffer, decr_buf, decoded_buf_len + 1);
	LOG_HEXDUMP_INF(decr_buf, sizeof(decr_buf), "DECRYPTED");
#endif
	int rc = 0;
	uint8_t cnt = 0;
	char ack_id[sizeof("XXXX")];
	/* FW-965: announce the shorter reclaim tx delay regime once per burst (the
	 * shorter delay itself is applied on retries below). Logged here, at the
	 * first send that meets the condition, so the regime is observable even
	 * when no ACK retry occurs. */
	if (!reclaim_tx_delay_logged && etc_device_record_num_reclaim_records() > 0 &&
	    !app_module_in_regular_tx_window(date_time_now_second())) {
		LOG_INF("Reclaim active: using shorter tx delay (<=%u ms)",
			LORA_TX_DELAY_RECLAIM_MSEC_MAX);
		reclaim_tx_delay_logged = true;
	}
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
#ifdef CONFIG_SHELL
	if (g_lora_monitor_sh != NULL) {
		shell_print(g_lora_monitor_sh,
			    "[LoRa TX] Sending Legacy Packet:\n"
			    "  Decoded: %s\n"
			    "  Length : %d bytes",
			    decoded_buf, decoded_buf_len);
	}
#endif

	if (cnt != 0) {
		/* Generate new TX_DELAY. FW-965: during an active reclaim that has run
		 * past the regular transmit window, use a shorter random delay so reclaim
		 * records keep streaming within the relay's listening window, and do not
		 * overwrite the standard (persisted) tx delay. */
		bool short_delay = (etc_device_record_num_reclaim_records() > 0) &&
				   !app_module_in_regular_tx_window(date_time_now_second());
		uint16_t tx_delay_max = short_delay ? LORA_TX_DELAY_RECLAIM_MSEC_MAX
						    : ETC_SETTING_TX_DELAY_MSEC_MAX;
		uint16_t new_tx_delay_msec = (uint16_t)(sys_rand32_get() % tx_delay_max);
		LOG_INF("Using %s TX delay %u ms", short_delay ? "reclaim" : "standard",
			new_tx_delay_msec);
		if (!short_delay) {
			etc_set_tx_delay_msec(new_tx_delay_msec);
		}
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
#endif
}

/* Message handler for STATE_CLOUD_DISCONNECTED. */
static void on_state_cloud_disconnected(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		state_set(STATE_CLOUD_CONNECTED);
		if (etc_device_is_relay()) {
			request_relay_run();
		}
	}
}

/* Message handler for STATE_CLOUD_CONNECTED. */
static void on_state_cloud_connected(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED) ||
	    IS_EVENT(msg, cloud, CLOUD_EVT_PAUSED)) {
		state_set(STATE_CLOUD_DISCONNECTED);
	}

	if (etc_device_is_relay()) {
		if (IS_EVENT(msg, app, APP_EVT_DATA_RECEIVE)) {
			request_relay_run();
		}
	}
}

/* Message handler for all states. */
static void on_all_states(struct lora_msg_data *msg)
{
	if (etc_device_is_logger_lora()) {
		if (IS_EVENT(msg, app, APP_EVT_DATA_TRANSMIT) ||
		    IS_EVENT(msg, app, APP_EVT_DATA_SYNC_CLOUD)) {
			request_logger_run();
		}
	}

	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(lora, LORA_EVT_SHUTDOWN_READY, self.id);
	}

	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_ENTER_ALWAYS_ON_MODE)) {
		request_relay_run();
	}
}

static void module_lora_rx_thread_fn(void)
{
	struct lora_request req;

	LOG_INF("Thread RX is running");
	while (1) {
		int err = k_sem_take(&lora_request_sem, K_FOREVER);
		if (err == 0) {
			LOG_INF("On request message");
			get_lora_request(&req);
			switch (req.type) {
			case LORA_REQUEST_IN_IDLE: {
				LOG_DBG("LORA_REQUEST_IN_IDLE");
				break;
			}
			case LORA_REQUEST_IN_RUN_RELAY: {
				k_mutex_lock(&lora_hw_mutex, K_FOREVER);
				LOG_INF("Relay is listening for data");
				{
					SEND_EVENT(lora, LORA_EVT_RELAY_START_RX);
				}
				int rc = module_lora_relay_wait_packet(req.uptime_ms);
				if (rc == 0) {
					LOG_INF("Waiting time is done. Go to sleep");
				} else {
					LOG_DBG("Error in waiting packet %d", rc);
				}
				{
					SEND_EVENT(lora, LORA_EVT_RELAY_RX_COMPLETE);
				}
				k_mutex_unlock(&lora_hw_mutex);
				break;
			}
			case LORA_REQUEST_IN_RUN_LOGGER: {
				k_mutex_lock(&lora_hw_mutex, K_FOREVER);
				LOG_INF("Logger sending data");
				int rc = 0;
				reclaim_tx_delay_logged = false;
				do {
					union etc_device_record record;
					int record_id;
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
					} else if (record_id < 0) {
						LOG_ERR("Error in reading record");
						break;
					} else {
						LOG_INF("No NACK record");
						break;
					}
				} while (1);
				SEND_EVENT(lora, LORA_EVT_RX_DATA_READY);
				k_mutex_unlock(&lora_hw_mutex);
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
		switch (state) {
		case STATE_CLOUD_DISCONNECTED:
			on_state_cloud_disconnected(&msg);
			break;
		case STATE_CLOUD_CONNECTED:
			on_state_cloud_connected(&msg);
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

#ifdef CONFIG_SHELL

static int cmd_lora_send(const struct shell *sh, size_t argc, char **argv)
{
	if (!device_is_ready(lora_dev)) {
		shell_error(sh, "LoRa device is not ready!");
		return -ENODEV;
	}

	union etc_device_record record;
	memset(&record, 0, sizeof(record));
	int record_id = 0;

	if (argc > 1 && strcmp(argv[1], "flash") == 0) {
		record_id = etc_device_read_record(&record, NULL);
		if (record_id <= 0) {
			shell_warn(sh, "[LoRa] No pending un-ACKed records in flash.");
			return 0;
		}
		shell_print(sh, "[LoRa] Transmitting pending record #%d from flash", record_id);
	} else {
		bool fresh_sample = false;
		if (argc > 1 && (strcmp(argv[1], "sample") == 0 || strcmp(argv[1], "fresh") == 0)) {
			fresh_sample = true;
		}

		struct etc_logger_all_inputs all_inputs;
		int ret = etc_sensor_read_all_inputs(&all_inputs, fresh_sample);
		if (ret != 0 && fresh_sample) {
			shell_warn(sh, "[LoRa] Live acquisition failed (%d), using cached readings", ret);
			ret = etc_sensor_read_all_inputs(&all_inputs, false);
		}

		/* If no previous sample was ever taken on port 1/2 or ambient, acquire a fresh sample */
		if (!all_inputs.in[0].connected && !all_inputs.in[1].connected &&
		    !all_inputs.ambient_valid && !fresh_sample) {
			shell_print(sh, "[LoRa] No previous readings; acquiring fresh sample from sensors...");
			etc_sensor_read_all_inputs(&all_inputs, true);
		}

		record.timestamp = (uint32_t)all_inputs.timestamp;
		record.battery = (float)all_inputs.battery_mv / 1000.0f;
		record.flag = (uint32_t)all_inputs.battery_status;

		for (int i = 0; i < ETC_DEVICE_NUM_SENSOR; i++) {
			record.sensor[i] = SENSOR_TEMP_NO_CONNECTED;
		}
		for (int i = 0; i < 4; i++) {
			record.sensor[i] = all_inputs.in[i].temp_c;
		}
		for (int i = 0; i < 4; i++) {
			record.sensor[i + SENSOR_INPUT_IN5] = all_inputs.splitter[i].temp_c;
		}
		record.sensor[SENSOR_INPUT_AMBIENT] = all_inputs.ambient_temp_c;
		record.sensor[SENSOR_INPUT_HUMID] = all_inputs.humidity_percent;

		shell_print(sh, "[LoRa] Transmitting actual logger readings:");
		if (all_inputs.in[0].connected) {
			shell_print(sh, "  Port 1 (IN1) : %.2f C [%s]",
				    (double)all_inputs.in[0].temp_c,
				    etc_sensor_type_str(all_inputs.in[0].type));
		} else {
			shell_print(sh, "  Port 1 (IN1) : -- (No probe)");
		}

		if (all_inputs.in[1].connected) {
			shell_print(sh, "  Port 2 (IN2) : %.2f C [%s]",
				    (double)all_inputs.in[1].temp_c,
				    etc_sensor_type_str(all_inputs.in[1].type));
		} else if (all_inputs.humidity_valid) {
			shell_print(sh, "  Port 2 (RH)  : %.2f %%",
				    (double)all_inputs.humidity_percent);
		} else {
			shell_print(sh, "  Port 2 (IN2) : -- (No probe)");
		}

		shell_print(sh, "  Battery      : %u mV (%.2f V, %u%%, %s)",
			    all_inputs.battery_mv,
			    (double)record.battery,
			    all_inputs.battery_percent,
			    etc_battery_status_str(all_inputs.battery_status));
	}

	const struct shell *prev_sh = g_lora_monitor_sh;
	g_lora_monitor_sh = sh;

	k_mutex_lock(&lora_hw_mutex, K_FOREVER);
	int rc = module_lora_process_packet(record);
	k_mutex_unlock(&lora_hw_mutex);

	g_lora_monitor_sh = prev_sh;

	if (rc == 0) {
		if (record_id > 0) {
			etc_device_set_ack_record(record_id);
		}
		shell_print(sh, "[LoRa] Packet transmitted and ACKed successfully.");
	} else {
		shell_error(sh, "[LoRa] Transmission failed / unacknowledged (code %d)", rc);
	}
	return rc;
}

static int cmd_lora_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "================== LoRa Subsystem Status ==================");
	shell_print(sh, "Device Ready    : %s (%s)",
		    device_is_ready(lora_dev) ? "YES" : "NO",
		    device_is_ready(lora_dev) ? lora_dev->name : "N/A");
	shell_print(sh, "Device Mode     : %s",
		    etc_device_is_logger_lora() ? "LoRa Logger (Mode 1)" :
		    etc_device_is_relay()       ? "Relay (Mode 0)" :
		    (etc_get_device_mode() == ETC_DEVICE_MODE_LTE_LOGGER) ? "LTE Logger (Mode 2)" : "Other");
	shell_print(sh, "TX Frequency    : %u MHz", CONFIG_ETC_LORA_MODULE_TX_FREQUENCY / 1000000);
	shell_print(sh, "RX Frequency    : %u MHz", CONFIG_ETC_LORA_MODULE_RX_FREQUENCY / 1000000);
#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
	shell_print(sh, "Protocol        : EXACT LoRa Mesh v2.1 (Heltec V3 compatible)");
	shell_print(sh, "Modulation      : SF9, BW 125 kHz, CR 4/5, Public Sync 0x34");
	shell_print(sh, "Origin Node ID  : 0x%04X%s",
		    xmesh_get_origin_id(),
		    xmesh_is_origin_id_persistent() ? " (Persistent in NVS)" : " (Auto from HW ID)");
	shell_print(sh, "Adopted Parent  : 0x%04X%s",
		    g_mesh_parent_id,
		    (g_mesh_parent_id == 0) ? " (Orphan mode - no ACK expected)" : "");
#else
	shell_print(sh, "Protocol        : Legacy EXACT LoRa (CAPE encrypted)");
	shell_print(sh, "Modulation      : SF7, BW 125 kHz, CR 4/5, Private Sync 0x12");
	shell_print(sh, "Relay Parent ID : %d", lora_parent_id);
#endif
	shell_print(sh, "TX Delay        : %u ms", etc_get_tx_delay_msec());
	shell_print(sh, "TX Interval     : %u s (%u mins)",
		    etc_get_tx_interval_secs(), etc_get_tx_interval_secs() / 60);
	shell_print(sh, "===========================================================");
	return 0;
}

static int cmd_lora_monitor(const struct shell *sh, size_t argc, char **argv)
{
	if (!device_is_ready(lora_dev)) {
		shell_error(sh, "LoRa device is not ready!");
		return -ENODEV;
	}

	uint32_t duration_sec = 30;
	bool trigger_send = false;

	for (size_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "send") == 0 || strcmp(argv[i], "--send") == 0) {
			trigger_send = true;
		} else {
			int val = atoi(argv[i]);
			if (val > 0) {
				duration_sec = (uint32_t)val;
			}
		}
	}

	shell_print(sh, "==========================================================");
	shell_print(sh, "   LoRa Live Monitor Started (%u seconds)", duration_sec);
	shell_print(sh, "   RX Freq : %u MHz | BW: 125 kHz",
		    CONFIG_ETC_LORA_MODULE_RX_FREQUENCY / 1000000);
#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
	shell_print(sh, "   Protocol: EXACT LoRa Mesh v2.1 (SF9, Public Sync 0x34)");
	shell_print(sh, "   Origin  : 0x%04X | Parent: 0x%04X%s",
		    xmesh_get_origin_id(), g_mesh_parent_id,
		    (g_mesh_parent_id == 0) ? " (Orphan mode)" : "");
#else
	shell_print(sh, "   Protocol: Legacy EXACT LoRa (SF7, Private Sync 0x12)");
#endif
	shell_print(sh, "==========================================================");

	const struct shell *prev_sh = g_lora_monitor_sh;
	g_lora_monitor_sh = sh;

	if (trigger_send) {
		shell_print(sh, "--> Triggering live transmission...");
		cmd_lora_send(sh, 0, NULL);
		shell_print(sh, "--> Now listening for channel traffic...");
	}

	int64_t start_time = k_uptime_get();
	int64_t end_time = start_time + (int64_t)duration_sec * 1000;
	int64_t last_heartbeat = start_time;
	uint8_t rx_buf[128];
	int16_t rssi;
	int8_t snr;
	uint32_t rx_packet_count = 0;

	while (k_uptime_get() < end_time) {
		int64_t now = k_uptime_get();
		int64_t remain_ms = end_time - now;
		if (remain_ms <= 0) {
			break;
		}

		uint32_t slice_ms = (remain_ms > 1000) ? 1000 : (uint32_t)remain_ms;

		k_mutex_lock(&lora_hw_mutex, K_FOREVER);
		int rc = lora_config(lora_dev, &etc_lora_rx_config);
		if (rc < 0) {
			k_mutex_unlock(&lora_hw_mutex);
			shell_error(sh, "Failed to configure radio for RX: %d", rc);
			break;
		}
		int ret = lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_MSEC(slice_ms), &rssi, &snr);
		k_mutex_unlock(&lora_hw_mutex);

		now = k_uptime_get();
		if (ret > 0) {
			rx_packet_count++;
			double elapsed_sec = (double)(now - start_time) / 1000.0;
			shell_print(sh, "\n[+%.1fs] [LoRa RX] Captured packet (%d bytes, RSSI: %d dBm, SNR: %d dB):",
				    elapsed_sec, ret, rssi, snr);

#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
			if (rx_buf[0] == XMESH_PKT_DATA && ret >= XMESH_DATA_PKT_SIZE) {
				uint16_t parent_id = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
				uint8_t path_len = rx_buf[3];
				uint16_t origin_id = (uint16_t)rx_buf[4] | ((uint16_t)rx_buf[5] << 8);
				uint16_t crc16 = (uint16_t)rx_buf[6] | ((uint16_t)rx_buf[7] << 8);
				uint32_t utc_sec = (uint32_t)rx_buf[8] | ((uint32_t)rx_buf[9] << 8) |
						   ((uint32_t)rx_buf[10] << 16) | ((uint32_t)rx_buf[11] << 24);
				int16_t in1_raw = (int16_t)((uint16_t)rx_buf[12] | ((uint16_t)rx_buf[13] << 8));
				int16_t in2_raw = (int16_t)((uint16_t)rx_buf[14] | ((uint16_t)rx_buf[15] << 8));
				uint16_t batt_mv = (uint16_t)rx_buf[16] | ((uint16_t)rx_buf[17] << 8);

				shell_print(sh, "  Type       : XMESH DATA (0xEA)\n"
						"  Origin ID  : 0x%04X\n"
						"  Parent ID  : 0x%04X (hops: %u)\n"
						"  In1 (Temp) : %.2f C\n"
						"  In2 (RH)   : %.2f\n"
						"  Battery    : %u mV\n"
						"  CRC16      : 0x%04X\n"
						"  UTC Time   : %u",
					    origin_id, parent_id, path_len,
					    (double)in1_raw / 100.0, (double)in2_raw / 100.0,
					    batt_mv, crc16, utc_sec);
			} else if (rx_buf[0] == XMESH_PKT_ACK && ret >= XMESH_ACK_PKT_SIZE) {
				uint16_t ack_from = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
				uint16_t ack_to   = (uint16_t)rx_buf[3] | ((uint16_t)rx_buf[4] << 8);
				uint16_t ack_crc  = (uint16_t)rx_buf[5] | ((uint16_t)rx_buf[6] << 8);
				uint8_t status    = rx_buf[7];
				uint32_t utc_sec  = (uint32_t)rx_buf[9] | ((uint32_t)rx_buf[10] << 8) |
						    ((uint32_t)rx_buf[11] << 16) | ((uint32_t)rx_buf[12] << 24);

				shell_print(sh, "  Type       : XMESH ACK (0xF3)\n"
						"  From Parent: 0x%04X\n"
						"  To Node    : 0x%04X\n"
						"  CRC Match  : 0x%04X\n"
						"  Status     : %u\n"
						"  Network UTC: %u",
					    ack_from, ack_to, ack_crc, status, utc_sec);
			} else if (rx_buf[0] == XMESH_PKT_JOIN_REQUEST && ret >= XMESH_JOIN_REQ_SIZE) {
				uint16_t origin_id = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
				uint8_t seq = rx_buf[3];
				shell_print(sh, "  Type       : XMESH JOIN_REQUEST (0xF0)\n"
						"  Origin ID  : 0x%04X\n"
						"  Sequence   : %u",
					    origin_id, seq);
			} else if (rx_buf[0] == XMESH_PKT_JOIN_OFFER && ret >= XMESH_JOIN_OFFER_SIZE) {
				uint16_t from_id = (uint16_t)rx_buf[1] | ((uint16_t)rx_buf[2] << 8);
				uint16_t to_id   = (uint16_t)rx_buf[3] | ((uint16_t)rx_buf[4] << 8);
				uint16_t cost    = (uint16_t)rx_buf[6] | ((uint16_t)rx_buf[7] << 8);
				uint8_t hop      = rx_buf[8];
				shell_print(sh, "  Type       : XMESH JOIN_OFFER (0xF1)\n"
						"  From Parent: 0x%04X\n"
						"  To Node    : 0x%04X\n"
						"  Hop / Cost : hop %u, cost %u",
					    from_id, to_id, hop, cost);
			} else
#endif
			{
				char decr[128];
				memset(decr, 0, sizeof(decr));
				etc_cape_decrypt((char *)rx_buf, decr, MIN(ret, (int)sizeof(decr) - 1));
				if (decr[0] == 'S' || decr[0] == 'R' || decr[0] == 'A') {
					shell_print(sh, "  Type       : Legacy Decrypted Payload\n"
							"  Text       : %s", decr);
				} else {
					shell_hexdump(sh, rx_buf, ret);
				}
			}
		} else {
			if (now - last_heartbeat >= 5000) {
				last_heartbeat = now;
				int remain_s = (int)((end_time - now) / 1000);
				if (remain_s > 0) {
					shell_print(sh, "[LoRa] Listening... (%d s remaining, %u packets captured)",
						    remain_s, rx_packet_count);
				}
			}
		}
	}

	g_lora_monitor_sh = prev_sh;
	shell_print(sh, "==========================================================");
	shell_print(sh, "   LoRa Live Monitor Ended (%u s elapsed | %u packets captured)",
		    duration_sec, rx_packet_count);
	shell_print(sh, "==========================================================");
	return 0;
}

#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
static int cmd_lora_node_id(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		uint16_t id = xmesh_get_origin_id();
		bool is_nvs = xmesh_is_origin_id_persistent();
		shell_print(sh, "LoRa Mesh Origin Node ID: 0x%04X (%u) [%s]",
			    id, id, is_nvs ? "Saved in NVS Flash/EEPROM" : "Auto from Nordic HW ID");
		return 0;
	}

	if (strcmp(argv[1], "auto") == 0 || strcmp(argv[1], "clear") == 0 || strcmp(argv[1], "default") == 0) {
		int rc = xmesh_set_origin_id(0);
		if (rc != 0 && rc != -ENOENT) {
			shell_error(sh, "Failed to clear NVS Node ID: %d", rc);
			return rc;
		}
		shell_print(sh, "NVS override cleared. Restored auto hardware Node ID: 0x%04X",
			    xmesh_get_origin_id());
		return 0;
	}

	char clean_str[32];
	const char *src = argv[1];
	/* Strip leading/trailing quotes if present */
	if (*src == '"' || *src == '\'') {
		src++;
	}
	size_t len = 0;
	while (*src && *src != '"' && *src != '\'' && len < sizeof(clean_str) - 1) {
		clean_str[len++] = *src++;
	}
	clean_str[len] = '\0';

	/* Check if the string has 'O' or 'o' commonly mistyped for '0' */
	bool substituted_o = false;
	for (size_t i = 0; i < len; i++) {
		if (clean_str[i] == 'O' || clean_str[i] == 'o') {
			substituted_o = true;
			clean_str[i] = '0';
		}
	}

	char *endptr = NULL;
	/* 1. Try parsing with base 0 (handles 0x hex and decimal) */
	unsigned long val = strtoul(clean_str, &endptr, 0);

	/* 2. If base 0 stopped on a non-decimal character (e.g. "6CE0" without "0x"), try base 16 */
	if (*endptr != '\0') {
		val = strtoul(clean_str, &endptr, 16);
	}

	if (*endptr != '\0' || val == 0 || val > 0xFFFF) {
		shell_error(sh, "Invalid Node ID '%s'. Must be 1..65535 or 4-digit hex (e.g. 0x6CE0, 6CE0, or 'auto')", argv[1]);
		return -EINVAL;
	}

	if (substituted_o) {
		shell_warn(sh, "Interpreted letter 'O' in '%s' as digit '0' -> 0x%04lX", argv[1], val);
	}

	uint16_t new_id = (uint16_t)val;
	int rc = xmesh_set_origin_id(new_id);
	if (rc != 0) {
		shell_error(sh, "Failed to write Node ID 0x%04X to NVS: %d", new_id, rc);
		return rc;
	}

	shell_print(sh, "[OK] Node ID permanently saved to NVS Flash/EEPROM: 0x%04X (%u)", new_id, new_id);
	shell_print(sh, "Active Origin Node ID is now: 0x%04X", xmesh_get_origin_id());
	return 0;
}
#endif

static int cmd_lora_default(const struct shell *sh, size_t argc, char **argv)
{
	if (argc <= 2) {
		return cmd_lora_monitor(sh, argc, argv);
	}
	shell_help(sh);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_lora,
	SHELL_CMD_ARG(monitor, NULL,
		      "Monitor LoRa sending & receiving for N seconds (default 30)\n"
		      "Usage: lora monitor [seconds] [send]",
		      cmd_lora_monitor, 1, 2),
#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)
	SHELL_CMD_ARG(node_id, NULL,
		      "Get or permanently save LoRa Mesh Node ID in NVS/EEPROM\n"
		      "Usage: lora node_id [0x<hex>|<dec>|auto]",
		      cmd_lora_node_id, 1, 1),
#endif
	SHELL_CMD_ARG(send, NULL,
		      "Transmit real logger readings (Port 1, Port 2, Battery) over LoRa\n"
		      "Usage: lora send [sample|flash]",
		      cmd_lora_send, 1, 1),
	SHELL_CMD(status, NULL,
		  "Display current LoRa radio configuration and network state",
		  cmd_lora_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(lora, &sub_lora,
		   "LoRa commands and live traffic monitor (default: 30s monitor)",
		   cmd_lora_default);
#endif /* CONFIG_SHELL */