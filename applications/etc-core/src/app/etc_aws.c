/***************************************************************************/
/*!
\file       etc_aws.c
\brief      AWS service application

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <net/aws_iot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ETC_AWS, CONFIG_ETC_APP_LOG_LEVEL);

#define APP_TOPICS_COUNT CONFIG_AWS_IOT_APP_SUBSCRIPTION_LIST_COUNT
#define APP_TOPIC_SETTING "setting"

static struct k_work_delayable shadow_update_work;
static struct k_work_delayable connect_work;
static struct k_work shadow_update_version_work;

static int shadow_update_internal_ms;

static bool cloud_connected;

K_SEM_DEFINE(lte_connected, 0, 1);

static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
{
	cJSON_AddItemToObject(parent, str, item);

	return 0;
}

static int json_add_str(cJSON *parent, const char *str, const char *item)
{
	cJSON *json_str;

	json_str = cJSON_CreateString(item);
	if (json_str == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_str);
}

static int json_add_number(cJSON *parent, const char *str, double item)
{
	cJSON *json_num;

	json_num = cJSON_CreateNumber(item);
	if (json_num == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_num);
}

static int shadow_update(bool version_number_include)
{
	int err;
	char *message;
	time_t message_ts = 0;
	int16_t bat_voltage = 0;
	int16_t temp = 0;
	int16_t humid = 0;

	pcf85263a_rtc_get_time(&message_ts);
	cJSON *root_obj = cJSON_CreateObject();;
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *device_obj = cJSON_CreateObject();
	cJSON *data_obj = cJSON_CreateObject();
	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL || device_obj == NULL 
		|| data_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(device_obj);
		cJSON_Delete(data_obj);
		err = -ENOMEM;
		return err;
	}

	if (version_number_include) {
		err = json_add_str(reported_obj, "version",
				    CONFIG_APP_VERSION);
	} else {
		err = 0;
	}
	extern char* quectel_bg95_get_imei(void);
	extern char* quectel_bg95_get_revision(void);
	extern char* quectel_bg95_get_sim_number(void);

	err += json_add_str(device_obj, "imei",  (const char*)quectel_bg95_get_imei());
	err += json_add_str(device_obj, "sim",  (const char*)quectel_bg95_get_sim_number());
	err += json_add_str(device_obj, "revision", (const char*)quectel_bg95_get_revision());
	err += json_add_obj(reported_obj, "device", device_obj);
	
	err += json_add_number(data_obj, "batv", bat_voltage);
	err += json_add_number(data_obj, "ts", message_ts);
	err += json_add_number(data_obj, "temp", temp);
	err += json_add_number(data_obj, "humid", humid);
	err += json_add_obj(reported_obj, "status", data_obj);
	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		LOG_ERR("Failed to Json Add, error: %d", err);
		goto cleanup;
	}

	message = cJSON_PrintUnformatted(root_obj);
	if (message == NULL) {
		LOG_ERR("cJSON_Print, error: returned NULL");
		err = -ENOMEM;
		goto cleanup;
	}

	struct aws_iot_data tx_data = {
		.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
		.ptr = message,
		.len = strlen(message)
	};

	LOG_INF("Publishing: %s to AWS IoT broker", message);

	err = aws_iot_send(&tx_data);
	if (err) {
		LOG_ERR("aws_iot_send, error: %d", err);
	}

	cJSON_FreeString(message);
cleanup:
	cJSON_Delete(root_obj);
	return err;
}

static void connect_work_fn(struct k_work *work)
{
	int err;

	if (cloud_connected) {
		return;
	}

	err = aws_iot_connect(NULL);
	if (err) {
		LOG_ERR("aws_iot_connect, error: %d", err);
	}

	LOG_DBG("Next connection retry in %d seconds",
	       CONFIG_CONNECTION_RETRY_TIMEOUT_SECONDS);

	k_work_schedule(&connect_work,
			K_SECONDS(CONFIG_CONNECTION_RETRY_TIMEOUT_SECONDS));
}

static void shadow_update_work_fn(struct k_work *work)
{
	int err;

	if (!cloud_connected) {
		return;
	}

	err = shadow_update(false);
	if (err) {
		LOG_ERR("shadow_update, error: %d", err);
	}

	LOG_DBG("Next data publication in %d seconds",
	       shadow_update_internal_ms);

	k_work_schedule(&shadow_update_work,
			K_SECONDS(shadow_update_internal_ms));
}

static void shadow_update_version_work_fn(struct k_work *work)
{
	int err;

	err = shadow_update(true);
	if (err) {
		LOG_ERR("shadow_update, error: %d", err);
	}
}

static void app_on_subscribed(const char *buf, const char *topic,
				size_t topic_len)
{
	char *str = NULL;
	cJSON *root_obj = NULL, *setting_obj, *interval_obj;

	root_obj = cJSON_Parse(buf);
	if (root_obj == NULL) {
		LOG_ERR("cJSON Parse failure");
		return;
	}

	str = cJSON_Print(root_obj);
	if (str == NULL) {
		LOG_ERR("Failed to print JSON object");
		goto clean_exit;
	}

	LOG_DBG("Received message %s", str);
	
	setting_obj = cJSON_GetObjectItemCaseSensitive(root_obj, "setting");
	if (setting_obj == NULL) {
		LOG_ERR("No 'setting' object found");
		goto clean_exit;
	}

	interval_obj = cJSON_GetObjectItemCaseSensitive(setting_obj, "interval");
	if (interval_obj == NULL) {
		LOG_ERR("No 'interval' object found");
		goto clean_exit;
	}

	LOG_DBG("Update shadow publish from %d to %d", shadow_update_internal_ms, interval_obj->valueint);
	shadow_update_internal_ms = interval_obj->valueint;
	cJSON_FreeString(str);

clean_exit:
	cJSON_Delete(root_obj);
}

void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
	switch (evt->type) {
	case AWS_IOT_EVT_CONNECTING:
		LOG_DBG("AWS_IOT_EVT_CONNECTING");
		break;
	case AWS_IOT_EVT_CONNECTED:
		LOG_DBG("AWS_IOT_EVT_CONNECTED");

		cloud_connected = true;
		(void)k_work_cancel_delayable(&connect_work);

		if (evt->data.persistent_session) {
			LOG_DBG("Persistent session enabled");
		}

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
		/** Successfully connected to AWS IoT broker, mark image as
		 *  working to avoid reverting to the former image upon reboot.
		 */
		boot_write_img_confirmed();
#endif

		/** Send version number to AWS IoT broker to verify that the
		 *  FOTA update worked.
		 */
		k_work_submit(&shadow_update_version_work);

		/** Start sequential shadow data updates.
		 */
		k_work_schedule(&shadow_update_work,
				K_SECONDS(CONFIG_PUBLICATION_INTERVAL_SECONDS));

		break;
	case AWS_IOT_EVT_READY:
		LOG_DBG("AWS_IOT_EVT_READY");
		break;
	case AWS_IOT_EVT_DISCONNECTED:
		LOG_DBG("AWS_IOT_EVT_DISCONNECTED");
		cloud_connected = false;
		(void)k_work_cancel_delayable(&shadow_update_work);
		k_work_schedule(&connect_work, K_NO_WAIT);
		break;
	case AWS_IOT_EVT_DATA_RECEIVED:
		LOG_DBG("AWS_IOT_EVT_DATA_RECEIVED");
		app_on_subscribed(evt->data.msg.ptr, evt->data.msg.topic.str,
				    evt->data.msg.topic.len);
		break;
	case AWS_IOT_EVT_FOTA_START:
		LOG_DBG("AWS_IOT_EVT_FOTA_START");
		k_work_cancel_delayable(&shadow_update_work);
		break;
	case AWS_IOT_EVT_FOTA_ERASE_PENDING:
		LOG_DBG("AWS_IOT_EVT_FOTA_ERASE_PENDING");
		LOG_DBG("Disconnect LTE link or reboot");
		break;
	case AWS_IOT_EVT_FOTA_ERASE_DONE:
		LOG_DBG("AWS_FOTA_EVT_ERASE_DONE");
		LOG_DBG("Reconnecting the LTE link");
		break;
	case AWS_IOT_EVT_FOTA_DONE:
		LOG_DBG("AWS_IOT_EVT_FOTA_DONE");
		LOG_DBG("FOTA done, rebooting device");
		k_sleep(K_SECONDS(10));
		aws_iot_disconnect();
		sys_reboot(0);
		break;
	case AWS_IOT_EVT_FOTA_DL_PROGRESS:
		LOG_DBG("AWS_IOT_EVT_FOTA_DL_PROGRESS, (%d%%)",
		       evt->data.fota_progress);
		break;
	case AWS_IOT_EVT_ERROR:
		LOG_DBG("AWS_IOT_EVT_ERROR, %d", evt->data.err);
		break;
	case AWS_IOT_EVT_FOTA_ERROR:
		LOG_DBG("AWS_IOT_EVT_FOTA_ERROR");
		break;
	default:
		LOG_DBG("Unknown AWS IoT event type: %d", evt->type);
		break;
	}
}

static void work_init(void)
{
	k_work_init_delayable(&shadow_update_work, shadow_update_work_fn);
	k_work_init_delayable(&connect_work, connect_work_fn);
	k_work_init(&shadow_update_version_work, shadow_update_version_work_fn);
}

static int app_topics_subscribe(void)
{
	int err;
	static char subscribed_topic[75] = APP_TOPIC_SETTING;

	const struct aws_iot_topic_data topics_list[APP_TOPICS_COUNT] = {
		[0].str = subscribed_topic,
		[0].len = strlen(subscribed_topic),
	};

	err = aws_iot_subscription_topics_add(topics_list,
					      ARRAY_SIZE(topics_list));
	if (err) {
		LOG_ERR("aws_iot_subscription_topics_add, error: %d", err);
	}

	LOG_DBG("Subscribed topics: %s", subscribed_topic);

	return err;
}

int etc_aws_init(void)
{
	int err;

	cJSON_Init();
	err = aws_iot_init(NULL, aws_iot_event_handler);
	if (err) {
		LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
		return err;
	}

	err = app_topics_subscribe();
	if (err) {
		LOG_ERR("Adding application specific topics failed, error: %d", err);
		return err;
	}

	shadow_update_internal_ms = CONFIG_PUBLICATION_INTERVAL_SECONDS;
	work_init();
	
	k_work_schedule(&connect_work, K_NO_WAIT);
	return 0;
}
