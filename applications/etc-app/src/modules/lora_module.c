#include <zephyr/kernel.h>
#include <stdio.h>
#include <app_event_manager.h>
#include <drivers/lora.h>
#include <zephyr.h>
#include "etc_date_time.h"
#include "etc_device.h"
#include "etc_settings.h"
#include "app_version.h"
#include "data/etc_cape.h"
#include "data/data_codec.h"

#define MODULE lora_module
#define MODULE_LORA_THREAD_STACK_SIZE 1024

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/lora_event.h"
#include "events/util_event.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

#define LORA_ACKUNCRYPT_LEN 128
#define LORA_ACKCRYPT_LEN 128
#define LORA_RETRY_MAX_TIME 2
#define LORA_SYNC_TIME_DIFF_SEC 30

struct lora_msg_data
{
	union
	{
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

K_MSGQ_DEFINE(msgq_lora, sizeof(struct lora_msg_data),
	      LORA_QUEUE_ENTRY_COUNT, LORA_QUEUE_BYTE_ALIGNMENT);

static void rx_thread_fn(void);
static K_KERNEL_STACK_DEFINE(lora_rx_stack, 1024);

static char decoded_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static char buf_tmp[ETC_SETTINGS_DEVICE_ID_LEN];
static char encoded_buffer[LORA_ACKCRYPT_LEN] = {0};
static uint8_t lora_rx_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
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
	switch (new_state)
	{
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
	switch (new_state)
	{
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
	struct lora_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_app_event(aeh))
	{
		struct app_event *event = cast_app_event(aeh);

		msg.module.app = *event;
		enqueue_msg = true;
	}

	if (is_data_event(aeh))
	{
		struct data_event *event = cast_data_event(aeh);

		msg.module.data = *event;
		enqueue_msg = true;
	}

	if (is_util_event(aeh))
	{
		struct util_event *event = cast_util_event(aeh);

		msg.module.util = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg)
	{
		int err = module_enqueue_msg(&self, &msg);

		if (err)
		{
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
	if (!device_is_ready(lora_dev))
	{
		return -1;
	}

	if (etc_device_get_mode() == ETC_DEVICE_MODE_RELAY)
	{
		LOG_INF("Start a thread for Relay LORA");
		k_thread_create(&lora_rx_thread, lora_rx_stack,
				K_KERNEL_STACK_SIZEOF(lora_rx_stack),
				(k_thread_entry_t)rx_thread_fn,
				NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	}
	else
	{
		LOG_INF("Logger device mode for Lora");
	}

	return 0;
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START))
	{
		state_set(STATE_RUNNING);
		sub_state_set(SUB_STATE_RECEIVE_MODE);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct lora_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_READY))
	{
	}

	if (IS_EVENT(msg, app, APP_EVT_DATA_GET))
	{
	}
}

static int module_lora_transmit_packet(const uint8_t *decoded_buf, int buf_len)
{
	int ret = lora_config(lora_dev, &etc_lora_tx_config);
	if (ret < 0)
	{
		LOG_ERR("Lora_config failed error %d", ret);
		return -EINVAL;
	}

	ret = lora_send(lora_dev, (uint8_t *)decoded_buf, buf_len);
	if (ret < 0)
	{
		LOG_ERR("LoRa send failed");
		return -EINVAL;
	}

	return 0;
}

static uint32_t lora_module_get_utc_data(char* package)
{
    uint32_t time = 0;
    uint8_t i;
    char * pt;
    char *ptr;
    pt = strtok(package, ",");
    if (pt != NULL) { //break down ACK string into parts. 
      for (i = 0; i < 6; i++) {
        if (i >= 3) {
			time = strtoul(pt, &ptr, 10);
			break;
		}
        pt = strtok(NULL, ",");
      }
    }
	return time;
}

static int module_lora_wait_packet(void) {
	int ret = 0;
	int16_t rssi;
	int8_t snr;
	memset(lora_rx_buf, 0, sizeof(lora_rx_buf));
	memset(decoded_buf, 0, sizeof(decoded_buf));
	ret = lora_recv(lora_dev, lora_rx_buf, sizeof(lora_rx_buf), K_SECONDS(etc_device_get_rx_timeout()), &rssi, &snr);
	if (ret < 0)
	{
		LOG_DBG("Timeout");
		return -ETIMEDOUT;
	}
	else
	{	
		LOG_DBG("Received ACK message");
		etc_cape_decrypt((char *)lora_rx_buf, decoded_buf, ret);
		uint32_t my_time = 0;
		date_time_utc_second(&my_time);
		uint32_t sync_time = lora_module_get_utc_data(decoded_buf);
		if (abs(sync_time - my_time) >= LORA_SYNC_TIME_DIFF_SEC) {
			date_time_set_second(sync_time);
		}
	}

	return 0;
}

static int module_lora_process_packet(etc_device_record_t record)
{
	etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);
	strcpy(decoded_buf, "S,OPEN,");
	strcat(decoded_buf, APP_VERSION_STR);
	strcat(decoded_buf, ",");
	strcat(decoded_buf, buf_tmp); // add device unique ID
	strcat(decoded_buf, ",");
	snprintf(buf_tmp, sizeof(buf_tmp), "%1.2f", record.battery);
	strcat(decoded_buf, buf_tmp);
	strcat(decoded_buf, ",0,");
	snprintf(buf_tmp, sizeof(buf_tmp), "%u", record.timestamp);
	strcat(decoded_buf, buf_tmp);
	strcat(decoded_buf, ",");
	for (int i = 0; i < SENSOR_EVENT_NUM_DEV_MAX; i++)
	{
		if (data_codec_compare_temperature_is_valid(record.sensor[i]))
		{
			snprintf(buf_tmp, sizeof(buf_tmp), "%2.2f", record.sensor[i]);
			strcat(decoded_buf, buf_tmp);
			strcat(decoded_buf, ",");
		}
		else
		{
			strcat(decoded_buf, "*,");
		}
	}

	strcat(decoded_buf, "*,");
	LOG_DBG("Decoded length %d", strlen(decoded_buf));
	decoded_buf[strlen(decoded_buf)] = '\0';
	LOG_DBG("Msg %s", decoded_buf);
	etc_cape_encrypt((char *)decoded_buf, encoded_buffer, strlen(decoded_buf), 21);
	int rc = module_lora_transmit_packet(encoded_buffer, strlen(encoded_buffer));
	if (rc == 0) {
		uint8_t cnt = 0;
retry:
		rc = module_lora_wait_packet();
		if (rc == 0) {
			return 0;
		} else {
			if (cnt++ >= LORA_RETRY_MAX_TIME) {
				return rc;
			} 
			k_sleep(K_SECONDS(30));
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
	if (etc_device_get_mode() == ETC_DEVICE_MODE_LOGGER)
	{
		if (IS_EVENT(msg, data, DATA_EVT_DATA_READY))
		{
			LOG_INF("Logger sending data");
			k_sleep(K_SECONDS(1));
			int rc = 0;
			do
			{
				etc_device_record_t record;
				rc = etc_device_read_record(&record);
				if (rc > 0)
				{
					LOG_INF("Sending data over LORA");
					rc = module_lora_process_packet(record);
					if (rc == 0) {
						etc_device_set_ack_record(rc);
					} else {
						LOG_ERR("Timeout in waiting ACK. Sleep in 30s");
						break;
					}
				}
				else if (rc < 0)
				{
					LOG_ERR("Error in reading record");
					break;
				}
				else
				{
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
	while (token != NULL)
	{
		count += 1;
		if (count == 7)
		{
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
	if (ret < 0)
	{
		LOG_ERR("Lora_config failed error %d", ret);
	}
	ret = 0;
	uint8_t rx_buf[128] = {0x00};
	int16_t rssi;
	int8_t snr;
	while (1)
	{
		ret = lora_recv(lora_dev, rx_buf, sizeof(rx_buf), K_FOREVER, &rssi, &snr);
		if (ret < 0)
		{
			continue;
		}
		else
		{
			etc_cape_decrypt((char *)rx_buf, decoded_buf, ret);
			if (decoded_buf[0] == 'S')
			{
				LOG_INF("Received data: RSSI:%ddBm, SNR:%ddBm", rssi, snr);
				LOG_INF("%.*s", LORA_ACKUNCRYPT_LEN, decoded_buf);
				lora_module_report_data((const uint8_t *)decoded_buf);
			}
			memset(rx_buf, 0, sizeof(rx_buf));
		}
		k_sleep(K_MSEC(500));
	}
}

static void module_thread_fn(void)
{
	int err;
	struct lora_msg_data msg = {0};

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err)
	{
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(lora, LORA_EVT_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err)
	{
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(lora, LORA_EVT_ERROR, err);
	}

	while (true)
	{
		module_get_next_msg(&self, &msg);

		switch (state)
		{
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			switch (sub_state)
			{
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
