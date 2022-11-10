#include <zephyr/kernel.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <drivers/lora.h>
#include <zephyr.h>
#include "data/etc_cape.h"
#include "data/data_codec.h"
#include <net/aws_iot.h>

#define MODULE lora_module
#define MODULE_LORA_THREAD_STACK_SIZE 2048

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/lora_event.h"
#include "events/util_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define LORA_ACKUNCRYPT_LEN 69
struct lora_msg_data {
	union {
		struct app_event app;
		struct data_event data;
		struct util_event util;
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

/* Lora module message queue. */
#define LORA_QUEUE_ENTRY_COUNT 10
#define LORA_QUEUE_BYTE_ALIGNMENT 4
#define MODULE_LORA_SENSOR_BUFFER_COUNT 32

static struct data_lora_sensors lora_buf[MODULE_LORA_SENSOR_BUFFER_COUNT];

static int head_lora_buf = 0;

K_MSGQ_DEFINE(msgq_lora, sizeof(struct lora_msg_data),
	      LORA_QUEUE_ENTRY_COUNT, LORA_QUEUE_BYTE_ALIGNMENT);

static void rx_thread_fn(void);
static K_KERNEL_STACK_DEFINE(lora_rx_stack, 1024);

static void lora_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(lora_work, lora_work_fn);

static struct lora_modem_config etc_lora_rx_config = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};
static struct lora_modem_config etc_lora_tx_config  = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = true,
};

const struct device* lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
static struct k_thread	       lora_rx_thread;
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

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
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

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(lora, LORA_EVT_ERROR, err);
		}
	}

	return false;
}

static int setup(void)
{
	if (!device_is_ready(lora_dev))
	{
		return -1;
	}

	k_thread_create(&lora_rx_thread, lora_rx_stack,
			K_KERNEL_STACK_SIZEOF(lora_rx_stack),
			(k_thread_entry_t) rx_thread_fn,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_work_reschedule(&lora_work, K_SECONDS(1 * 30));
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
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_READY)) {
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET)) {

	}
}

/* Message handler for all states. */
static void on_all_states(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(lora, LORA_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
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

static void data_encode(void) {
	if (head_lora_buf == 0) {
		return;
	}

	LOG_INF("Head lora buf %d", head_lora_buf);
	char* data_msg = data_codec_prepare_cloud_packet(lora_buf, head_lora_buf);
	if (data_msg == NULL) {
		LOG_WRN("No message to publish");
		return;
	}
	const char topic_lora_data[] = "etc/sensor/data";

	struct aws_iot_data tx_data = {
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.topic.str = topic_lora_data,
		.topic.len = strlen(topic_lora_data),
		.ptr = data_msg,
		.len = strlen(data_msg)
	};

	LOG_INF("Publishing: %s", data_msg);

	int err = aws_iot_send(&tx_data);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
	}

	head_lora_buf = 0;
}

static void lora_work_fn(struct k_work *work) {
	data_encode();
	k_work_reschedule(&lora_work, K_SECONDS(1 * 30));
}

static void lora_module_send(const char* msg, int msg_len)
{
	struct data_lora_sensors new_lora_data = {
		.queued = true,
		.env_ts = k_uptime_get(),
	};
	memcpy(new_lora_data.sensor_msg, msg, msg_len);
	new_lora_data.sensor_msg[msg_len] = '\0';
	data_codec_populate_lora_sensor_buffer(lora_buf, &new_lora_data, &head_lora_buf, ARRAY_SIZE(lora_buf));
}

static void lora_module_report_data(const uint8_t* package) {
	char *token = strchr((const char*)package, ',');
	int count = 0;
	while (token != NULL) {
		count += 1;
		if (count == 7) {
			LOG_INF("Token %s", token);
			token += 1;
			lora_module_send(token, strlen(token));
		};
		token = strchr(token + 1, ',');
	}
}

static void rx_thread_fn(void) {
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
			char decoded_buffer[LORA_ACKUNCRYPT_LEN] = {0};
  			etc_cape_decrypt(rx_buf, decoded_buffer, ret); 
			if (decoded_buffer[0] == 'S') {
				LOG_INF("Received data: RSSI:%ddBm, SNR:%ddBm", rssi, snr);
				LOG_INF("%.*s", LORA_ACKUNCRYPT_LEN, decoded_buffer);
				lora_module_report_data(decoded_buffer);
			}
			memset(rx_buf, 0, sizeof(rx_buf));
		}
		k_sleep(K_MSEC(100));
	}
}

static void module_thread_fn(void)
{
	int err;
	struct lora_msg_data msg = { 0 };

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

K_THREAD_DEFINE(lora_module_thread, MODULE_LORA_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);
