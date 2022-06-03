/***************************************************************************/
/*!
\file       etc_msg.c
\brief      Message for MQTT service

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <zephyr.h>
#include <power/reboot.h>
#include <device.h>
#include <string.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <fs/nvs.h>
#include <cJSON.h>
#include <cJSON_os.h>

#include "etc_msg.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(etc_msg, CONFIG_ETC_APP_LOG_LEVEL);

enum {
    ETC_MSG_HB_STRUCT_SENSOR_ID,
    ETC_MSG_HB_STRUCT_TIME_ID,
    ETC_MSG_HB_STRUCT_BATT_ID,
    ETC_MSG_HB_STRUCT_SIG_ID,
    ETC_MSG_HB_STRUCT_FW_ID,
    ETC_MSG_HB_STRUCT_PACKET_ID,
    ETC_MSG_HB_STRUCT_V1_ID,
    ETC_MSG_HB_STRUCT_V2_ID,
    ETC_MSG_HB_STRUCT_V3_ID,
    ETC_MSG_HB_STRUCT_V4_ID,
    ETC_MSG_HB_STRUCT_V5_ID,
    ETC_MSG_HB_STRUCT_V6_ID,
    ETC_MSG_HB_STRUCT_NUM
};

const char* etc_msg_structure_name[ETC_MSG_HB_STRUCT_NUM] = {
    "sensor_id", 
    "time", 
    "batt", 
    "sig", 
    "fw", 
    "packet",
    "v1", 
    "v2", 
    "v3",
    "v4", 
    "v5", 
    "v6"
};

char* etc_msg_generator(void) {
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        LOG_ERR("Failed to create object for heartbeat");
        return NULL;
    }

    cJSON_AddNumberToObject(json, "modem_id", 80001);
    cJSON_AddNumberToObject(json, "time", 1234567890);
    cJSON_AddNumberToObject(json, "batt", 3.7);
    cJSON_AddNumberToObject(json, "sig", -90);
    cJSON_AddStringToObject(json, "fw", "v0.1.0");
    cJSON_AddNumberToObject(json, "packet", 3);

    cJSON *struct_arr = cJSON_CreateStringArray(etc_msg_structure_name, ETC_MSG_HB_STRUCT_NUM);
    if (struct_arr == NULL) {
        LOG_ERR("Failed to create structure objbect");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddItemToObject(json, "structure", struct_arr);

    cJSON *data_arr = cJSON_CreateArray();
    if (data_arr == NULL) {
        LOG_ERR("Failed to create data objbect");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddItemToObject(json, "data", data_arr);
    char msg_buf[256] = {0x00};
    for (int data_id = 0; data_id < 1; data_id ++) {
        /* TODO: Need to add actual data here */
        etc_msg_struct_t msg_data = {0x00};
        memset(&msg_buf, 0, sizeof(msg_buf));
        snprintf(msg_buf, sizeof(msg_buf), "%d,%d,%.2f,%d,\"%s\",%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", 
            msg_data.sensor_id, msg_data.time, msg_data.bat, msg_data.rssi, msg_data.version,
            msg_data.packet, msg_data.v1, msg_data.v2, msg_data.v3, msg_data.v4, msg_data.v5, msg_data.v6);
        cJSON* element_arr = cJSON_CreateStringArray((const char * const*)&msg_buf, 1);
        cJSON_AddItemToArray(data_arr, element_arr);
    }

    char* msg_output = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return msg_output;
}
