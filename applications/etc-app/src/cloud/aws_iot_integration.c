#include "cloud/cloud_wrapper.h"
#include <zephyr/kernel.h>
#include <net/aws_iot.h>

#define MODULE aws_iot_integration

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CLOUD_INTEGRATION_LOG_LEVEL);

#define AWS_CLOUD_CLIENT_ID_LEN ETC_SETTINGS_DEVICE_ID_LEN

#define MESSAGES_TOPIC "exact/core/readings/old"
#define MESSAGES_TOPIC_LEN (sizeof(MESSAGES_TOPIC) - 1)

#define REQUEST_SHADOW_DOCUMENT_STRING ""

static char messages_topic[MESSAGES_TOPIC_LEN + 1];

static struct aws_iot_config config;

static cloud_wrap_evt_handler_t wrapper_evt_handler;

static void cloud_wrapper_notify_event(const struct cloud_wrap_event *evt)
{
	if ((wrapper_evt_handler != NULL) && (evt != NULL)) {
		wrapper_evt_handler(evt);
	} else {
		LOG_ERR("Library event handler not registered, or empty event");
	}
}

/**
 * @brief Function that handles incoming data from the AWS IoT library. The function notifies the
 *	  cloud module with the appropriate event based on the incoming topic.
 *
 * @param[in] event  Pointer to AWS IoT data event.
 */
static void incoming_message_handle(struct aws_iot_evt *event)
{
	struct cloud_wrap_event cloud_wrap_evt = {
		.data.buf = event->data.msg.ptr,
		.data.len = event->data.msg.len,
		.type = CLOUD_WRAP_EVT_DATA_RECEIVED
	};

	cloud_wrapper_notify_event(&cloud_wrap_evt);
}

void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
	struct cloud_wrap_event cloud_wrap_evt = { 0 };
	bool notify = false;


	switch (evt->type)
	{
	case AWS_IOT_EVT_CONNECTING:
	{
		LOG_DBG("AWS_IOT_EVT_CONNECTING");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTING;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_CONNECTED:
	{
		LOG_DBG("AWS_IOT_EVT_CONNECTED");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTED;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_READY:
	{
		LOG_DBG("AWS_IOT_EVT_READY");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_READY;
		notify = true;
		break;
	}

	case AWS_IOT_EVT_DISCONNECTED:
	{
		LOG_DBG("AWS_IOT_EVT_DISCONNECTED");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_DISCONNECTED;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_DATA_RECEIVED:
	{
		LOG_DBG("AWS_IOT_EVT_DATA_RECEIVED");
		incoming_message_handle((struct aws_iot_evt *)evt);
		break;
	}
	case AWS_IOT_EVT_FOTA_START:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_START");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_START;
		notify = true;
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
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_DONE;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_FOTA_DL_PROGRESS:
		LOG_DBG("AWS_IOT_EVT_FOTA_DL_PROGRESS, (%d%%)",
			evt->data.fota_progress);
		break;
	case AWS_IOT_EVT_ERROR:
	{
		LOG_DBG("AWS_IOT_EVT_ERROR, %d", evt->data.err);
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		cloud_wrap_evt.err = evt->data.err;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_FOTA_ERROR:
	{
		LOG_DBG("AWS_IOT_EVT_FOTA_ERROR");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_ERROR;
		notify = true;
		break;
	}
	case AWS_IOT_EVT_PUBACK:
	{
		LOG_DBG("AWS_IOT_EVT_PUBACK %d", evt->data.message_id);
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_DATA_SEND_ACK;
		cloud_wrap_evt.message_id = evt->data.message_id;
		notify = true;
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

	if (notify) {
		cloud_wrapper_notify_event(&cloud_wrap_evt);
	}
}

int cloud_wrap_init(cloud_wrap_evt_handler_t event_handler)
{
	int err;
	int len;
	char id[ETC_SETTINGS_DEVICE_ID_LEN + sizeof("urn:dev:os:60606-")] = "urn:dev:os:60606-";

	/* Use nRF52 device ID (64 bit) as part of the endpoint name */
	len = strlen(id);
	etc_get_device_id(&id[len], sizeof(id) - len);
	LOG_DBG("id: %s", id);

	config.client_id = id;
	config.client_id_len = strlen(config.client_id);

	err = aws_iot_init(&config, aws_iot_event_handler);
	if (err) {
		LOG_ERR("aws_iot_init, error: %d", err);
		return err;
	}
	LOG_DBG("AWS IoT init successful");

	wrapper_evt_handler = event_handler;

	return 0;
}

int cloud_wrap_connect(void)
{
	int err;

	err = aws_iot_connect(NULL);
	if (err) {
		LOG_ERR("aws_iot_connect, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_disconnect(void)
{
	int err;

	err = aws_iot_disconnect();
	if (err) {
		LOG_ERR("aws_iot_disconnect, error: %d", err);
		return err;
	}

	return 0;
}


int cloud_wrap_pause(void)
{
	return cloud_wrap_disconnect();
}

int cloud_wrap_resume(void)
{
	return cloud_wrap_connect();
}

int cloud_wrap_state_get(bool ack, uint32_t id)
{
	int err;

	struct aws_iot_data msg = {
		.ptr = REQUEST_SHADOW_DOCUMENT_STRING,
		.len = sizeof(REQUEST_SHADOW_DOCUMENT_STRING) - 1,
		.message_id = id,
		.qos = ack ? MQTT_QOS_1_AT_LEAST_ONCE : MQTT_QOS_0_AT_MOST_ONCE,
		.topic.type = AWS_IOT_SHADOW_TOPIC_GET
	};

	err = aws_iot_send(&msg);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_state_send(char *buf, size_t len, bool ack, uint32_t id)
{
	int err;

	struct aws_iot_data msg = {
		.ptr = buf,
		.len = len,
		.message_id = id,
		.qos = ack ? MQTT_QOS_1_AT_LEAST_ONCE : MQTT_QOS_0_AT_MOST_ONCE,
		.topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
	};

	err = aws_iot_send(&msg);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_data_send(char *buf, size_t len, bool ack, uint32_t id, 
			 const struct lwm2m_obj_path path_list[])
{
	ARG_UNUSED(path_list);

	int err;
	struct aws_iot_data msg = {
		.ptr = buf,
		.len = len,
		.message_id = id,
		.qos = ack ? MQTT_QOS_1_AT_LEAST_ONCE : MQTT_QOS_0_AT_MOST_ONCE,
		.topic.str = MESSAGES_TOPIC,
		.topic.len = MESSAGES_TOPIC_LEN
	};

	err = aws_iot_send(&msg);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_batch_send(char *buf, size_t len, bool ack, uint32_t id)
{
	return -ENOTSUP;
}

int cloud_wrap_ui_send(char *buf, size_t len, bool ack, uint32_t id, char *path_list[])
{
	ARG_UNUSED(path_list);
	return 0;
}