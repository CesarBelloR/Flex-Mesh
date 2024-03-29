#include "ble_helpers.h"
#include "etc_device.h"
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_helpers);

void ble_helpers_handle_reclaim_request(cJSON* json, etc_ble_evt_handler_t handler) {
	/* {"request" : "reclaim", "start" : xxx, "end" : xxxx} */
	LOG_DBG("Reclaim request");
	cJSON *start_json = cJSON_GetObjectItem(json, "start");
	cJSON *end_json = cJSON_GetObjectItem(json, "end");
	if (start_json == NULL || end_json == NULL) {
		LOG_ERR("Missing start/stop parameter");
	} else {
		int start_time = (int)start_json->valuedouble;
		int end_time = (int)end_json->valuedouble;
		if (start_time > end_time) {
			LOG_ERR("start_time > end_time");
		} else {
			LOG_DBG("Start %d - End %d", start_time, end_time);
			if (handler != NULL) {
				struct etc_ble_evt evt = {
					.type = ETC_BLE_EVT_CCC_RECLAIM_READY,
					.reclaim.start_time_s = start_time,
					.reclaim.end_time_s = end_time,
				};
				handler(&evt);
			}
		}
	}
}

void ble_helpers_handle_query_request(cJSON* json, etc_ble_evt_handler_t handler) {
	/* {"request" : "query", "type" : "xxx"} */
	LOG_DBG("Query request");
	cJSON *type_json = cJSON_GetObjectItem(json, "type");
	if (type_json == NULL) {
		LOG_ERR("Missing type parameter");
	} else {
		if (strstr(type_json->valuestring, "unack") != NULL) {
			LOG_DBG("Query the current number of unacknowledged samples");
			cJSON *response_json = cJSON_CreateObject();
			if (response_json == NULL) {
				LOG_ERR("Can't create response object");
				return;
			}

			cJSON_AddStringToObject(response_json, "response", "query");
			cJSON_AddStringToObject(response_json, "type", "unack");
			cJSON_AddNumberToObject(response_json, "data", etc_device_nack_count());
			char *response_msg = cJSON_PrintUnformatted(response_json);
			if (response_msg == NULL) {
				LOG_ERR("Can't create response object");
				cJSON_Delete(response_json);
				return;
			} else {
				LOG_INF("Response message %s", response_msg);
				etc_ble_notify(ETC_BLE_CONFIG_CHAR, response_msg,
					       strlen(response_msg), false);
				cJSON_free(response_msg);
				cJSON_Delete(response_json);
			}
		} else if (strstr(type_json->valuestring, "reclaim") != NULL) {
			LOG_DBG("Query reclaim");
			cJSON *start_json = cJSON_GetObjectItem(json, "start");
			cJSON *end_json = cJSON_GetObjectItem(json, "end");
			if (start_json == NULL || end_json == NULL) {
				LOG_ERR("Missing start/stop parameter");
			} else {
				int start_time = (int)start_json->valuedouble;
				int end_time = (int)end_json->valuedouble;
				if (start_time > end_time) {
					LOG_ERR("start_time > end_time");
				} else {
					LOG_DBG("Start %d - End %d", start_time, end_time);
					if (handler != NULL) {
						struct etc_ble_evt evt = {
							.type = ETC_BLE_EVT_CCC_QUERY_RECLAIM,
							.reclaim.start_time_s = start_time,
							.reclaim.end_time_s = end_time,
						};
						handler(&evt);
					}
				}
			}
		} else {
			LOG_DBG("Unknown request type %s", type_json->valuestring);
		}
	}
}