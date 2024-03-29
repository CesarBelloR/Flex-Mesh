#ifndef BLE_HELPERS__
#define BLE_HELPERS__

#include <cJSON.h>
#include <cJSON_os.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include "etc_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

void ble_helpers_handle_reclaim_request(cJSON* json, etc_ble_evt_handler_t handler);
void ble_helpers_handle_query_request(cJSON* json, etc_ble_evt_handler_t handler);

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* BLE_HELPERS__ */