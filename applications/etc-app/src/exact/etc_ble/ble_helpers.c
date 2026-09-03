#include "ble_helpers.h"
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_helpers);

struct ble_reclaim_info {
	int start;
	int end;
};

static int ble_helpers_get_reclaim_info(cJSON *json, struct ble_reclaim_info *info)
{
	if (json == NULL || info == NULL) {
		return -EINVAL;
	}

	cJSON *start_json = cJSON_GetObjectItem(json, "start");
	cJSON *end_json = cJSON_GetObjectItem(json, "end");
	if (start_json == NULL || end_json == NULL) {
		LOG_ERR("Missing start/stop parameter");
		return -EINVAL;
	}

	int start_time = (int)start_json->valuedouble;
	int end_time = (int)end_json->valuedouble;
	if (start_time > end_time) {
		LOG_ERR("start_time > end_time");
		return -EINVAL;
	}

	LOG_DBG("Start %d - End %d", start_time, end_time);
	info->start = start_time;
	info->end = end_time;
	return 0;
}

char *ble_helpers_prepare_response(const char *response_type, const char *type, bool is_data,
				   int response_data)
{
	cJSON *response_json = cJSON_CreateObject();
	if (response_json == NULL) {
		LOG_ERR("Can't create response object");
		return NULL;
	}

	cJSON_AddStringToObject(response_json, "response", response_type);
	cJSON_AddStringToObject(response_json, "type", type);
	cJSON_AddNumberToObject(response_json, is_data ? "data" : "status", response_data);
	char *response_msg = cJSON_PrintUnformatted(response_json);
	if (response_msg == NULL) {
		LOG_ERR("Can't create response object");
		cJSON_Delete(response_json);
		return NULL;
	}
	cJSON_Delete(response_json);
	return response_msg;
}

int ble_helpers_handle_reclaim_request(cJSON *json, etc_ble_evt_handler_t handler)
{
	/* {"request" : "reclaim", "start" : xxx, "end" : xxxx} */
	LOG_DBG("Reclaim request");
	struct ble_reclaim_info reclaim_info = {0x00};
	int rc = ble_helpers_get_reclaim_info(json, &reclaim_info);
	if (rc) {
		return rc;
	}
	if (handler != NULL) {
		struct etc_ble_evt evt = {
			.type = ETC_BLE_EVT_CCC_RECLAIM_READY,
			.reclaim.start_time_s = reclaim_info.start,
			.reclaim.end_time_s = reclaim_info.end,
		};
		handler(&evt);
	}
	return 0;
}

int ble_helpers_handle_query_request(cJSON *json, etc_ble_evt_handler_t handler)
{
	/* {"request" : "query", "type" : "xxx"} */
	LOG_DBG("Query request");
	cJSON *type_json = cJSON_GetObjectItem(json, "type");
	if (!cJSON_IsString(type_json) || type_json->valuestring == NULL) {
		LOG_ERR("Missing type parameter");
		return -EINVAL;
	}
	if (strstr(type_json->valuestring, "unack") != NULL) {
		LOG_DBG("Query the current number of unacknowledged samples");
		/* Answered from the data module thread: this runs on the
		 * Bluetooth RX thread, which must not wait on a notification. */
		if (handler != NULL) {
			struct etc_ble_evt evt = {.type = ETC_BLE_EVT_CCC_QUERY_UNACK};
			handler(&evt);
		}
		return 0;
	}
	if (strstr(type_json->valuestring, "reclaim") != NULL) {
		LOG_DBG("Query reclaim");
		struct ble_reclaim_info reclaim_info = {0x00};
		int rc = ble_helpers_get_reclaim_info(json, &reclaim_info);
		if (rc) {
			return rc;
		}
		if (handler != NULL) {
			struct etc_ble_evt evt = {
				.type = ETC_BLE_EVT_CCC_QUERY_RECLAIM,
				.reclaim.start_time_s = reclaim_info.start,
				.reclaim.end_time_s = reclaim_info.end,
			};
			handler(&evt);
		}
		return 0;
	}
	LOG_DBG("Unknown request type %s", type_json->valuestring);
	return -EINVAL;
}